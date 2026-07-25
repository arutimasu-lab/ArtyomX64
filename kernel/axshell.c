#include "axshell.h"
#include "../lib/gfxlib.h"
#include "../lib/axipc.h"
#include "../drivers/mouse.h"
#include "../drivers/keyboard.h"
// #include "../drivers/gpu/ixg_driver.h"  // ПОЛНОСТЬЮ УБРАТЬ
#include "../fs/fs.h"
#include "../lib/common.h"
//#include "../mm/malloc.h"
#include "../mm/kheap.h"
#include "../fs/task.h"
#include <stdbool.h>
#include <stdint.h>
#include "../dev/tty.h"
//#include "elf_loader.h"

#define malloc kmalloc
#define free kfree
void (*current_app_entry)(void);
/* Forward static prototypes */
static void launch_about(void);
static void request_launch(const char *elf);

extern void serial_putc_ax(char c){ outb(0x3F8, (u8int)c); }
extern void serial_puts_ax(const char *s){ while(*s) serial_putc_ax(*s++); }
extern void handle_mouse(void);
extern u8int inb(u16int port);
extern void outb(u16int port, u8int val);
extern int exec(const char* path);
extern void yield(void);
//extern int  task_spawn(void (*entry)(void));
extern void *image_load(char *elf_start, unsigned int size);
extern u32int read_fs(fs_node_t *node, u32int offset, u32int size, u8int *buffer);
extern fs_node_t *finddir_fs(fs_node_t *node, char *name);
extern fs_node_t *fs_root;
extern void debug_putnum(uint64_t n);
static char     pending_launch[32];
static volatile int launch_request = 0;

#define ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define RGB(r,g,b)    ARGB(0xFF,r,g,b)

#define AX_RESIZE_GRIP 14
#define AX_WIN_MIN_W   160
#define AX_WIN_MIN_H   100

#define AX_FADE_FRAMES  18

#define AX_WALL_GRADIENT  0
#define AX_WALL_PHOTO     1
#define AX_WALL_SLIDESHOW 2

#define MAX_PHOTOS 8
#define SLIDESHOW_INTERVAL 600

static uint32_t accent = RGB(10,132,255);

static const uint32_t wallpapers_top[] = {
    RGB(34,40,82), RGB(120,40,90), RGB(20,80,80), RGB(60,30,70), RGB(15,15,20)
};
static const uint32_t wallpapers_bot[] = {
    RGB(90,120,200), RGB(230,120,90), RGB(60,180,160), RGB(150,80,170), RGB(50,50,70)
};
#define WALLPAPER_COUNT 5
static int wallpaper_index = 0;
static int wallpaper_mode  = AX_WALL_GRADIENT;

typedef struct {
    uint32_t *pixels;
    int w, h;
    bool valid;
} ax_photo_t;

static ax_photo_t photos[MAX_PHOTOS];
static int photo_count = 0;
static int current_photo = 0;
static int slideshow_counter = 0;
static int wallpaper_fade = 256;
static int wallpaper_fade_dir = 0;
static int prev_wallpaper_index = 0;
static int prev_photo_index = 0;

typedef struct {
    int  id;
    bool used;
    bool visible;
    bool focused;
    bool minimized;
    bool maximized;      // Добавлено: флаг развернутого состояния
    bool dragging;
    bool resizing;
    int  x, y, w, h;
    int  restore_x, restore_y, restore_w, restore_h;  // Добавлено: сохранение размера до разворачивания
    int  z;
    int  drag_dx, drag_dy;
    char title[48];
    ax_app_kind kind;
    uint32_t *canvas;
    int       canvas_w, canvas_h;
    ax_event  ev_queue[16];
    int       ev_head, ev_tail;
    int anim;
    bool dirty;

    tty_device_t *bound_tty;
} ax_surface_win;

static ax_surface_win surfaces[AX_MAX_WINDOWS];
static int z_counter = 1;
static int active_surface = -1;

static ax_mouse_state mouse;

static bool ctx_menu_open = false;
static int  ctx_x, ctx_y;
static char prev_key_char = 0;

static int scr_w, scr_h;

typedef struct {
    int x, y;
    bool dragging;
    int drag_dx, drag_dy;
    bool visible;
} ax_widget_state;

static ax_widget_state w_clock   = { 460, 30,  false, 0, 0, true };
static ax_widget_state w_cal     = { 440, 130, false, 0, 0, true };
static ax_widget_state w_sysmon  = { 440, 300, false, 0, 0, false };
static ax_widget_state *widgets[] = { &w_clock, &w_cal, &w_sysmon };
#define WIDGET_COUNT 3
static int dragging_widget = -1;

typedef struct {
    const char *label;
    int x_pos;
    int w;
    bool open;
    const char **items;
    int item_count;
} ax_menu_t;

static const char *menu_file_items[]  = { "New Window", "Close Window", "Quit" };
static const char *menu_edit_items[]  = { "Cut", "Copy", "Paste", "Select All" };
static const char *menu_view_items[]  = { "Toggle Widgets", "Gradient WP", "Photo WP", "Slideshow", "Next Accent" };

static ax_menu_t menus[4];
static int menu_count = 4;
static int open_menu = -1;

static uint32_t cursor_bg[16][16];
static int cursor_old_x = -1, cursor_old_y = -1;


static void draw_cursor(int x, int y)
{
    // Ограничение координат курсора
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + 16 > scr_w) x = scr_w - 16;
    if (y + 16 > scr_h) y = scr_h - 16;

    // Восстановление старого фона
    if (cursor_old_x >= 0 && cursor_old_y >= 0) {
        for (int j = 0; j < 16; j++) {
            for (int i = 0; i < 16; i++) {
                int px = cursor_old_x + i;
                int py = cursor_old_y + j;
                if (px >= 0 && px < scr_w && py >= 0 && py < scr_h) {
                    gfx_pixel(px, py, cursor_bg[j][i]);
                }
            }
        }
    }

    // Сохранение нового фона
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < scr_w && py >= 0 && py < scr_h) {
                cursor_bg[j][i] = gfx_get_pixel(px, py);
            }
        }
    }

    // Рисование курсора
    uint32_t blk = RGB(0,0,0), wht = RGB(255,255,255);
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i <= j && i < 11; i++) {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < scr_w && py >= 0 && py < scr_h) {
                if (i == 0 || i == j || j == 15) {
                    gfx_pixel(px, py, blk);
                } else {
                    gfx_pixel(px, py, wht);
                }
            }
        }
    }

    cursor_old_x = x;
    cursor_old_y = y;
}

static void draw_titlebar(ax_surface_win *w)
{
    uint32_t base = w->focused ? ARGB(0xCC,60,60,70) : ARGB(0xAA,45,45,52);
    
    // Заголовок с закруглениями сверху
    gfx_rounded_rect(w->x, w->y, w->w, AX_TITLEBAR_H, 6, base);
    gfx_rounded_outline(w->x, w->y, w->w, AX_TITLEBAR_H, 6, 0x60FFFFFF);
    
    // Подсветка
    for (int j = 0; j < 6; j++) {
        for (int i = 4; i < w->w - 4; i++) {
            int px = w->x + i, py = w->y + j;
            int alpha = 0x30 - (j * 0x30 / 6);
            if (alpha < 0) alpha = 0;
            uint32_t c = gfx_get_pixel(px, py);
            if (c != 0) {
                gfx_pixel(px, py, gfx_blend(c, ((uint32_t)alpha << 24) | 0x00FFFFFF));
            }
        }
    }

    // Кнопки
    int cy = w->y + AX_TITLEBAR_H / 2;
    gfx_circle(w->x + 16, cy, 6, RGB(255,95,86));
    gfx_circle(w->x + 36, cy, 6, RGB(255,189,46));
    gfx_circle(w->x + 56, cy, 6, RGB(39,201,63));

    // Текст
    int tw = gfx_text_width(w->title);
    gfx_text(w->title, w->x + w->w/2 - tw/2, w->y + 10,
             w->focused ? RGB(255,255,255) : RGB(170,170,180));
}

