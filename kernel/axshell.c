#include "axshell.h"
#include "x11.h"
#include "../lib/gfxlib.h"
#include "../lib/axipc.h"
#include "../drivers/mouse.h"
#include "../drivers/keyboard.h"
#include "../drivers/gpu/ixg_driver.h"
#include "../drivers/usb.h"
#include "../fs/fs.h"
#include "../lib/common.h"
#include "../mm/malloc.h"
#include <stdbool.h>
#include <stdint.h>

static void launch_about(void);
static void request_launch(const char *elf);

extern void handle_mouse(void);
extern u8int inb(u16int port);
extern void outb(u16int port, u8int val);
extern int exec(const char* path);
extern void yield(void);
extern int  task_spawn(void (*entry)(void));
extern void *image_load(char *elf_start, unsigned int size);
extern u32int read_fs(fs_node_t *node, u32int offset, u32int size, u8int *buffer);
extern fs_node_t *finddir_fs(fs_node_t *node, char *name);
extern fs_node_t *fs_root;

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

#define AX_MENU_MAX      4
#define AX_EV_QUEUE_SIZE 32
#define AX_EV_QUEUE_MASK (AX_EV_QUEUE_SIZE - 1)

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
    bool dragging;
    bool resizing;
    int  x, y, w, h;
    int  z;
    int  drag_dx, drag_dy;
    char title[48];
    ax_app_kind kind;

    uint32_t *canvas;
    int       canvas_w, canvas_h;

    ax_event  ev_queue[AX_EV_QUEUE_SIZE];
    int       ev_head, ev_tail;

    int anim;

    uint32_t xid;
    bool     is_x;
} ax_surface_win;

static ax_surface_win surfaces[AX_MAX_WINDOWS];
static int z_counter = 1;
static int active_surface = -1;

static ax_mouse_state mouse;

static bool ctx_menu_open = false;
static int  ctx_x, ctx_y;

static int scr_w, scr_h;

static unsigned char last_ascii = 0;
static int key_hold_frames = 0;

#define AX_DIRTY_MAX 32

typedef struct {
    ax_rect_t r;
    bool used;
} ax_dirty_t;

static ax_dirty_t dirty_rects[AX_DIRTY_MAX];
static int dirty_count = 0;
static bool dirty_full = true;

void ax_comp_invalidate(ax_rect_t r)
{
    if (r.w <= 0 || r.h <= 0) return;
    if (r.x < 0) { r.w += r.x; r.x = 0; }
    if (r.y < 0) { r.h += r.y; r.y = 0; }
    if (r.x + r.w > scr_w) r.w = scr_w - r.x;
    if (r.y + r.h > scr_h) r.h = scr_h - r.y;
    if (r.w <= 0 || r.h <= 0) return;
    if (dirty_full) return;
    for (int i = 0; i < dirty_count; i++) {
        ax_rect_t *d = &dirty_rects[i].r;
        if (r.x >= d->x && r.y >= d->y &&
            r.x + r.w <= d->x + d->w && r.y + r.h <= d->y + d->h)
            return;
        if (!(r.x + r.w < d->x || r.x > d->x + d->w ||
              r.y + r.h < d->y || r.y > d->y + d->h)) {
            int nx = r.x < d->x ? r.x : d->x;
            int ny = r.y < d->y ? r.y : d->y;
            int nw = (r.x + r.w > d->x + d->w ? r.x + r.w : d->x + d->w) - nx;
            int nh = (r.y + r.h > d->y + d->h ? r.y + r.h : d->y + d->h) - ny;
            d->x = nx; d->y = ny; d->w = nw; d->h = nh;
            return;
        }
    }
    if (dirty_count < AX_DIRTY_MAX) {
        dirty_rects[dirty_count].r = r;
        dirty_rects[dirty_count].used = true;
        dirty_count++;
    } else {
        dirty_full = true;
    }
}

void ax_comp_invalidate_window(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return;
    ax_surface_win *w = &surfaces[surf_id];
    if (!w->used) return;
    ax_rect_t r = { w->x - 8, w->y - 8, w->w + 16, w->h + AX_TITLEBAR_H + 16 };
    ax_comp_invalidate(r);
}

static void ax_comp_reset_dirty(void)
{
    dirty_count = 0;
    dirty_full = false;
    for (int i = 0; i < AX_DIRTY_MAX; i++) dirty_rects[i].used = false;
}

