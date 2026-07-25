#ifndef UAPI_IPC_H
#define UAPI_IPC_H

#include <stdint.h>

#define GFX_DRIVER 0

#define GFX_SET_PIXEL 0
#define GFX_SET_X 1
#define GFX_SET_Y 2
#define GFX_SET_COL 3
#define GFX_CLEAR 4
#define GFX_INIT 5

typedef struct ipc_msg {
    uint32_t type;
    uint32_t arg0;
    uint32_t arg1;
    uint32_t arg2;
} ipc_msg_t;

#endif