void draw_surface(ax_surface_win *w)
{
    if (!w->visible || w->minimized) return;

    int alpha = 0xFF;
    if (w->anim < AX_FADE_FRAMES) {
        alpha = (w->anim * 0xFF) / AX_FADE_FRAMES;
        w->anim++;
    }

    // Тень окна
    gfx_rounded_rect(w->x + 4, w->y + AX_TITLEBAR_H + 4, w->w, w->h, 6, 0x40000000);
    
    // Фон окна (только если нет канваса или он прозрачный)
    // gfx_rounded_rect(w->x, w->y + AX_TITLEBAR_H, w->w, w->h, 6, ARGB(0xF0,28,28,34));
    
    // Обводка
    gfx_rounded_outline(w->x, w->y + AX_TITLEBAR_H, w->w, w->h, 6, 0x40FFFFFF);

    // ВСЕГДА рисуем канвас, даже если dirty=false
    if (w->canvas) {
        //serial_puts_ax("DRAW_CANVAS\n");
        if (w->w == w->canvas_w && w->h == w->canvas_h) {
            gfx_blit_argb(w->canvas, w->canvas_w, w->canvas_h, w->x + 2, w->y + AX_TITLEBAR_H + 2);
        } else {
            gfx_blit_argb_scaled(w->canvas, w->canvas_w, w->canvas_h,
                                  w->x + 2, w->y + AX_TITLEBAR_H + 2, w->w - 4, w->h - 4);
        }
        w->dirty = false;
    } else {
        serial_puts_ax("NO_CANVAS\n");
    }

    draw_titlebar(w);

    // Ресайз гриф
    gfx_line(w->x + w->w - AX_RESIZE_GRIP, w->y + AX_TITLEBAR_H + w->h - 2,
             w->x + w->w - 2, w->y + AX_TITLEBAR_H + w->h - AX_RESIZE_GRIP,
             ARGB(0x70,255,255,255));
    gfx_line(w->x + w->w - AX_RESIZE_GRIP + 5, w->y + AX_TITLEBAR_H + w->h - 2,
             w->x + w->w - 2, w->y + AX_TITLEBAR_H + w->h - AX_RESIZE_GRIP + 5,
             ARGB(0x70,255,255,255));
}

void ax_set_wallpaper(int index)
{
    if (index < 0) index = 0;
    if (index >= WALLPAPER_COUNT) index = WALLPAPER_COUNT - 1;
    if (index != wallpaper_index) {
        prev_wallpaper_index = wallpaper_index;
        wallpaper_index = index;
        wallpaper_fade = 0;
        wallpaper_fade_dir = 1;
    }
}
int ax_get_wallpaper(void) { return wallpaper_index; }
void ax_set_accent(uint32_t color) { accent = color; }

static const uint8_t ICON_FILES[16][16] = {
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,3,3,3,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,3,3,3,3,3,2,1,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,3,3,3,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,1,2,2,2,2,2,2,2,2,1,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
};

static void draw_icon(const uint8_t icon[16][16], const uint32_t *pal,
                      int x, int y, int scale)
{
    for (int j = 0; j < 16; j++)
        for (int i = 0; i < 16; i++) {
            uint8_t idx = icon[j][i];
            if (idx == 0) continue;
            gfx_fill_rect(x + i * scale, y + j * scale, scale, scale, pal[idx]);
        }
}

static int rtc_read(int reg)
{
    outb(0x70, reg);
    return inb(0x71);
}
static int bcd2bin(int v) { return (v & 0x0F) + ((v >> 4) * 10); }

static void ax_time_now(ax_time_t *t)
{
    t->second = bcd2bin(rtc_read(0x00));
    t->minute = bcd2bin(rtc_read(0x02));
    t->hour   = bcd2bin(rtc_read(0x04));
    t->day    = bcd2bin(rtc_read(0x07));
    t->month  = bcd2bin(rtc_read(0x08));
    t->year   = 2000 + bcd2bin(rtc_read(0x09));
}

static bool load_bmp_from_fs(const char *name, ax_photo_t *out)
{
    fs_node_t *node = finddir_fs(fs_root, (char*)name);
    if (!node) return false;

    static u8int hdrbuf[54];
    u32int sz = read_fs(node, 0, 54, hdrbuf);
    if (sz < 54) return false;
    if (hdrbuf[0] != 'B' || hdrbuf[1] != 'M') return false;

    uint32_t data_offset = hdrbuf[10] | (hdrbuf[11]<<8) | (hdrbuf[12]<<16) | (hdrbuf[13]<<24);
    int32_t width  = hdrbuf[18] | (hdrbuf[19]<<8) | (hdrbuf[20]<<16) | (hdrbuf[21]<<24);
    int32_t height = hdrbuf[22] | (hdrbuf[23]<<8) | (hdrbuf[24]<<16) | (hdrbuf[25]<<24);
    uint16_t bpp   = hdrbuf[28] | (hdrbuf[29]<<8);

    if (width <= 0 || height <= 0) return false;
    if (bpp != 24 && bpp != 32) return false;
    if (width > 640 || height > 480) return false;

    int abs_h = (height < 0) ? -height : height;
    uint32_t *buf = (uint32_t*)malloc((uint32_t)width * abs_h * 4);
    if (!buf) return false;

    int row_bytes = width * (bpp / 8);
    int padded = (row_bytes + 3) & ~3;
    int total_data = padded * abs_h;
    if (total_data > 65536) total_data = 65536;

    static u8int databuf[65536];
    memset(databuf, 0, sizeof(databuf));
    read_fs(node, data_offset, total_data, databuf);

    for (int y = 0; y < abs_h; y++) {
        int src_y = (height < 0) ? y : (abs_h - 1 - y);
        u8int *row = databuf + src_y * padded;
        for (int x = 0; x < width; x++) {
            u8int b = row[x * (bpp/8)];
            u8int g = row[x * (bpp/8) + 1];
            u8int r = row[x * (bpp/8) + 2];
            buf[y * width + x] = ARGB(0xFF, r, g, b);
        }
    }

    out->pixels = buf;
    out->w = width;
    out->h = abs_h;
    out->valid = true;
    return true;
}

static void scan_photos(void)
{
    static const char *names[] = { "wp1.bmp", "wp2.bmp", "wp3.bmp", "wp4.bmp",
                                    "wp5.bmp", "wp6.bmp", "wp7.bmp", "wp8.bmp" };
    photo_count = 0;
    for (int i = 0; i < MAX_PHOTOS; i++) {
        if (load_bmp_from_fs(names[i], &photos[photo_count]))
            photo_count++;
        if (photo_count >= MAX_PHOTOS) break;
    }
}

static void draw_wallpaper(void)
{
    if (wallpaper_mode == AX_WALL_GRADIENT || photo_count == 0) {
        gfx_vgradient(0, 0, scr_w, scr_h,
                      wallpapers_top[wallpaper_index], wallpapers_bot[wallpaper_index]);
        return;
    }

    if (wallpaper_mode == AX_WALL_PHOTO || wallpaper_mode == AX_WALL_SLIDESHOW) {
        int idx = current_photo;
        if (idx < 0 || idx >= photo_count || !photos[idx].valid) {
            gfx_vgradient(0, 0, scr_w, scr_h,
                          wallpapers_top[wallpaper_index], wallpapers_bot[wallpaper_index]);
            return;
        }
        gfx_blit_argb_scaled(photos[idx].pixels, photos[idx].w, photos[idx].h,
                              0, 0, scr_w, scr_h);
        if (wallpaper_fade < 256 && wallpaper_fade_dir == 1) {
            uint32_t fade_a = (uint32_t)(256 - wallpaper_fade) * 0xFF / 256;
            gfx_fill_rect_alpha(0, 0, scr_w, scr_h, (fade_a << 24));
        }
    }
}

static void wallpaper_tick(void)
{
    if (wallpaper_fade_dir == 1) {
        wallpaper_fade += 16;
        if (wallpaper_fade >= 256) { wallpaper_fade = 256; wallpaper_fade_dir = 0; }
    }
    if (wallpaper_mode == AX_WALL_SLIDESHOW && photo_count > 1) {
        slideshow_counter++;
        if (slideshow_counter >= SLIDESHOW_INTERVAL) {
            slideshow_counter = 0;
            prev_photo_index = current_photo;
            current_photo = (current_photo + 1) % photo_count;
            wallpaper_fade = 0;
            wallpaper_fade_dir = 1;
        }
    }
}

