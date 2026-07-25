#ifndef AXSHELL_H
#define AXSHELL_H

#include <stdint.h>
#include <stdbool.h>


#define AX_OS_NAME    "ArtyomXOS"
#define AX_SHELL_NAME "AXShell"
#define AX_VERSION    "1.0"

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
    AX_APP_ABOUT
} ax_app_kind;

typedef struct ax_window ax_window;

typedef void (*ax_draw_fn)(ax_window *w);
typedef void (*ax_key_fn)(ax_window *w, char key);
typedef void (*ax_click_fn)(ax_window *w, int lx, int ly);

struct ax_window {
    int  id;
    bool used;
    bool visible;
    bool focused;
    bool minimized;
    int  x, y, w, h;
    int  z;
    char title[48];
    ax_app_kind kind;

    bool dragging;
    int  drag_dx, drag_dy;

    ax_draw_fn  on_draw;
    ax_key_fn   on_key;
    ax_click_fn on_click;

    char  text[4096];
    int   text_len;
    int   scroll;
    int   sel;
    void *user;
};

typedef struct {
    int  x, y;
    int  prev_x, prev_y;
    bool left, right;
    bool prev_left, prev_right;
    bool clicked;
    bool released;
} ax_mouse_state;

void axshell_main(void);

ax_window *ax_window_open(ax_app_kind kind, const char *title, int x, int y, int w, int h);
void ax_window_close(ax_window *w);
void ax_window_focus(ax_window *w);

void ax_set_wallpaper(int index);
int  ax_get_wallpaper(void);
void ax_set_accent(uint32_t color);

extern int  m_cursor_x;
extern int  m_cursor_y;
extern int  mouse_buttons;
extern char current_key;
extern int  is_enter_pressed;


#endif