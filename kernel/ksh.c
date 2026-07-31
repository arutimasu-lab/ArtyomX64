#include "../lib/gfxlib.h"
#include "../drivers/monitor.h"
#include "../drivers/mouse.h"
#include "../fs/initrd.h"
#include "../fs/task.h"
#include "../lib/syscalls.h"
#include <stdbool.h>
int read(int fd, void *buf, unsigned long count);
int write(int fd, void *buf, unsigned long count);
int exec(const void *path);

#include "../fs/fs.h"
#include "../lib/common.h"
#define RGB(r,g,b) ((b)|((g)<<8)|((r)<<16))

#define COL_BG   RGB(240,240,240)
#define COL_WIN  RGB(210,210,210)
#define COL_BAR  RGB(160,160,160)
#define COL_TEXT RGB(0,0,0)
#define COL_CUR  RGB(0,0,255)
extern int m_cursor_x;
extern int m_cursor_y;
extern int gfx_scale;

extern int mouse_buttons;
typedef struct {
    int x, y, w, h;
    bool dragging;
    int dx, dy;
} Window;

static Window win = { 100, 80, 240, 160, false, 0, 0 };
static bool prev_mouse = false;

static int mx(void) { return m_cursor_x / gfx_scale; }
static int my(void) { return m_cursor_y / gfx_scale; }

#include <stdint.h>
extern uint64_t framebuffer_addr;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;

#define CURSOR_COLOR 0xFFFFFFFF

void draw_cursor(int x, int y)
{
    uint8_t *fb = (uint8_t*)(uintptr_t)framebuffer_addr;

    for (int dy = 0; dy < 3; dy++)
    {
        int py = y + dy;
        if (py < 0 || py >= (int)framebuffer_height) continue;

        uint32_t *row = (uint32_t*)(fb + py * framebuffer_pitch);
        for (int dx = 0; dx < 3; dx++)
        {
            int px = x + dx;
            if (px < 0 || px >= (int)framebuffer_width) continue;
            row[px] = CURSOR_COLOR;
        }
    }
}


static bool in_title(Window *w, int x, int y)
{
    return x >= w->x && x < w->x + w->w &&
           y >= w->y && y < w->y + 16;
}

void draw_window(Window *w)
{
    gfx_rect(w->x, w->y, w->w, w->h, COL_WIN);
    gfx_rect(w->x, w->y, w->w, 16, COL_BAR);
    gfx_draw_string_8x8("Demo window",
        w->x + 4, w->y + 4, COL_TEXT, 0, false);
}

void gui_main(void)
{
    gfx_init();

    while (1) {
        gfx_clear(COL_BG);
        handle_mouse();

        bool m = mouse_buttons & 1;
        bool click = m && !prev_mouse;
        bool release = !m && prev_mouse;

        int x = mx();
        int y = my();

        if (click && in_title(&win, x, y)) {
            win.dragging = true;
            win.dx = x - win.x;
            win.dy = y - win.y;
        }

        if (win.dragging && m) {
            win.x = x - win.dx;
            win.y = y - win.dy;
        }

        if (release)
            win.dragging = false;

        draw_window(&win);

        gfx_rect(x, y, 2, 2, COL_CUR);

        prev_mouse = m;
        __asm__ volatile("hlt");
    }
}


void kshell(void)
{
     char buffer[1024];

    for(;;){

    char* tokens[64];

    monitor_write("\nX ");
    read(0, buffer, 1024);

    int position = 0;
    char *token = strtok(buffer, " ");
    while(token)
    {
        tokens[position] = token;
        position++;
        token = strtok(0, " ");
    }
    if(strcmp(tokens[0], "startx")==0){
       gui_main();
    }
    if(strcmp(tokens[0], "echo")==0){
        monitor_write("\n");
        for(int i = 1; i < position; i++){
            monitor_write(tokens[i]);
            monitor_put(' ');
        }
    }
    if(strcmp(tokens[0], "cat")==0){

        fs_node_t *fsnode = finddir_fs(fs_root, tokens[1]);
        char buf[256];
        u32int sz = read_fs(fsnode, 0, 256, buf);
         monitor_write("\n");
        write(1, buf, sz);

            monitor_write("\n");
    }
    if(strcmp(tokens[0], "ls")==0){
           monitor_write("\n");
     int i = 0;
    struct dirent *node = 0;
    while ( (node = readdir_fs(fs_root, i)) != 0)
    {
        monitor_write("Found file ");
        monitor_write(node->name);
        fs_node_t *fsnode = finddir_fs(fs_root, node->name);
        if ((fsnode->flags&0x7) == FS_DIRECTORY)
        {
            monitor_write("\n\t(directory)\n");
        }
        else
        {
            monitor_write("\n\t contents: \"");
            char buf[256];
            u32int sz = read_fs(fsnode, 0, 256, buf);
            int j;
            for (j = 0; j < sz; j++)
                monitor_put(buf[j]);

            monitor_write("\"\n");
        }
        i++;
    }
    monitor_write("\n");
    }
    if(strcmp(tokens[0], "rm")==0){
        initrd_remove(tokens[1]);
    }
    if(strcmp(tokens[0], "mv")==0){
        initrd_move(tokens[1],tokens[2]);
    }
    if (strcmp(tokens[0], "ex") == 0) {
    exec(tokens[1]);
}

    }
    }