typedef struct {
    int x, y;
    bool dragging;
    int drag_dx, drag_dy;
    bool visible;
} ax_widget_state;

static ax_widget_state w_clock   = { 460, 30,  false, 0, 0, true };
static ax_widget_state w_cal     = { 440, 130, false, 0, 0, true };
static ax_widget_state w_sysmon  = { 440, 300, false, 0, 0, true };
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

static ax_menu_t menus[AX_MENU_MAX];
static int menu_count = 0;
static int open_menu = -1;

void ax_set_wallpaper(int index)
{
    if (index < 0) index = 0;
    if (index >= WALLPAPER_COUNT) index = WALLPAPER_COUNT - 1;
    if (index != wallpaper_index) {
        prev_wallpaper_index = wallpaper_index;
        wallpaper_index = index;
        wallpaper_fade = 0;
        wallpaper_fade_dir = 1;
        dirty_full = true;
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
// Добавьте эту функцию перед draw_wallpaper()
static void gfx_vgradient_rect(int x, int y, int w, int h, uint32_t top, uint32_t bot)
{
    for (int j = 0; j < h; j++) {
        uint32_t color = gfx_lerp_color(top, bot, j * 256 / h);
        gfx_fill_rect(x, y + j, w, 1, color);
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
        dirty_full = true;
    }
    if (wallpaper_mode == AX_WALL_SLIDESHOW && photo_count > 1) {
        slideshow_counter++;
        if (slideshow_counter >= SLIDESHOW_INTERVAL) {
            slideshow_counter = 0;
            prev_photo_index = current_photo;
            current_photo = (current_photo + 1) % photo_count;
            wallpaper_fade = 0;
            wallpaper_fade_dir = 1;
            dirty_full = true;
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
}

static void surf_refocus_top(void)
{
    ax_surface_win *best = 0;
    int bestz = -1;
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *w = &surfaces[i];
        if (!w->used || !w->visible || w->minimized) continue;
        if (w->z > bestz) { bestz = w->z; best = w; }
    }
    for (int i = 0; i < AX_MAX_WINDOWS; i++) surfaces[i].focused = false;
    if (best) {
        best->focused = true;
        active_surface = best->id;
    } else {
        active_surface = -1;
    }
}

static void surf_push_event(ax_surface_win *w, ax_event ev)
{
    int next = (w->ev_head + 1) & AX_EV_QUEUE_MASK;
    if (next == w->ev_tail) {
        if (ev.type == AX_EV_MOUSE || ev.type == AX_EV_RESIZE) {
            int last = (w->ev_head - 1) & AX_EV_QUEUE_MASK;
            if (last != w->ev_tail && w->ev_queue[last].type == ev.type) {
                w->ev_queue[last] = ev;
                return;
            }
        }
        return;
    }
    w->ev_queue[w->ev_head] = ev;
    w->ev_head = next;
}

static int surf_alloc(const char *title, int w, int h, ax_app_kind kind)
{
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *s = &surfaces[i];
        if (s->used) continue;
        memset((u8int*)s, 0, sizeof(ax_surface_win));
        s->id = i;
        s->used = true;
        s->visible = true;
        s->kind = kind;
        s->canvas_w = w;
        s->canvas_h = h;
        s->w = w; s->h = h;
        s->x = scr_w/2 - w/2 + (z_counter % 4) * 24;
        s->y = scr_h/2 - h/2 + (z_counter % 4) * 18;
        if (s->y < AX_MENUBAR_H + 4) s->y = AX_MENUBAR_H + 4;
        if (s->y + s->h + AX_TITLEBAR_H > scr_h - AX_DOCK_H - 4)
            s->y = scr_h - AX_DOCK_H - 4 - s->h - AX_TITLEBAR_H;
        if (s->y < AX_MENUBAR_H + 4) s->y = AX_MENUBAR_H + 4;
        s->canvas = (uint32_t*)malloc((uint32_t)w * h * 4);
        if (!s->canvas) { s->used = false; return -1; }
        for (int p = 0; p < w * h; p++) s->canvas[p] = ARGB(0xF0,28,28,34);
        int k = 0;
        while (title[k] && k < 47) { s->title[k] = title[k]; k++; }
        s->title[k] = 0;
        s->anim = 0;
        surf_focus(s);
        return i;
    }
    return -1;
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

int ax_syscall_poll(uint32_t canvas_ptr, ax_event *out)
{
    for (int i = 0; i < AX_MAX_WINDOWS; i++) {
        ax_surface_win *s = &surfaces[i];
        if (s->used && (uint32_t)(uintptr_t)s->canvas == canvas_ptr) {
            if (s->ev_head == s->ev_tail) { out->type = AX_EV_NONE; yield(); return 0; }
            *out = s->ev_queue[s->ev_tail];
            s->ev_tail = (s->ev_tail + 1) & AX_EV_QUEUE_MASK;
            return 1;
        }
    }
    out->type = AX_EV_NONE;
    return 0;
}

int ax_syscall_time(ax_time_t *out) { ax_time_now(out); return 0; }
int ax_syscall_screen(ax_screen_t *out) { out->width = scr_w; out->height = scr_h; return 0; }

static void draw_titlebar(ax_surface_win *w)
{
    uint32_t base = w->focused ? ARGB(0xCC,60,60,70) : ARGB(0xAA,45,45,52);
    gfx_liquid_glass(w->x, w->y, w->w, AX_TITLEBAR_H, 0, base, 2);

    int cy = w->y + AX_TITLEBAR_H / 2;
    gfx_circle(w->x + 16, cy, 6, RGB(255,95,86));
    gfx_circle(w->x + 36, cy, 6, RGB(255,189,46));
    gfx_circle(w->x + 56, cy, 6, RGB(39,201,63));

    int tw = gfx_text_width(w->title);
    gfx_text(w->title, w->x + w->w/2 - tw/2, w->y + 10,
             w->focused ? RGB(255,255,255) : RGB(170,170,180));
}

static void draw_window_shadow(ax_surface_win *w)
{
    int off = 6;
    for (int i = 0; i < 4; i++) {
        uint32_t a = (uint32_t)(0x28 - i * 8) << 24;
        gfx_fill_rect_alpha(w->x + off + i, w->y + off + i,
                            w->w, w->h + AX_TITLEBAR_H, a);
    }
}

static void draw_surface(ax_surface_win *w)
{
    if (!w->visible || w->minimized) return;

    draw_window_shadow(w);

    int alpha = 0xFF;
    if (w->anim < AX_FADE_FRAMES) {
        alpha = (w->anim * 0xFF) / AX_FADE_FRAMES;
        w->anim++;
    }

    if (w->w == w->canvas_w && w->h == w->canvas_h)
        gfx_blit_argb(w->canvas, w->canvas_w, w->canvas_h, w->x, w->y + AX_TITLEBAR_H);
    else
        gfx_blit_argb_scaled(w->canvas, w->canvas_w, w->canvas_h,
                              w->x, w->y + AX_TITLEBAR_H, w->w, w->h);

    if (alpha < 0xFF) {
        uint32_t fade_a = (uint32_t)(0xFF - alpha) << 24;
        gfx_fill_rect_alpha(w->x, w->y, w->w, w->h + AX_TITLEBAR_H, fade_a);
    }

    gfx_rect_outline(w->x, w->y + AX_TITLEBAR_H, w->w, w->h, ARGB(0x40,255,255,255));
    draw_titlebar(w);

    gfx_line(w->x + w->w - AX_RESIZE_GRIP, w->y + AX_TITLEBAR_H + w->h - 2,
             w->x + w->w - 2, w->y + AX_TITLEBAR_H + w->h - AX_RESIZE_GRIP,
             ARGB(0x70,255,255,255));
    gfx_line(w->x + w->w - AX_RESIZE_GRIP + 5, w->y + AX_TITLEBAR_H + w->h - 2,
             w->x + w->w - 2, w->y + AX_TITLEBAR_H + w->h - AX_RESIZE_GRIP + 5,
             ARGB(0x70,255,255,255));
}

static void surf_sort_by_z(ax_surface_win **order, int n)
{
    for (int i = 1; i < n; i++) {
        ax_surface_win *key = order[i];
        int j = i - 1;
        while (j >= 0 && order[j]->z > key->z) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }
}

static ax_time_t clock_cache;
static int clock_cache_tick = -1;
static int frame_counter = 0;

static void clock_tick(void)
{
    if (frame_counter == clock_cache_tick) return;
    ax_time_now(&clock_cache);
    clock_cache_tick = frame_counter;
}

static void widget_clock(int cx, int cy, int r)
{
    clock_tick();
    ax_time_t t = clock_cache;

    gfx_liquid_glass(cx - r - 6, cy - r - 6, (r + 6) * 2, (r + 6) * 2,
                     r + 6, ARGB(0x60,30,30,40), 2);
    gfx_circle(cx, cy, r, ARGB(0xE0,250,250,252));
    gfx_circle(cx, cy, r - 3, ARGB(0xFF,40,42,52));

    for (int hh = 0; hh < 12; hh++) {
        int ang = hh * 30;
        int sx = cx + (r - 6) * gfx_cos_deg(ang) / 1000;
        int sy = cy + (r - 6) * gfx_sin_deg(ang) / 1000;
        gfx_fill_rect(sx - 1, sy - 1, 3, 3, RGB(220,220,230));
    }

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
    clock_tick();
    ax_time_t t = clock_cache;
    int w = 168, h = 150;
    gfx_liquid_glass(x, y, w, h, 12, ARGB(0x80,30,30,42), 3);

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
    gfx_liquid_glass(x, y, w, h, 12, ARGB(0x80,30,40,38), 3);
    gfx_text("System Monitor", x + 12, y + 8, RGB(255,255,255));

    fake_cpu_load = (fake_cpu_load + 7) % 100;
    fake_mem_load = 40 + ((fake_mem_load + 3) % 40);

    gfx_text("CPU", x + 12, y + 30, RGB(200,200,210));
    gfx_fill_rect(x + 50, y + 30, 100, 8, ARGB(0xFF,50,50,60));
    gfx_fill_rect(x + 50, y + 30, fake_cpu_load, 8, RGB(48,209,88));

    gfx_text("MEM", x + 12, y + 52, RGB(200,200,210));
    gfx_fill_rect(x + 50, y + 52, 100, 8, ARGB(0xFF,50,50,60));
    gfx_fill_rect(x + 50, y + 52, fake_mem_load, 8, accent);

    gfx_text(AX_OS_NAME " x86_64", x + 12, y + 74, RGB(150,150,160));
}

static void draw_widgets(void)
{
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
    menus[0].label = "ArtyomX";
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
    gfx_liquid_glass(0, 0, scr_w, AX_MENUBAR_H, 0, ARGB(0xB0,20,20,26), 2);

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
            gfx_liquid_glass(mx0, my0, mw, mh, 6, ARGB(0xE0,40,40,48), 2);
            for (int j = 0; j < menus[i].item_count; j++) {
                int iy = my0 + 4 + j * 22;
                if (mouse.x >= mx0 && mouse.x < mx0 + mw &&
                    mouse.y >= iy && mouse.y < iy + 22)
                    gfx_fill_rect_alpha(mx0 + 4, iy, mw - 8, 22, accent & 0x60FFFFFF);
                gfx_text(menus[i].items[j], mx0 + 10, iy + 7, RGB(230,230,240));
            }
        }
    }

    clock_tick();
    ax_time_t t = clock_cache;
    char clk[16];
    clk[0] = '0' + t.hour/10; clk[1] = '0' + t.hour%10; clk[2] = ':';
    clk[3] = '0' + t.minute/10; clk[4] = '0' + t.minute%10;
    clk[5] = 0;
    gfx_text(clk, scr_w - 60, 8, RGB(255,255,255));
}

static void system_power_sleep(void){ __asm__ volatile("hlt"); }
static void reboot(void){ outb(0x64, 0xFE); for(;;) __asm__ volatile("hlt"); }
static void system_power_off(void){ outb(0x604, (u8int)0x00); for(;;) __asm__ volatile("hlt"); }

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
    { "term",   "Term",  RGB(40,40,48)   },
    { "test",   "Test",  RGB(90,160,250) },
    { "notes",  "Notes", RGB(255,214,90) },
    { "calc",   "Calc",  RGB(255,159,10) },
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

    gfx_liquid_glass(x0, y0, total, AX_DOCK_H, 18, ARGB(0x88,40,40,50), 3);

    for (int i = 0; i < DOCK_COUNT; i++) {
        int ix = x0 + 14 + i * (DOCK_ICON + 14);
        int iy = y0 + 8;
        bool hover = mouse.x >= ix && mouse.x < ix + DOCK_ICON &&
                     mouse.y >= iy && mouse.y < iy + DOCK_ICON;
        int sz = hover ? DOCK_ICON + 4 : DOCK_ICON;
        int ox = ix - (sz - DOCK_ICON) / 2;
        int oy = iy - (sz - DOCK_ICON);
        gfx_rounded_rect(ox, oy, sz, sz, 10, dock_items[i].color);
        gfx_rounded_outline(ox, oy, sz, sz, 10, ARGB(0x50,255,255,255));
        if (i == 0)
            draw_icon(ICON_FILES, icon_pal_files, ox + 4, oy + 4, 2);
        int tw = gfx_text_width(dock_items[i].label);
        gfx_text(dock_items[i].label, ix + DOCK_ICON/2 - tw/2, iy + DOCK_ICON + 2, RGB(220,220,230));
    }
}

