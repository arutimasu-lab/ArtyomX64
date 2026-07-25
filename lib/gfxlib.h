#pragma once
#include <stdint.h>
#include <stdbool.h>

#define GUI_W 640
#define GUI_H 480

extern int gfx_scale;

void gfx_init(void);

void gfx_clear(uint32_t color);
void gfx_circle(int cx, int cy, int r, uint32_t color);

void gfx_draw_char_8x8(char c, int x, int y,
                       uint32_t fg, uint32_t bg, bool bgon);
void gfx_draw_string_8x8(const char *s, int x, int y,
                         uint32_t fg, uint32_t bg, bool bgon);

void gfx_putpixel_raw(int x, int y, uint32_t color);

uint32_t gfx_width(void);
uint32_t gfx_height(void);
uint32_t gfx_fb_width(void);
uint32_t gfx_fb_height(void);
bool gfx_gpu_available(void);
void gfx_gpu_fallback(void);

uint32_t *gfx_backbuffer(void);
void gfx_present(void);

void gfx_clear_rgb(uint32_t color);
void gfx_pixel(int x, int y, uint32_t color);
uint32_t gfx_get_pixel(int x, int y);

void gfx_rect(int x, int y, int w, int h, uint32_t color);
void gfx_fill_rect(int x, int y, int w, int h, uint32_t color);
void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t argb);
void gfx_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color);
void gfx_rounded_rect_alpha(int x, int y, int w, int h, int radius, uint32_t argb);
void gfx_rect_outline(int x, int y, int w, int h, uint32_t color);
void gfx_rounded_outline(int x, int y, int w, int h, int radius, uint32_t color);

void gfx_hline(int x, int y, int w, uint32_t color);
void gfx_vline(int x, int y, int h, uint32_t color);
void gfx_line(int x0, int y0, int x1, int y1, uint32_t color);

void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void gfx_blur_region(int x, int y, int w, int h, int passes);
void gfx_glass_region(int x, int y, int w, int h, int radius, uint32_t tint_argb, int blur_passes);
void gfx_liquid_glass(int x, int y, int w, int h, int radius, uint32_t tint_argb, int blur_passes);
void gfx_glass_specular(int x, int y, int w, int h, int radius);
void gfx_glass_shadow(int x, int y, int w, int h, int radius, int blur_passes);
void gfx_glass_highlight(int x, int y, int w, int h, int radius);

void gfx_text(const char *s, int x, int y, uint32_t color);
void gfx_text_scaled(const char *s, int x, int y, uint32_t color, int scale);
int  gfx_text_width(const char *s);

void gfx_blit_argb(const uint32_t *src, int sw, int sh, int dx, int dy);
void gfx_blit_argb_scaled(const uint32_t *src, int sw, int sh, int dx, int dy, int dw, int dh);

uint32_t gfx_blend(uint32_t dst, uint32_t src_argb);
uint32_t gfx_lerp_color(uint32_t a, uint32_t b, int t256);

int gfx_sin_deg(int deg);
int gfx_cos_deg(int deg);

void draw_rectangle(int x, int y, int w, int h, uint8_t color);
void draw_circle(int x, int y, int radius, uint8_t color);
void draw_vga_character(uint8_t c, int x, int y, int fg, int bg, bool bgon);
void draw_text_string(const char *text, int x, int y, int fg, int bg, bool bgon);
void draw_vga_character_8x8(uint8_t c, int x, int y, int fg, int bg, bool bgon);
void draw_text_string_8x8(const char *text, int x, int y, int fg, int bg, bool bgon);