static ax_surface_win *surf_at(int x, int y)
{
    ax_surface_win *best = 0;
    int bestz = -1;
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *w = &surfaces[i];
        if (!w->used || !w->visible || w->minimized) continue;
        if (x >= w->x && x < w->x + w->w &&
            y >= w->y && y < w->y + w->h + AX_TITLEBAR_H) {
            if (w->z > bestz) { bestz = w->z; best = w; }
        }
    }
    return best;
}

static void surf_focus(ax_surface_win *w)
{
    for (int i = 0; i < AX_MAX_WINDOWS; i++) surfaces[i].focused = false;
    w->focused = true;
    w->z = z_counter++;
    active_surface = w->id;
    active_master_tty = w->bound_tty;
}

static void surf_push_event(ax_surface_win *w, ax_event ev)
{
    if (!w || !w->used) {
        serial_puts_ax("ERROR: push_event to null/used window\n");
        return;
    }
    
    int next = (w->ev_head + 1) % 16;
    if (next == w->ev_tail) return;

    w->ev_queue[w->ev_head] = ev;
    w->ev_head = next;
}
//extern void print_hex(uint64_t val);
static int surf_alloc(const char *title, int w, int h, ax_app_kind kind)
{
    serial_puts_ax("SURF_ALLOC: ");
    serial_puts_ax(title);
    serial_putc_ax('\n');
    
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *s = &surfaces[i];
        if (s->used) continue;
        
        memset((u8int*)s, 0, sizeof(ax_surface_win));
        s->dirty = true;
        s->id = i;
        s->used = true;
        s->visible = true;
        s->kind = kind;
        s->canvas_w = w;
        s->canvas_h = h;
        s->w = w; 
        s->h = h;
        s->x = scr_w/2 - w/2 + (z_counter % 4) * 24;
        s->y = scr_h/2 - h/2 + (z_counter % 4) * 18;
        
        if (s->y < AX_MENUBAR_H + 4) s->y = AX_MENUBAR_H + 4;
        if (s->y + s->h + AX_TITLEBAR_H > scr_h - AX_DOCK_H - 4)
            s->y = scr_h - AX_DOCK_H - 4 - s->h - AX_TITLEBAR_H;
        if (s->y < AX_MENUBAR_H + 4) s->y = AX_MENUBAR_H + 4;
        
        // ИСПОЛЬЗУЕМ malloc вместо статического буфера
        s->canvas = (uint32_t*)malloc(w * h * 4);
        if (!s->canvas) {
            serial_puts_ax("SURF_ALLOC_MALLOC_FAIL\n");
            s->used = false;
            return -1;
        }
        
        serial_puts_ax("CANVAS_ALLOC: ");
       // print_hex((uint64_t)s->canvas);
        serial_putc_ax('\n');
        
        // Заполняем канвас
        for (int p = 0; p < w * h; p++) {
            s->canvas[p] = ARGB(0xF0, 28, 28, 34);
        }
        
        int k = 0;
        while (title[k] && k < 47) { 
            s->title[k] = title[k]; 
            k++; 
        }
        s->title[k] = 0;
        s->anim = 0;
        s->bound_tty = tty_create(s->title);
        
        serial_puts_ax("SURF_ALLOC_OK id=");
        char buf[8];
        int n = 0;
        int x = i;
        char t[8];
        if (x == 0) t[n++] = '0';
        while (x) { t[n++] = '0' + (x % 10); x /= 10; }
        while (n) serial_putc_ax(t[--n]);
        serial_putc_ax('\n');
        
        surf_focus(s);
        return i;
    }
    return -1;
}
static void toggle_maximize(ax_surface_win *w)
{
    if (!w || !w->used) return;
    
    if (!w->maximized) {
        // Сохраняем текущее положение и размер
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_w = w->w;
        w->restore_h = w->h;
        
        // Разворачиваем на весь экран
        w->x = 0;
        w->y = AX_MENUBAR_H;
        w->w = scr_w;
        w->h = scr_h - AX_MENUBAR_H - AX_DOCK_H;
        w->maximized = true;
        
    } else {
        // Восстанавливаем предыдущий размер
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->w = w->restore_w;
        w->h = w->restore_h;
        w->maximized = false;
    }
    
    // Отправляем событие изменения размера
    ax_event ev = {0};
    ev.type = AX_EV_RESIZE;
    ev.w = w->w;
    ev.h = w->h;
    surf_push_event(w, ev);
}
int64_t ax_syscall_surface(const char *title, int w, int h)
{
    if (w < AX_WIN_MIN_W) w = AX_WIN_MIN_W;
    if (h < AX_WIN_MIN_H) h = AX_WIN_MIN_H;
    if (w > scr_w) w = scr_w;
    if (h > scr_h - AX_MENUBAR_H - AX_DOCK_H) h = scr_h - AX_MENUBAR_H - AX_DOCK_H;
    int id = surf_alloc(title, w, h, AX_APP_NONE);
    if (id < 0) return 0;
    return (int64_t)(uintptr_t)surfaces[id].canvas;
}

// В syscalls.c

int ax_syscall_poll(uint32_t canvas_ptr, ax_event *out)
{
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *s = &surfaces[i];
        if (!s->used) continue;

        if ((uint32_t)(uintptr_t)s->canvas == canvas_ptr) {
            if (s->ev_head != s->ev_tail) {
                *out = s->ev_queue[s->ev_tail];
                s->ev_tail = (s->ev_tail + 1) % 16;
                return 1;
            }

            out->type = AX_EV_NONE;
            return 0;
        }
    }

    out->type = AX_EV_NONE;
    return -1;
}
int ax_syscall_commit(uint32_t canvas_ptr) {
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *s = &surfaces[i];
        if (s->used && (uint32_t)(uintptr_t)s->canvas == canvas_ptr) {
            s->dirty = true;
            
            // НЕМЕДЛЕННАЯ ПЕРЕРИСОВКА!
            // Сохраняем текущий контекст и рисуем окно
            draw_surface(s);
            gfx_present();
            
            return 0;
        }
    }
    return -1;
}
int ax_syscall_time(ax_time_t *out) { ax_time_now(out); return 0; }
int ax_syscall_screen(ax_screen_t *out) { out->width = scr_w; out->height = scr_h; return 0; }

// ============================================================
// КРАСИВЫЕ УПРОЩЕННЫЕ ФУНКЦИИ
// ============================================================

static void draw_rounded_rect_with_shadow(int x, int y, int w, int h, int radius, uint32_t color)
{
    // Тень
    gfx_rounded_rect(x + 3, y + 3, w, h, radius, 0x40000000);
    // Основной прямоугольник
    gfx_rounded_rect(x, y, w, h, radius, color);
    // Обводка
    gfx_rounded_outline(x, y, w, h, radius, 0x60FFFFFF);
    // Подсветка сверху
    for (int j = 0; j < h/4 && j < 20; j++) {
        for (int i = 0; i < w; i++) {
            int px = x + i, py = y + j;
            uint32_t c = gfx_get_pixel(px, py);
            if (c != 0) {
                int alpha = 0x20 - (j * 0x20 / (h/4));
                if (alpha < 0) alpha = 0;
                gfx_pixel(px, py, gfx_blend(c, ((uint32_t)alpha << 24) | 0x00FFFFFF));
            }
        }
    }
}