static inline void serial_putc_ax(char c){ outb(0x3F8, (u8int)c); }
static inline void serial_puts_ax(const char *s){ while(*s) serial_putc_ax(*s++); }

static void app_trampoline(void)
{
    char path[32];
    int k = 0;
    while (pending_launch[k] && k < 31) { path[k] = pending_launch[k]; k++; }
    path[k] = 0;
    launch_request = 0;

    serial_puts_ax("APP_TRAMPOLINE_BEGIN\n");
    serial_puts_ax("APP_PATH:"); serial_puts_ax(path); serial_putc_ax('\n');

    fs_node_t *fsnode = finddir_fs(fs_root, path);
    if (!fsnode) { serial_puts_ax("APP_FS_NOT_FOUND\n"); for(;;) yield(); }

    static char buf[65536];
    memset((u8int*)buf, 0, sizeof(buf));
    u32int sz = read_fs(fsnode, 0, sizeof(buf), buf);
    if (!sz) { serial_puts_ax("APP_READ_ZERO\n"); for(;;) yield(); }

    serial_puts_ax("APP_IMAGE_LOAD\n");
    void *entry = image_load(buf, sz);
    if (!entry) { serial_puts_ax("APP_IMAGE_LOAD_FAIL\n"); for(;;) yield(); }

    serial_puts_ax("APP_JUMP\n");
    void (*go)(void) = (void(*)(void))entry;
    go();

    serial_puts_ax("APP_RETURNED\n");
    for (;;) yield();
}

