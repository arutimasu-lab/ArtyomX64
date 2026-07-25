#ifndef AXIPC_H
#define AXIPC_H

#include <stdint.h>

#define AX_SYS_SURFACE   160
#define AX_SYS_POLL      161
#define AX_SYS_COMMIT    162
#define AX_SYS_TIME      163
#define AX_SYS_SCREEN    164
#define AX_SYS_TTY_READ  165

#define AX_EV_NONE   0
#define AX_EV_KEY    1
#define AX_EV_MOUSE  2
#define AX_EV_CLOSE  3
#define AX_EV_RESIZE 4

typedef struct {
    uint32_t type;
    uint32_t key;
    int32_t  mx, my;
    uint32_t buttons;
    uint32_t w, h;
} ax_event;

typedef struct {
    int32_t  year, month, day;
    int32_t  hour, minute, second;
} ax_time_t;

typedef struct {
    uint32_t width;
    uint32_t height;
} ax_screen_t;

#endif