static void widget_clock(int cx, int cy, int r)
{
    ax_time_t t; ax_time_now(&t);

    // Тень
    gfx_circle(cx + 3, cy + 3, r + 6, 0x40000000);
    
    // Фон часов
    gfx_circle(cx, cy, r + 6, ARGB(0x60,30,30,40));
    gfx_circle(cx, cy, r + 6, 0x40FFFFFF);
    gfx_circle(cx, cy, r, ARGB(0xE0,250,250,252));
    gfx_circle(cx, cy, r - 3, ARGB(0xFF,40,42,52));

    // Метки часов
    for (int hh = 0; hh < 12; hh++) {
        int ang = hh * 30;
        int sx = cx + (r - 6) * gfx_cos_deg(ang) / 1000;
        int sy = cy + (r - 6) * gfx_sin_deg(ang) / 1000;
        gfx_fill_rect(sx - 1, sy - 1, 3, 3, RGB(220,220,230));
    }

    // Стрелки
    int hour_ang = (t.hour % 12) * 30 + t.minute / 2 - 90;
    int min_ang  = t.minute * 6 - 90;
    int sec_ang  = t.second * 6 - 90;

    gfx_line(cx, cy, cx + (r/2) * gfx_cos_deg(hour_ang) / 1000,
                     cy + (r/2) * gfx_sin_deg(hour_ang) / 1000, RGB(255,255,255));
    gfx_line(cx, cy, cx + (r*3/4) * gfx_cos_deg(min_ang) / 1000,
                     cy + (r*3/4) * gfx_sin_deg(min_ang) / 1000, RGB(220,220,230));
    gfx_line(cx, cy, cx + (r-8) * gfx_cos_deg(sec_ang) / 1000,
                     cy + (r-8) * gfx_sin_deg(sec_ang) / 1000, accent);
    gfx_circle(cx, cy, 3, accent);
}

static const char *month_names[] = {
    "", "January","February","March","April","May","June",
    "July","August","September","October","November","December"
};
static int days_in_month(int m, int y)
{
    int d[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && ((y%4==0 && y%100!=0) || y%400==0)) return 29;
    if (m >= 1 && m <= 12) return d[m];
    return 30;
}
static int day_of_week(int d, int m, int y)
{
    static int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    if (m < 3) y -= 1;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

static void widget_calendar(int x, int y)
{
    ax_time_t t; ax_time_now(&t);
    int w = 168, h = 150;
    
    // Тень
    gfx_rounded_rect(x + 3, y + 3, w, h, 8, 0x40000000);
    
    // Фон
    gfx_rounded_rect(x, y, w, h, 8, ARGB(0x80,30,30,42));
    gfx_rounded_outline(x, y, w, h, 8, 0x40FFFFFF);

    char hdr[32]; int p = 0;
    const char *mn = month_names[t.month >= 1 && t.month <= 12 ? t.month : 1];
    while (mn[p] && p < 20) { hdr[p] = mn[p]; p++; } hdr[p] = 0;
    gfx_text(hdr, x + 12, y + 8, RGB(255,255,255));

    const char *wd = "S M T W T F S";
    gfx_text(wd, x + 10, y + 26, accent);

    int dow = day_of_week(1, t.month, t.year);
    int dim = days_in_month(t.month, t.year);
    int col = dow, row = 0;
    for (int d = 1; d <= dim; d++) {
        int cellx = x + 10 + col * 22;
        int celly = y + 42 + row * 16;
        if (d == t.day) {
            gfx_circle(cellx + 6, celly + 4, 9, accent);
        }
        char ds[4];
        if (d < 10) { ds[0] = '0' + d; ds[1] = 0; }
        else        { ds[0] = '0' + d/10; ds[1] = '0' + d%10; ds[2] = 0; }
        gfx_text(ds, cellx, celly, RGB(230,230,235));
        col++;
        if (col > 6) { col = 0; row++; }
    }
}

static int fake_cpu_load = 30;
static int fake_mem_load = 45;

static void widget_sysmon(int x, int y)
{
    int w = 168, h = 96;
    
    // Тень
    gfx_rounded_rect(x + 3, y + 3, w, h, 8, 0x40000000);
    
    // Фон
    gfx_rounded_rect(x, y, w, h, 8, ARGB(0x80,30,40,38));
    gfx_rounded_outline(x, y, w, h, 8, 0x40FFFFFF);
    
    gfx_text("System Monitor", x + 12, y + 8, RGB(255,255,255));

    fake_cpu_load = (fake_cpu_load + 7) % 100;
    fake_mem_load = 40 + ((fake_mem_load + 3) % 40);

    gfx_text("CPU", x + 12, y + 30, RGB(200,200,210));
    gfx_rounded_rect(x + 50, y + 30, 100, 8, 4, ARGB(0xFF,50,50,60));
    gfx_rounded_rect(x + 50, y + 30, fake_cpu_load, 8, 4, RGB(48,209,88));

    gfx_text("MEM", x + 12, y + 52, RGB(200,200,210));
    gfx_rounded_rect(x + 50, y + 52, 100, 8, 4, ARGB(0xFF,50,50,60));
    gfx_rounded_rect(x + 50, y + 52, fake_mem_load, 8, 4, accent);

    gfx_text(AX_OS_NAME " x86_64", x + 12, y + 74, RGB(150,150,160));
}

static void draw_widgets(void)
{
     // Часы - правый верхний угол
    w_clock.x = scr_w - 120;
    w_clock.y = AX_MENUBAR_H + 20;
    
    // Календарь - под часами
    w_cal.x = scr_w - 180;
    w_cal.y = AX_MENUBAR_H + 140;
    
    // Системный монитор - под календарем
    w_sysmon.x = scr_w - 180;
    w_sysmon.y = AX_MENUBAR_H + 310;
    if (w_clock.visible) {
        widget_clock(w_clock.x + 50, w_clock.y + 50, 50);
    }
    if (w_cal.visible) {
        widget_calendar(w_cal.x, w_cal.y);
    }
    if (w_sysmon.visible) {
        widget_sysmon(w_sysmon.x, w_sysmon.y);
    }
}

static bool point_in_widget(ax_widget_state *ws, int mx, int my)
{
    if (!ws->visible) return false;
    if (ws == &w_clock) {
        int cx = ws->x + 50, cy = ws->y + 50, r = 56;
        int dx = mx - cx, dy = my - cy;
        return dx*dx + dy*dy <= r*r;
    }
    if (ws == &w_cal)   return mx >= ws->x && mx < ws->x + 168 && my >= ws->y && my < ws->y + 150;
    if (ws == &w_sysmon) return mx >= ws->x && mx < ws->x + 168 && my >= ws->y && my < ws->y + 96;
    return false;
}

static const char *menu_logo_items[] = { "Sleep", "Reboot", "Power Off", "System Info", "Program Manager", "Control Panel" };

static void init_menus(void)
{
    menus[0].label = "AXShell";
    menus[0].x_pos = 12;
    menus[0].w = 96;
    menus[0].open = false;
    menus[0].items = menu_logo_items;
    menus[0].item_count = 6;

    menus[1].label = "File";
    menus[1].x_pos = 130;
    menus[1].w = 40;
    menus[1].open = false;
    menus[1].items = menu_file_items;
    menus[1].item_count = 3;

    menus[2].label = "Edit";
    menus[2].x_pos = 180;
    menus[2].w = 40;
    menus[2].open = false;
    menus[2].items = menu_edit_items;
    menus[2].item_count = 4;

    menus[3].label = "View";
    menus[3].x_pos = 230;
    menus[3].w = 44;
    menus[3].open = false;
    menus[3].items = menu_view_items;
    menus[3].item_count = 5;

    menu_count = 4;
}

static void draw_logo_glyph(int x, int y)
{
    gfx_text("A", x, y, RGB(255,255,255));
}

static void draw_menubar(void)
{
    // Меню бар с тенью
    gfx_rect(0, AX_MENUBAR_H, scr_w, 2, 0x40000000);
    gfx_fill_rect(0, 0, scr_w, AX_MENUBAR_H, ARGB(0xB0,20,20,26));
    
    // Нижняя граница
    gfx_hline(0, AX_MENUBAR_H - 1, scr_w, 0x60FFFFFF);

    int logo_w = 22;
    bool logo_hover = mouse.x >= 8 && mouse.x < 8 + logo_w && mouse.y >= 0 && mouse.y < AX_MENUBAR_H;
    if (logo_hover || menus[0].open)
        gfx_fill_rect_alpha(8 - 4, 2, logo_w + 8, AX_MENUBAR_H - 4, accent & 0x40FFFFFF);
    draw_logo_glyph(12, 8);

    for (int i = 0; i < menu_count; i++) {
        bool hover = mouse.x >= menus[i].x_pos && mouse.x < menus[i].x_pos + menus[i].w &&
                     mouse.y >= 0 && mouse.y < AX_MENUBAR_H;
        if (menus[i].open || hover)
            gfx_fill_rect_alpha(menus[i].x_pos - 4, 2, menus[i].w + 8, AX_MENUBAR_H - 4,
                                accent & 0x40FFFFFF);
        gfx_text(menus[i].label, menus[i].x_pos, 8,
                 (menus[i].open || hover) ? RGB(255,255,255) : RGB(210,210,220));

        if (menus[i].open) {
            int mw = 160;
            int mh = menus[i].item_count * 22 + 8;
            int mx0 = menus[i].x_pos - 4;
            int my0 = AX_MENUBAR_H;
            
            // Тень меню
            gfx_rounded_rect(mx0 + 3, my0 + 3, mw, mh, 6, 0x40000000);
            
            // Фон меню
            gfx_rounded_rect(mx0, my0, mw, mh, 6, ARGB(0xE0,40,40,48));
            gfx_rounded_outline(mx0, my0, mw, mh, 6, 0x60FFFFFF);
            
            for (int j = 0; j < menus[i].item_count; j++) {
                int iy = my0 + 4 + j * 22;
                if (mouse.x >= mx0 && mouse.x < mx0 + mw &&
                    mouse.y >= iy && mouse.y < iy + 22)
                    gfx_fill_rect_alpha(mx0 + 4, iy, mw - 8, 22, accent & 0x60FFFFFF);
                gfx_text(menus[i].items[j], mx0 + 10, iy + 7, RGB(230,230,240));
            }
        }
    }

    ax_time_t t; ax_time_now(&t);
    char clk[16];
    clk[0] = '0' + t.hour/10; clk[1] = '0' + t.hour%10; clk[2] = ':';
    clk[3] = '0' + t.minute/10; clk[4] = '0' + t.minute%10;
    clk[5] = 0;
    gfx_text(clk, scr_w - 60, 8, RGB(255,255,255));

      char width[16]; char height[16];

    /*intToStr(scr_w, width);
    intToStr(scr_h, height);
    gfx_text(width, scr_w - 170, 8, RGB(255,255,255));
    gfx_text(height, scr_w - 120, 8, RGB(255,255,255));
*/

}

static void system_power_sleep(void){ __asm__ volatile("hlt"); }
static void reboot(void) { outb(0x64, 0xFE); for(;;) __asm__ volatile("hlt"); }
static void system_power_off(void){ 
    /*outb(0x604, (u8int)0x00); for(;;) __asm__ volatile("hlt");*/ 
    outw(0xB004, 0x2000);
    outw(0x604, 0x2000);
    outw(0x4004, 0x3400);

}
// Отправка события конкретному окну
static void ax_send_event_to_canvas(uint32_t canvas_ptr, ax_event *ev)
{
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *s = &surfaces[i];
        if (s->used && (uint32_t)(uintptr_t)s->canvas == canvas_ptr) {
            surf_push_event(s, *ev);
            return;
        }
    }
}