static void request_launch(const char *elf)
{
    if (launch_request) return;
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
    gfx_liquid_glass(cx, cy, CTX_W, h, 8, ARGB(0xE0,40,40,48), 2);
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
    gfx_liquid_glass(about_x, about_y, w, h, 12, ARGB(0xF0,30,30,42), 4);
    gfx_text_scaled(AX_OS_NAME, about_x + 30, about_y + 26, RGB(255,255,255), 3);
    gfx_text("Shell: " AX_SHELL_NAME "  v" AX_VERSION, about_x + 30, about_y + 70, accent);
    gfx_text("64-bit UNIX compatible", about_x + 30, about_y + 92, RGB(210,210,220));
    if (gfx_gpu_available()) {
        gfx_text("GPU: iXlinx [accelerated]", about_x + 30, about_y + 108, RGB(100,255,100));
    } else {
        gfx_text("GPU: Software render", about_x + 30, about_y + 108, RGB(255,200,100));
    }
    gfx_text("Powered by iXlinx and ArtyomX", about_x + 30, about_y + 124, RGB(180,180,190));
    gfx_text("[ click to close ]", about_x + 30, about_y + h - 26, RGB(140,140,150));

    if (alpha < 0xFF) {
        uint32_t fade_a = (uint32_t)(0xFF - alpha) << 24;
        gfx_fill_rect_alpha(about_x, about_y, w, h, fade_a);
    }
}

