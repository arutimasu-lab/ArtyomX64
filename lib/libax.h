#ifndef LIBAX_H
#define LIBAX_H

#include <stdint.h>
#include "axipc.h"

static inline uint32_t *ax_surface(const char *title, int w, int h)
{
    int64_t ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(AX_SYS_SURFACE), "D"((long)title), "S"((long)w), "d"((long)h)
        : "memory");
    return (uint32_t*)(uintptr_t)ret;
}

static inline int ax_poll(uint32_t *canvas, ax_event *ev)
{
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(AX_SYS_POLL), "D"((long)(uintptr_t)canvas), "S"((long)ev)
        : "memory");
    return ret;
}

static inline void ax_commit(uint32_t *canvas)
{
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(AX_SYS_COMMIT), "D"((long)(uintptr_t)canvas)
        : "memory");
    (void)ret;
}

static inline void ax_time(ax_time_t *t)
{
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(AX_SYS_TIME), "D"((long)t)
        : "memory");
    (void)ret;
}

static inline void ax_screen(ax_screen_t *s)
{
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(AX_SYS_SCREEN), "D"((long)s)
        : "memory");
    (void)ret;
}

#define AX_ARGB(a,r,g,b) (((uint32_t)(a)<<24)|((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define AX_RGB(r,g,b)    AX_ARGB(0xFF,r,g,b)

static int g_cw, g_ch;
static uint32_t *g_canvas;

static inline void ax_canvas_dims(int w, int h) { g_cw = w; g_ch = h; }

static inline void ax_px(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= g_cw || y >= g_ch) return;
    g_canvas[y * g_cw + x] = c;
}

static inline void ax_fill(uint32_t c)
{
    for (int i = 0; i < g_cw * g_ch; i++) g_canvas[i] = c;
}

static inline void ax_rect(int x, int y, int w, int h, uint32_t c)
{
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            ax_px(x + i, y + j, c);
}

#include "font8x8_basic.h"

static inline void ax_char(char ch, int x, int y, uint32_t c)
{
    const unsigned char *g = (const unsigned char*)font8x8_basic[(unsigned char)ch];
    for (int cy = 0; cy < 8; cy++)
        for (int cx = 0; cx < 8; cx++)
            if (g[cy] & (1 << cx)) ax_px(x + cx, y + cy, c);
}

static inline void ax_text(const char *s, int x, int y, uint32_t c)
{
    int ox = x;
    while (*s) {
        if (*s == '\n') { y += 9; x = ox; s++; continue; }
        ax_char(*s++, x, y, c);
        x += 8;
    }
}

#endif