// Отправка события активному приложению
static void ax_send_event_to_active(ax_event *ev)
{
    if (active_surface >= 0 && active_surface < AX_MAX_WINDOWS) {
        ax_surface_win *s = &surfaces[active_surface];
        if (s->used && s->focused) {
            surf_push_event(s, *ev);
        }
    }
}
static void menu_click(int mx, int my)
{
    if (open_menu < 0) return;
    if (open_menu >= menu_count) return;
    ax_menu_t *m = &menus[open_menu];
    int mw = 160;
    int mh = m->item_count * 22 + 8;
    int mx0 = m->x_pos - 4;
    int my2 = AX_MENUBAR_H;
    if (mx < mx0 || mx >= mx0 + mw || my < my2 || my >= my2 + mh) {
        m->open = false;
        open_menu = -1;
        return;
    }
    int idx = (my - my2 - 4) / 22;
    if (idx >= 0 && idx < m->item_count) {
        if (open_menu == 3) {
            switch (idx) {
                case 0: w_clock.visible = !w_clock.visible; w_cal.visible = !w_cal.visible; w_sysmon.visible = !w_sysmon.visible; break;
                case 1: wallpaper_mode = AX_WALL_GRADIENT; break;
                case 2: if (photo_count > 0) wallpaper_mode = AX_WALL_PHOTO; break;
                case 3: if (photo_count > 1) wallpaper_mode = AX_WALL_SLIDESHOW; break;
                case 4: { static int ai = 0; static const uint32_t ac[] = { RGB(10,132,255), RGB(255,69,58), RGB(48,209,88), RGB(255,159,10), RGB(191,90,242) }; ai = (ai + 1) % 5; accent = ac[ai]; break; }
            }
        } else if (open_menu == 0) {
            switch (idx) {
                case 0: system_power_sleep(); break;
                case 1: reboot(); break;
                case 2: system_power_off(); break;
                case 3: launch_about(); break;
                case 4: request_launch("progman"); break;
                case 5: request_launch("prefs"); break;
            }
        } else if (open_menu == 1) {
            
            switch (idx) {
                     case 1: // "Close Window" - ЗАКРЫТИЕ АКТИВНОГО ОКНА
            if (active_surface >= 0 && active_surface < AX_MAX_WINDOWS) {
                ax_surface_win *w = &surfaces[active_surface];
                if (w->used && !w->minimized) {
                    // Отправляем событие закрытия
                    ax_event ev = {0};
                    ev.type = AX_EV_CLOSE;
                    surf_push_event(w, ev);
                    
                    // Освобождаем ресурсы
                    if (w->bound_tty) {
                        tty_destroy(w->bound_tty);
                        w->bound_tty = NULL;
                    }
                    if (w->canvas) {
                        free(w->canvas);
                        w->canvas = NULL;
                    }
                    w->used = false;
                    w->visible = false;
                    w->focused = false;
                    
                    // Ищем новое активное окно
                    int new_active = -1;
                    int highest_z = -1;
                    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
                        if (surfaces[i].used && surfaces[i].visible && !surfaces[i].minimized) {
                            if (surfaces[i].z > highest_z) {
                                highest_z = surfaces[i].z;
                                new_active = i;
                            }
                        }
                    }
                    
                    if (new_active >= 0) {
                        surf_focus(&surfaces[new_active]);
                    } else {
                        active_surface = -1;
                    }
                }
            }
            break;
        case 2: // "Quit"
            // Закрываем все окна
            for (int i = 0; i < AX_MAX_WINDOWS; i++) {
                if (surfaces[i].used) {
                    ax_event ev = {0};
                    ev.type = AX_EV_CLOSE;
                    surf_push_event(&surfaces[i], ev);
                    if (surfaces[i].bound_tty) {
                        tty_destroy(surfaces[i].bound_tty);
                        surfaces[i].bound_tty = NULL;
                    }
                    if (surfaces[i].canvas) {
                        free(surfaces[i].canvas);
                        surfaces[i].canvas = NULL;
                    }
                    surfaces[i].used = false;
                }
            }
            active_surface = -1;
            break;
            }
        }
    }
    m->open = false;
    open_menu = -1;
}

static struct {
    const char *elf;
    const char *label;
    uint32_t color;
} dock_items[] = {
    { "xsh",   "Term",  RGB(40,40,48)   },
    //{ "files",  "Files",  RGB(255,214,90)    },
    { "test",   "Test",  RGB(90,160,250) },
    { "notes",  "Notes", RGB(255,214,90) },
    //{ "calc",   "Calc",  RGB(255,159,10) },
    { "prefs",  "Prefs", RGB(150,150,160)},
    { 0,        "About", RGB(191,90,242) },
};
#define DOCK_COUNT 6
#define DOCK_ICON  40

static int dock_x0(void)
{
    int total = DOCK_COUNT * (DOCK_ICON + 14) + 14;
    return scr_w/2 - total/2;
}
static int dock_y0(void) { return scr_h - AX_DOCK_H - 4; }