static void draw_cursor(int x, int y)
{
    uint32_t blk = RGB(0,0,0), wht = RGB(255,255,255);
    for (int j = 0; j < 16; j++)
        for (int i = 0; i <= j && i < 11; i++) {
            if (i == 0 || i == j || j == 15) gfx_pixel(x + i, y + j, blk);
            else gfx_pixel(x + i, y + j, wht);
        }
}

static void update_mouse(void)
{
    handle_mouse();
    
    // Инвалидируем старую позицию курсора перед обновлением
    if (mouse.x != m_cursor_x || mouse.y != m_cursor_y) {
        ax_rect_t old_cursor = { mouse.x - 2, mouse.y - 2, 20, 20 };
        ax_comp_invalidate(old_cursor);
    }
    
    mouse.prev_x = mouse.x; mouse.prev_y = mouse.y;
    mouse.prev_left = mouse.left; mouse.prev_right = mouse.right;
    mouse.x = m_cursor_x;
    mouse.y = m_cursor_y;
    mouse.left  = (mouse_buttons & 1) != 0;
    mouse.right = (mouse_buttons & 2) != 0;
    mouse.clicked  = mouse.left && !mouse.prev_left;
    mouse.released = !mouse.left && mouse.prev_left;
    
    // Инвалидируем новую позицию курсора
    if (mouse.x != m_cursor_x || mouse.y != m_cursor_y) {
        // Это уже новая позиция, если координаты изменились
    }
}

