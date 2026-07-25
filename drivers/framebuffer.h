#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>

extern uint64_t framebuffer_addr;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;
extern uint32_t framebuffer_bpp;

#define COLOR_BLACK     0xFF000000
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFFF0000
#define COLOR_GREEN     0xFF00FF00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_CYAN      0xFF00FFFF
#define COLOR_MAGENTA   0xFFFF00FF
#define COLOR_YELLOW    0xFFFFFF00
#define COLOR_GRAY      0xFF808080

void fb_init(uint32_t *mboot_ptr);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_clear(uint32_t color);
uint32_t* fb_get_pixel_ptr(uint32_t x, uint32_t y);
int fb_is_available(void);
void fb_put_char(char c, uint32_t x, uint32_t y, uint32_t color);
void fb_put_string(const char *str, uint32_t x, uint32_t y, uint32_t color);

#endif