static const uint32_t icon_pal_files[4] = { 0, RGB(40,90,160), RGB(90,160,250), RGB(220,235,255) };

static void draw_dock(void)
{
    int total = DOCK_COUNT * (DOCK_ICON + 14) + 14;
    int x0 = dock_x0();
    int y0 = dock_y0();

    // Тень дока
    gfx_rounded_rect(x0 + 3, y0 + 3, total, AX_DOCK_H, 18, 0x40000000);
    
    // Фон дока
    gfx_rounded_rect(x0, y0, total, AX_DOCK_H, 18, ARGB(0x88,40,40,50));
    gfx_rounded_outline(x0, y0, total, AX_DOCK_H, 18, 0x60FFFFFF);

    for (int i = 0; i < DOCK_COUNT; i++) {
        int ix = x0 + 14 + i * (DOCK_ICON + 14);
        int iy = y0 + 8;
        bool hover = mouse.x >= ix && mouse.x < ix + DOCK_ICON &&
                     mouse.y >= iy && mouse.y < iy + DOCK_ICON;
        int sz = hover ? DOCK_ICON + 4 : DOCK_ICON;
        int ox = ix - (sz - DOCK_ICON) / 2;
        int oy = iy - (sz - DOCK_ICON);
        
        // Тень иконки
        gfx_rounded_rect(ox + 2, oy + 2, sz, sz, 10, 0x40000000);
        
        // Иконка
        gfx_rounded_rect(ox, oy, sz, sz, 10, dock_items[i].color);
        gfx_rounded_outline(ox, oy, sz, sz, 10, ARGB(0x50,255,255,255));
        
        if (i == 0)
            draw_icon(ICON_FILES, icon_pal_files, ox + 4, oy + 4, 2);
        int tw = gfx_text_width(dock_items[i].label);
        gfx_text(dock_items[i].label, ix + DOCK_ICON/2 - tw/2, iy + DOCK_ICON + 2, RGB(220,220,230));
    }
}

static void launch_about(void);
static void request_launch(const char *elf);


// В axshell.c - упрощенный request_launch

static void request_launch(const char *elf)
{
    if (launch_request) return;
    
    serial_puts_ax("REQUEST_LAUNCH: ");
    serial_puts_ax(elf);
    serial_putc_ax('\n');
    
    // Сохраняем имя для запуска
    int k = 0;
    while (elf[k] && k < 31) { pending_launch[k] = elf[k]; k++; }
    pending_launch[k] = 0;
    launch_request = 1;
}


static void dock_click(int mx, int my)
{
    int x0 = dock_x0();
    int y0 = dock_y0();
    if (my < y0 + 4 || my > y0 + AX_DOCK_H) return;
    for (int i = 0; i < DOCK_COUNT; i++) {
        int ix = x0 + 14 + i * (DOCK_ICON + 14);
        int iy = y0 + 8;
        int sz = DOCK_ICON + 4;
        if (mx >= ix - 2 && mx < ix + sz && my >= iy - 4 && my < iy + sz) {
            if (dock_items[i].elf)
                request_launch(dock_items[i].elf);
            else
                launch_about();
            return;
        }
    }
}

static const char *ctx_items[] = { "Refresh", "Next Wallpaper", "Photo WP", "Slideshow", "Next Accent", "About" };
#define CTX_COUNT 6
#define CTX_W 160
#define CTX_ITEM_H 26

static void draw_context_menu(void)
{
    if (!ctx_menu_open) return;
    int h = CTX_COUNT * CTX_ITEM_H + 8;
    int cx = ctx_x, cy = ctx_y;
    if (cx + CTX_W > scr_w) cx = scr_w - CTX_W - 2;
    if (cy + h > scr_h) cy = scr_h - h - 2;
    
    // Тень
    gfx_rounded_rect(cx + 3, cy + 3, CTX_W, h, 8, 0x40000000);
    
    // Фон
    gfx_rounded_rect(cx, cy, CTX_W, h, 8, ARGB(0xE0,40,40,48));
    gfx_rounded_outline(cx, cy, CTX_W, h, 8, 0x60FFFFFF);
    
    for (int i = 0; i < CTX_COUNT; i++) {
        int iy = cy + 4 + i * CTX_ITEM_H;
        if (mouse.x >= cx && mouse.x < cx + CTX_W &&
            mouse.y >= iy && mouse.y < iy + CTX_ITEM_H)
            gfx_fill_rect_alpha(cx + 4, iy, CTX_W - 8, CTX_ITEM_H, accent & 0x80FFFFFF);
        gfx_text(ctx_items[i], cx + 14, iy + 9, RGB(230,230,240));
    }
}

static const uint32_t accent_cycle[] = {
    RGB(10,132,255), RGB(255,69,58), RGB(48,209,88), RGB(255,159,10), RGB(191,90,242)
};
static int accent_idx = 0;

static void context_menu_click(int mx, int my)
{
    int h = CTX_COUNT * CTX_ITEM_H + 8;
    int cx = ctx_x, cy = ctx_y;
    if (cx + CTX_W > scr_w) cx = scr_w - CTX_W - 2;
    if (cy + h > scr_h) cy = scr_h - h - 2;
    if (mx < cx || mx >= cx + CTX_W || my < cy || my >= cy + h) {
        ctx_menu_open = false; return;
    }
    int idx = (my - cy - 4) / CTX_ITEM_H;
    ctx_menu_open = false;
    switch (idx) {
        case 0: break;
        case 1: ax_set_wallpaper((wallpaper_index + 1) % WALLPAPER_COUNT);
                if (wallpaper_mode == AX_WALL_PHOTO || wallpaper_mode == AX_WALL_SLIDESHOW) {
                    current_photo = (current_photo + 1) % (photo_count > 0 ? photo_count : 1);
                }
                break;
        case 2: if (photo_count > 0) wallpaper_mode = AX_WALL_PHOTO; break;
        case 3: if (photo_count > 1) wallpaper_mode = AX_WALL_SLIDESHOW; break;
        case 4: accent_idx = (accent_idx + 1) % 5; ax_set_accent(accent_cycle[accent_idx]); break;
        case 5: launch_about(); break;
    }
}

static bool about_open = false;
static int  about_x, about_y;
static int  about_anim = 0;
static void launch_about(void) { about_open = true; about_x = scr_w/2 - 180; about_y = scr_h/2 - 110; about_anim = 0; }

static void draw_about(void)
{
    if (!about_open) return;
    int w = 360, h = 220;
    int alpha = 0xFF;
    if (about_anim < AX_FADE_FRAMES) {
        alpha = (about_anim * 0xFF) / AX_FADE_FRAMES;
        about_anim++;
    }
    
    // Тень
    gfx_rounded_rect(about_x + 4, about_y + 4, w, h, 12, 0x50000000);
    
    // Фон
    gfx_rounded_rect(about_x, about_y, w, h, 12, ARGB(0xF0,30,30,42));
    gfx_rounded_outline(about_x, about_y, w, h, 12, 0x60FFFFFF);
    
    gfx_text_scaled(AX_OS_NAME, about_x + 30, about_y + 26, RGB(255,255,255), 3);
    gfx_text("Shell: " AX_SHELL_NAME "  v" AX_VERSION, about_x + 30, about_y + 70, accent);
    gfx_text("64-bit UNIX compatible", about_x + 30, about_y + 92, RGB(210,210,220));
    gfx_text("GPU: Software render", about_x + 30, about_y + 108, RGB(255,200,100));
    gfx_text("Powered by iXlinx and ArtyomX", about_x + 30, about_y + 124, RGB(180,180,190));
    gfx_text("[ click to close ]", about_x + 30, about_y + h - 26, RGB(140,140,150));

    if (alpha < 0xFF) {
        uint32_t fade_a = (uint32_t)(0xFF - alpha) << 24;
        gfx_fill_rect_alpha(about_x, about_y, w, h, fade_a);
    }
}

// ============================================================
// БЫСТРЫЙ КУРСОР С СОХРАНЕНИЕМ ФОНА
// ============================================================