static bool in_resize_grip(ax_surface_win *w, int mx, int my)
{
    int gx = w->x + w->w - AX_RESIZE_GRIP;
    int gy = w->y + AX_TITLEBAR_H + w->h - AX_RESIZE_GRIP;
    return mx >= gx && my >= gy && mx < w->x + w->w && my < w->y + AX_TITLEBAR_H + w->h;
}

static void surf_close(ax_surface_win *w)
{
    ax_event ev = {0};
    ev.type = AX_EV_CLOSE;
    surf_push_event(w, ev);
    if (w->is_x) {
        x11_notify_window_event(w->id, X11_EV_DESTROY, w->x, w->y, w->w, w->h);
        x11_on_window_destroyed(w->id);
    }
    if (w->canvas) { free(w->canvas); w->canvas = 0; }
    w->used = false;
    w->visible = false;
    w->focused = false;
    if (active_surface == w->id) surf_refocus_top();
}

static void push_mouse_event(ax_surface_win *w, int lx, int ly)
{
    ax_event ev = {0};
    ev.type = AX_EV_MOUSE;
    ev.mx = lx;
    ev.my = ly;
    ev.buttons = mouse_buttons;
    surf_push_event(w, ev);
}

// Добавить статические переменные в начало файла
static int old_mouse_x = 0;
static int old_mouse_y = 0;

