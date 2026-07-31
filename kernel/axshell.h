#ifndef AXSHELL_H
#define AXSHELL_H

#include <stdint.h>
#include <stdbool.h>
#include "../lib/axipc.h"

#define AX_OS_NAME    "ArtyomXOS"
#define AX_SHELL_NAME "AXShell"
#define AX_VERSION    "2.0"

#define AX_MAX_WINDOWS 16
#define AX_TITLEBAR_H  28
#define AX_MENUBAR_H   24
#define AX_DOCK_H      56

#define AX_KEY_NONE   0
#define AX_KEY_ENTER  '\n'
#define AX_KEY_BACK   '\b'
#define AX_KEY_ESC    27

typedef enum {
    AX_APP_NONE = 0,
    AX_APP_SETTINGS,
    AX_APP_NOTES,
    AX_APP_FILES,
    AX_APP_TERMINAL,
    AX_APP_IMAGES,
    AX_APP_ABOUT,
    AX_APP_X11
} ax_app_kind;

typedef struct {
    int  x, y;
    int  prev_x, prev_y;
    bool left, right;
    bool prev_left, prev_right;
    bool clicked;
    bool released;
} ax_mouse_state;

typedef struct {
    int x, y, w, h;
} ax_rect_t;

void axshell_main(void);

int64_t ax_syscall_surface(const char *title, int w, int h);
int     ax_syscall_poll(uint32_t canvas_ptr, ax_event *out);
int     ax_syscall_time(ax_time_t *out);
int     ax_syscall_screen(ax_screen_t *out);

void    ax_set_wallpaper(int index);
int     ax_get_wallpaper(void);
void    ax_set_accent(uint32_t color);

int     ax_x_window_create(int x, int y, int w, int h, uint32_t xid);
int     ax_x_window_move_resize(int surf_id, int x, int y, int w, int h);
int     ax_x_window_map(int surf_id);
int     ax_x_window_unmap(int surf_id);
int     ax_x_window_destroy(int surf_id);
int     ax_x_window_raise(int surf_id);
int     ax_x_window_set_title(int surf_id, const char *title);
int     ax_x_window_get_geom(int surf_id, int *x, int *y, int *w, int *h);
uint32_t *ax_x_window_canvas(int surf_id);
int     ax_x_window_canvas_w(int surf_id);
int     ax_x_window_canvas_h(int surf_id);

void    ax_comp_invalidate(ax_rect_t r);
void    ax_comp_invalidate_window(int surf_id);

extern int  m_cursor_x;
extern int  m_cursor_y;
extern int  mouse_buttons;
extern char current_key;
extern int  is_enter_pressed;

#endif