static void update_mouse(void)
{
    handle_mouse();
    mouse.prev_x = mouse.x; mouse.prev_y = mouse.y;
    mouse.prev_left = mouse.left; mouse.prev_right = mouse.right;
    
    // Получаем позицию мыши с ограничениями
    int new_x = m_cursor_x;
    int new_y = m_cursor_y;
    
    // Ограничение позиции мыши
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x >= scr_w) new_x = scr_w - 1;
    if (new_y >= scr_h) new_y = scr_h - 1;
    
    mouse.x = new_x;
    mouse.y = new_y;
    mouse.left  = (mouse_buttons & 1) != 0;
    mouse.right = (mouse_buttons & 2) != 0;
    mouse.clicked  = mouse.left && !mouse.prev_left;
    mouse.released = !mouse.left && mouse.prev_left;
}
static bool in_resize_grip(ax_surface_win *w, int mx, int my)
{
    int gx = w->x + w->w - AX_RESIZE_GRIP;
    int gy = w->y + AX_TITLEBAR_H + w->h - AX_RESIZE_GRIP;
    return mx >= gx && my >= gy && mx < w->x + w->w && my < w->y + AX_TITLEBAR_H + w->h;
}


// Глобальная переменная: какой мастер сейчас активен (на переднем плане)
extern tty_device_t *active_master_tty; 



static void process_input(void)
{
    if (about_open && mouse.clicked) {
        if (mouse.x >= about_x && mouse.x < about_x + 360 &&
            mouse.y >= about_y && mouse.y < about_y + 220) { about_open = false; return; }
    }

    if (mouse.right && !mouse.prev_right) {
        ctx_menu_open = true; ctx_x = mouse.x; ctx_y = mouse.y;
        for (int i = 0; i < menu_count; i++) menus[i].open = false;
        open_menu = -1;
        return;
    }
    if (ctx_menu_open) { if (mouse.clicked) context_menu_click(mouse.x, mouse.y); return; }

    if (mouse.clicked) {
        if (mouse.y < AX_MENUBAR_H) {
            int logo_w = 22;
            if (mouse.x >= 8 && mouse.x < 8 + logo_w) {
                bool was_open = menus[0].open;
                for (int j = 0; j < menu_count; j++) menus[j].open = false;
                menus[0].open = !was_open;
                open_menu = menus[0].open ? 0 : -1;
                return;
            }
            for (int i = 0; i < menu_count; i++) {
                bool was_open = menus[i].open;
                menus[i].open = false;
                if (mouse.x >= menus[i].x_pos && mouse.x < menus[i].x_pos + menus[i].w) {
                    if (was_open) {
                        open_menu = -1;
                    } else {
                        for (int j = 0; j < menu_count; j++) menus[j].open = false;
                        menus[i].open = true;
                        open_menu = i;
                    }
                    return;
                }
            }
            open_menu = -1;
            return;
        }
        if (open_menu >= 0) {
            menu_click(mouse.x, mouse.y);
            return;
        }

        if (mouse.y > scr_h - AX_DOCK_H - 8) { dock_click(mouse.x, mouse.y); return; }

        for (int wi = 0; wi < WIDGET_COUNT; wi++) {
            if (point_in_widget(widgets[wi], mouse.x, mouse.y)) {
                dragging_widget = wi;
                widgets[wi]->dragging = true;
                widgets[wi]->drag_dx = mouse.x - widgets[wi]->x;
                widgets[wi]->drag_dy = mouse.y - widgets[wi]->y;
                return;
            }
        }

        ax_surface_win *w = surf_at(mouse.x, mouse.y);
        if (w) {
            surf_focus(w);
            int lx = mouse.x - w->x;
            int ly = mouse.y - w->y;
            if (in_resize_grip(w, mouse.x, mouse.y)) {
                w->resizing = true;
            } else if (ly < AX_TITLEBAR_H) {
                //Закрыть окно
                if (lx >= 10 && lx <= 22) {
                    ax_event ev = {0}; ev.type = AX_EV_CLOSE;
                    surf_push_event(w, ev);
                    w->used = false;
                    if (w->bound_tty) { tty_destroy(w->bound_tty); w->bound_tty = 0; }
                    if (w->canvas) { free(w->canvas); w->canvas = 0; }
                    // Если это было активное окно, сбрасываем active_surface
                    if (active_surface == w->id) {
                        active_surface = -1;
                        // Находим другое активное окно
                        for (int i = 0; i < AX_MAX_WINDOWS; i++) {
                            if (surfaces[i].used && surfaces[i].visible) {
                                surf_focus(&surfaces[i]);
                                break;
                            }
                        }
                    }
                    return;
                }
                //Свернуть
                if (lx >= 30 && lx <= 42) { w->minimized = true; return; }
                    w->dragging = true;
                    w->drag_dx = mouse.x - w->x;
                    w->drag_dy = mouse.y - w->y;
                } else {
                    ax_event ev = {0};
                    ev.type = AX_EV_MOUSE;
                    ev.mx = lx;
                    ev.my = ly - AX_TITLEBAR_H;
                    ev.buttons = mouse_buttons;
                    surf_push_event(w, ev);
                }if (lx >= 50 && lx <= 62) {
    // Зеленая кнопка - развернуть/восстановить
    toggle_maximize(w);
    return;
}
        }
    }

    if (mouse.left) {
        if (dragging_widget >= 0) {
            ax_widget_state *ws = widgets[dragging_widget];
            if (ws->dragging) {
                ws->x = mouse.x - ws->drag_dx;
                ws->y = mouse.y - ws->drag_dy;
                if (ws->x < 0) ws->x = 0;
                if (ws->y < AX_MENUBAR_H) ws->y = AX_MENUBAR_H;
                if (ws->x > scr_w - 60) ws->x = scr_w - 60;
                if (ws->y > scr_h - AX_DOCK_H - 60) ws->y = scr_h - AX_DOCK_H - 60;
            }
        }
        for (int i = 0; i < AX_MAX_WINDOWS; i++) {
            ax_surface_win *w = &surfaces[i];
            if (!w->used) continue;
            if (w->dragging) {
                w->x = mouse.x - w->drag_dx;
                w->y = mouse.y - w->drag_dy;
                if (w->y < AX_MENUBAR_H) w->y = AX_MENUBAR_H;
                if (w->x < 0) w->x = 0;
                if (w->x + w->w > scr_w) w->x = scr_w - w->w;
                if (w->y + w->h + AX_TITLEBAR_H > scr_h - AX_DOCK_H - 2)
                    w->y = scr_h - AX_DOCK_H - 2 - w->h - AX_TITLEBAR_H;
                if (w->y < AX_MENUBAR_H) w->y = AX_MENUBAR_H;
            }
            if (w->resizing) {
                w->w = mouse.x - w->x;
                w->h = mouse.y - w->y - AX_TITLEBAR_H;
                if (w->w < AX_WIN_MIN_W) w->w = AX_WIN_MIN_W;
                if (w->h < AX_WIN_MIN_H) w->h = AX_WIN_MIN_H;
                if (w->x + w->w > scr_w) w->w = scr_w - w->x;
                if (w->y + w->h + AX_TITLEBAR_H > scr_h - AX_DOCK_H - 2)
                    w->h = scr_h - AX_DOCK_H - 2 - w->y - AX_TITLEBAR_H;
                ax_event ev = {0};
                ev.type = AX_EV_RESIZE; ev.w = w->w; ev.h = w->h;
                surf_push_event(w, ev);
            }
        }
    }

    if (mouse.released) {
        dragging_widget = -1;
        for (int i = 0; i < WIDGET_COUNT; i++) widgets[i]->dragging = false;
        for (int i = 0; i < AX_MAX_WINDOWS; i++) {
            surfaces[i].dragging = false;
            surfaces[i].resizing = false;
        }
    }

 // Отправляем клавиатурные события ВСЕМ фокусированным окнам
   // В функции process_input() файла axshell.c

char k = current_key;
if (k && k != prev_key_char) {
    extern unsigned char keyboard_map[128];
    char ascii = ((unsigned char)k < 128) ? keyboard_map[(unsigned char)k] : 0;
    
    if (ascii) {
        // Подача ввода в консоль отладки (оставьте если нужно)
        //console_feed_input(ascii);

        // --- ИСПРАВЛЕНИЕ ---
        // Берем TTY напрямую из активной поверхности
        if (active_surface >= 0 && active_surface < AX_MAX_WINDOWS) {
            ax_surface_win *w = &surfaces[active_surface];
            
            if (w->used && w->focused && w->bound_tty) {
                tty_device_t *tty = w->bound_tty;
                
                // Обработка Ctrl+C
                if ((tty->termios_c_lflag & ISIG) && ascii == 0x03) {
                    serial_puts_ax("^C\n");
                }

                ax_event ev = {0};
                ev.type = AX_EV_KEY;
                ev.key = (uint32_t)(unsigned char)ascii;
                surf_push_event(w, ev);

                // Запись символа в буфер драйвера PTY
                tty_master_feed(tty, ascii);
            }
        }
    }
}
prev_key_char = k;
    
    // Отправляем события мыши в окно под курсором
if (mouse.clicked || mouse.released || mouse.x != mouse.prev_x || mouse.y != mouse.prev_y) {
    ax_surface_win *w = surf_at(mouse.x, mouse.y);
    if (w) {
        ax_event ev = {0};
        ev.type = AX_EV_MOUSE;
        ev.mx = mouse.x - w->x;
        ev.my = mouse.y - w->y - AX_TITLEBAR_H;
        ev.buttons = mouse_buttons;
        surf_push_event(w, ev);
        
        serial_puts_ax("EVENT: MOUSE to ");
        serial_puts_ax(w->title);
        serial_puts_ax(" at ");
        debug_putnum(ev.mx);
        serial_putc_ax(',');
        debug_putnum(ev.my);
        serial_putc_ax('\n');
    } else {
        // Если окна под курсором нет, сбрасываем фокус
        if (active_surface >= 0) {
            // Проверяем, существует ли еще окно
            bool found = false;
            for (int i = 0; i < AX_MAX_WINDOWS; i++) {
                if (surfaces[i].used && surfaces[i].focused) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                active_surface = -1;
            }
        }
    }
}
}
    