static void process_input(void)
{
    // Обновляем состояние мыши из аппаратных данных
    mouse.prev_x = mouse.x; 
    mouse.prev_y = mouse.y;
    mouse.prev_left = mouse.left; 
    mouse.prev_right = mouse.right;
    mouse.x = m_cursor_x;
    mouse.y = m_cursor_y;
    mouse.left  = (mouse_buttons & 1) != 0;
    mouse.right = (mouse_buttons & 2) != 0;
    mouse.clicked  = mouse.left && !mouse.prev_left;
    mouse.released = !mouse.left && mouse.prev_left;
    
    // Если курсор сдвинулся, перерисовываем старую и новую позиции
    if (mouse.x != mouse.prev_x || mouse.y != mouse.prev_y) {
        ax_rect_t old = { mouse.prev_x - 4, mouse.prev_y - 4, 24, 24 };
        ax_rect_t new = { mouse.x - 4, mouse.y - 4, 24, 24 };
        ax_comp_invalidate(old);
        ax_comp_invalidate(new);
        // НЕ устанавливаем dirty_full = true здесь!
    }

    // Обработка клика по окну About - ПЕРВАЯ!
    if (about_open && mouse.clicked) {
        if (mouse.x >= about_x && mouse.x < about_x + 360 &&
            mouse.y >= about_y && mouse.y < about_y + 220) {
            about_open = false;
            dirty_full = true; // Это остается, так как окно исчезает
            return;
        }
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
            ax_comp_invalidate_window(w->id);
            int lx = mouse.x - w->x;
            int ly = mouse.y - w->y;
            if (in_resize_grip(w, mouse.x, mouse.y)) {
                w->resizing = true;
            } else if (ly < AX_TITLEBAR_H) {
                if (lx >= 10 && lx <= 22) {
                    surf_close(w);
                    return;
                }
                if (lx >= 30 && lx <= 42) {
                    w->minimized = true;
                    surf_refocus_top();
                    return;
                }
                w->dragging = true;
                w->drag_dx = mouse.x - w->x;
                w->drag_dy = mouse.y - w->y;
            } else {
                push_mouse_event(w, lx, ly - AX_TITLEBAR_H);
                if (w->is_x)
                    x11_notify_window_event(w->id, X11_EV_BUTTON, lx, ly - AX_TITLEBAR_H, 0, 0);
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
                ax_comp_invalidate_window(w->id);
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
                ax_comp_invalidate_window(w->id);
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

    {
        extern unsigned char keyboard_map[128];
        unsigned char raw = (unsigned char)current_key;
        unsigned char ascii = (raw < 128) ? keyboard_map[raw] : 0;
        if (ascii && active_surface >= 0) {
            if (ascii != last_ascii) {
                key_hold_frames = 0;
            } else {
                key_hold_frames++;
            }
            if (key_hold_frames == 0 || key_hold_frames > 20) {
                ax_surface_win *w = &surfaces[active_surface];
                if (w->used && w->focused) {
                    ax_event ev = {0};
                    ev.type = AX_EV_KEY; ev.key = (char)ascii;
                    surf_push_event(w, ev);
                    if (w->is_x)
                        x11_notify_window_event(w->id, X11_EV_KEY, 0, 0, (int)ascii, 0);
                }
                if (key_hold_frames > 24) key_hold_frames = 21;
            }
            last_ascii = ascii;
        } else {
            last_ascii = 0;
            key_hold_frames = 0;
        }
    }
}

static void compose(void)
{
    frame_counter++;

    // Если есть грязные области - перерисовываем фон
    if (dirty_full) {
        // Полная перерисовка
        draw_wallpaper();
        wallpaper_tick();
        dirty_full = false;
    } else if (dirty_count > 0) {
        // Частичная перерисовка - перерисовываем фон только в dirty областях
        for (int i = 0; i < dirty_count; i++) {
            ax_rect_t r = dirty_rects[i].r;
            // Обрезаем область до размеров экрана
            if (r.x < 0) { r.w += r.x; r.x = 0; }
            if (r.y < 0) { r.h += r.y; r.y = 0; }
            if (r.x + r.w > scr_w) r.w = scr_w - r.x;
            if (r.y + r.h > scr_h) r.h = scr_h - r.y;
            if (r.w <= 0 || r.h <= 0) continue;
            
            // Рисуем фон в этой области
            if (wallpaper_mode == AX_WALL_GRADIENT || photo_count == 0) {
                // Для градиента - рисуем вертикальный градиент в области
                // Просто рисуем весь фон для упрощения
                // Так как это только для курсора, это нормально
                draw_wallpaper();
                break;
            } else {
                // Для фото - перерисовываем весь фон
                draw_wallpaper();
                break;
            }
        }
    }
    
    // Обновляем анимацию обоев (если нужно)
    if (wallpaper_fade_dir == 1 && !dirty_full) {
        wallpaper_tick();
    }

    draw_widgets();

    {
        ax_surface_win *order[AX_MAX_WINDOWS];
        int n = 0;
        for (int i = 0; i < AX_MAX_WINDOWS; i++) {
            ax_surface_win *w = &surfaces[i];
            if (w->used && w->visible && !w->minimized) order[n++] = w;
        }
        surf_sort_by_z(order, n);
        for (int i = 0; i < n; i++) draw_surface(order[i]);
    }

    draw_about();
    draw_menubar();
    draw_dock();
    draw_context_menu();
    
    draw_cursor(mouse.x, mouse.y);

    x11_poll();

    gfx_present();
    
    // Сбрасываем dirty после отображения
    ax_comp_reset_dirty();

    if (gfx_gpu_available()) {
        static int gpu_health = 0;
        gpu_health++;
        if (gpu_health >= 300) {
            gpu_health = 0;
            if (!ixg_driver_is_accel_ready()) {
                gfx_gpu_fallback();
            }
        }
    }

    if (launch_request) {
        task_spawn(app_trampoline);
        launch_request = 0;
    }
    yield();
}
int ax_x_window_create(int x, int y, int w, int h, uint32_t xid)
{
    if (w < AX_WIN_MIN_W) w = AX_WIN_MIN_W;
    if (h < AX_WIN_MIN_H) h = AX_WIN_MIN_H;
    if (w > scr_w) w = scr_w;
    if (h > scr_h - AX_MENUBAR_H - AX_DOCK_H) h = scr_h - AX_MENUBAR_H - AX_DOCK_H;
    int id = surf_alloc("X11", w, h, AX_APP_NONE);
    if (id < 0) return -1;
    ax_surface_win *s = &surfaces[id];
    s->is_x = true;
    s->xid = xid;
    s->visible = false;
    if (x >= 0 && x + w <= scr_w) s->x = x;
    if (y >= AX_MENUBAR_H && y + h + AX_TITLEBAR_H <= scr_h - AX_DOCK_H) s->y = y;
    return id;
}

int ax_x_window_move_resize(int surf_id, int x, int y, int w, int h)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    if (w < AX_WIN_MIN_W) w = AX_WIN_MIN_W;
    if (h < AX_WIN_MIN_H) h = AX_WIN_MIN_H;
    if (w > scr_w) w = scr_w;
    if (h > scr_h - AX_MENUBAR_H - AX_DOCK_H) h = scr_h - AX_MENUBAR_H - AX_DOCK_H;
    bool size_changed = (w != s->w || h != s->h);
    s->x = x; s->y = y;
    s->w = w; s->h = h;
    if (size_changed) {
        if (s->canvas) free(s->canvas);
        s->canvas_w = w; s->canvas_h = h;
        s->canvas = (uint32_t*)malloc((uint32_t)w * h * 4);
        if (s->canvas) {
            for (int p = 0; p < w * h; p++) s->canvas[p] = ARGB(0xF0,28,28,34);
            ax_event ev = {0};
            ev.type = AX_EV_RESIZE; ev.w = w; ev.h = h;
            surf_push_event(s, ev);
        }
        x11_notify_window_event(surf_id, X11_EV_RESIZE, x, y, w, h);
        x11_notify_window_event(surf_id, X11_EV_EXPOSE, x, y, w, h);
    }
    return 0;
}

int ax_x_window_map(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    s->visible = true;
    surf_focus(s);
    x11_notify_window_event(surf_id, X11_EV_EXPOSE, s->x, s->y, s->w, s->h);
    return 0;
}

int ax_x_window_unmap(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    s->visible = false;
    return 0;
}

int ax_x_window_destroy(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    surf_close(s);
    return 0;
}

int ax_x_window_raise(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    surf_focus(s);
    return 0;
}

int ax_x_window_set_title(int surf_id, const char *title)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    int k = 0;
    while (title[k] && k < 47) { s->title[k] = title[k]; k++; }
    s->title[k] = 0;
    return 0;
}

