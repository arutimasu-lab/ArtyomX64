#ifndef X11_H
#define X11_H

#include <stdint.h>
#include "ksock.h"

void x11_init(int screen_w, int screen_h);
int  x11_listener_fd(void);
void x11_poll(void);
void x11_notify_window_event(int surf_id, int ev_kind, int x, int y, int w, int h);
int  x11_is_x_window(int surf_id);
void x11_on_window_destroyed(int surf_id);
#define X11_EV_EXPOSE   1
#define X11_EV_RESIZE   2
#define X11_EV_DESTROY  3
#define X11_EV_FOCUS_IN 4
#define X11_EV_BUTTON   5
#define X11_EV_MOTION   6
#define X11_EV_KEY      7

#endif