// В axshell.c

static int app_running = 0;  // Флаг, что приложение запущено
// В axshell.c



extern void debug_putnum(uint64_t n);

// В axshell.c

// Глобальные переменные
static void *app_entry_ptr = NULL;
static uint32_t *app_canvas_ptr = NULL;

// Обертка для запуска приложения как задачи
static void app_task_wrapper(void *arg)
{
    uint32_t *canvas = (uint32_t*)arg;
    
    serial_puts_ax("APP_TASK: starting\n");
    
    if (app_entry_ptr) {
        void (*app_entry)(uint32_t*) = (void(*)(uint32_t*))app_entry_ptr;
        app_entry(canvas);
    }
    
    serial_puts_ax("APP_TASK: finished\n");
    task_exit(0);
}

void app_trampoline(void)
{
      // Убедитесь, что pending_launch содержит правильное имя
    char path[32];
    int k = 0;
    while (pending_launch[k] && k < 31) { 
        path[k] = pending_launch[k]; 
        k++; 
    }
    path[k] = 0;
    
    // Для отладки - выводим path
    serial_puts_ax("PATH: ");
    serial_puts_ax(path);
    serial_putc_ax('\n');

    serial_puts_ax("APP_TRAMPOLINE_BEGIN: ");
    serial_puts_ax(path);
    serial_putc_ax('\n');


    // Загружаем ELF
    fs_node_t *fsnode = finddir_fs(fs_root, path);
    if (!fsnode) { 
        serial_puts_ax("APP_FS_NOT_FOUND\n");
        launch_request = 0;
        return;
    }

    static char buf[65536];
    memset((u8int*)buf, 0, sizeof(buf));
    u32int sz = read_fs(fsnode, 0, sizeof(buf), buf);
    if (!sz) { 
        serial_puts_ax("APP_READ_ZERO\n");
        launch_request = 0;
        return;
    }

    serial_puts_ax("APP_IMAGE_LOAD\n");
    void *entry = image_load(buf, sz);
    if (!entry) { 
        serial_puts_ax("APP_IMAGE_LOAD_FAIL\n");
        launch_request = 0;
        return;
    }

    serial_puts_ax("APP_JUMP: entry at ");
    uint64_t addr = (uint64_t)entry;
    char t[16];
    int n = 0;
    if (addr == 0) t[n++] = '0';
    while (addr) { t[n++] = '0' + (addr % 10); addr /= 10; }
    while (n) serial_putc_ax(t[--n]);
    serial_putc_ax('\n');

    // Сохраняем точку входа для задачи
    app_entry_ptr = entry;
    
    
    
    // Запускаем приложение как задачу
    int pid = task_spawn(app_task_wrapper, app_canvas_ptr, path);
    if (pid < 0) {
        serial_puts_ax("TASK_SPAWN_FAIL\n");
        launch_request = 0;
        return;
    }
    
    serial_puts_ax("APP_TASK_SPAWNED: PID=");
    debug_putnum(pid);
    serial_putc_ax('\n');
    
    launch_request = 0;
}

// В compose() - запускаем через exec
static void compose(void)
{
    draw_wallpaper();
    wallpaper_tick();
    draw_widgets();

    // Проверяем, есть ли хоть одно окно
    bool has_windows = false;
    int first_visible = -1;
    
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *w = &surfaces[i];
        if (w->used && w->visible && !w->minimized) {
            has_windows = true;
            if (first_visible == -1) first_visible = i;
            draw_surface(w);
        }
    }

    // Если нет окон, сбрасываем active_surface
    if (!has_windows) {
        active_surface = -1;
        // Восстанавливаем курсор
        cursor_old_x = -1;
        cursor_old_y = -1;
    } else if (active_surface == -1 && first_visible != -1) {
        // Если active_surface сброшен, но есть окна - фокусируем первое
        surf_focus(&surfaces[first_visible]);
    }

    draw_about();
    draw_menubar();
    draw_dock();
    draw_context_menu();
    draw_cursor(mouse.x, mouse.y);

    gfx_present();

    // Запускаем приложение, если есть запрос
    if (launch_request) {
        serial_puts_ax("LAUNCH_REQUEST: ");
        serial_puts_ax(pending_launch);
        serial_putc_ax('\n');
        
        app_trampoline();
        launch_request = 0;
    }
    
    yield();
}
void axshell_main(void)
{
    /* Ensure serial is initialised here as well, so logs are visible */
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x03);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);

    serial_puts_ax("AXSHELL_START\n");

    /* Query actual framebuffer size from gfx before any scaling */
    gfx_init();
    scr_w = gfx_width();
    scr_h = gfx_height();

    serial_puts_ax("SCR:");
    {
        int x = scr_w; char t[16]; int n=0; if(!x) t[n++]='0'; while(x){ t[n++]='0'+(x%10); x/=10; } while(n) serial_putc_ax(t[--n]);
        serial_putc_ax('x');
        x = scr_h; n=0; if(!x) t[n++]='0'; while(x){ t[n++]='0'+(x%10); x/=10; } while(n) serial_putc_ax(t[--n]);
        serial_putc_ax('\n');
    }

    memset((u8int*)surfaces, 0, sizeof(surfaces));
    memset((u8int*)photos, 0, sizeof(photos));

    m_cursor_x = scr_w / 2;
    m_cursor_y = scr_h / 2;

    init_menus();
/*    uint32_t *test_canvas = (uint32_t*)ax_syscall_surface("Test", 300, 200);
if (test_canvas) {
    for (int j = 0; j < 200; j++) {
        for (int i = 0; i < 300; i++) {
            test_canvas[j * 300 + i] = ARGB(0xFF, i * 255 / 300, j * 255 / 200, 128);
        }
    }
}*/
    scan_photos();

    launch_about();

    while (1) {
        update_mouse();
        process_input();
        compose();
         yield();  // ← Переключаемся на другие задачи
        
        __asm__ volatile("hlt");
    }
}