int ax_x_window_get_geom(int surf_id, int *x, int *y, int *w, int *h)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return -1;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return -1;
    *x = s->x; *y = s->y; *w = s->w; *h = s->h;
    return 0;
}

uint32_t *ax_x_window_canvas(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return 0;
    ax_surface_win *s = &surfaces[surf_id];
    if (!s->used) return 0;
    return s->canvas;
}

int ax_x_window_canvas_w(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return 0;
    return surfaces[surf_id].used ? surfaces[surf_id].canvas_w : 0;
}

int ax_x_window_canvas_h(int surf_id)
{
    if (surf_id < 0 || surf_id >= AX_MAX_WINDOWS) return 0;
    return surfaces[surf_id].used ? surfaces[surf_id].canvas_h : 0;
}

void axshell_main(void)
{
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x80);
    outb(0x3F8, 0x03);
    outb(0x3F9, 0x00);
    outb(0x3FB, 0x03);
    outb(0x3FA, 0xC7);
    outb(0x3FC, 0x0B);

    serial_puts_ax("AXSHELL_START\n");

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
     // Инициализация состояния мыши
    mouse.x = m_cursor_x;
    mouse.y = m_cursor_y;
    mouse.prev_x = m_cursor_x;
    mouse.prev_y = m_cursor_y;
    mouse.left = false;
    mouse.right = false;
    mouse.prev_left = false;
    mouse.prev_right = false;
    mouse.clicked = false;
    mouse.released = false;

    // Убираем old_mouse_x и old_mouse_y - они не нужны
    // old_mouse_x = m_cursor_x;
    // old_mouse_y = m_cursor_y;

    init_menus();
    scan_photos();
    x11_init(scr_w, scr_h);

    launch_about();
    dirty_full = true; // Первый кадр - перерисовываем всё

    while (1) {
        handle_mouse();  // Обновляем аппаратные данные мыши
        process_input(); // Обрабатываем ввод
        usb_poll();
        compose();       // Рисуем всё
        __asm__ volatile("hlt");
    }
}

