#include "gfxlib.h"
#include "../drivers/framebuffer.h"
#include "../drivers/gpu/ixg_driver.h"
#include "font8x8_basic.h"

extern uint8_t vgafnt[4096];

static uint8_t  *fb;
static uint32_t  fb_w, fb_h, fb_pitch;

static uint32_t *backbuffer;
static uint32_t *blurtmp;
#define LOG_W fb_w

int gfx_scale = 1;
static bool gfx_gpu_active = false;
static bool gfx_gpu_tried  = false;

static const int sin_table[91] = {
       0,  17,  35,  52,  70,  87, 105, 122, 139, 156,
     174, 191, 208, 225, 242, 259, 276, 292, 309, 326,
     342, 358, 375, 391, 407, 423, 438, 454, 469, 485,
     500, 515, 530, 545, 559, 574, 588, 602, 616, 629,
     643, 656, 669, 682, 695, 707, 719, 731, 743, 755,
     766, 777, 788, 799, 809, 819, 829, 839, 848, 857,
     866, 875, 883, 891, 899, 906, 914, 921, 927, 934,
     940, 946, 951, 956, 961, 966, 970, 974, 978, 982,
     985, 988, 990, 993, 995, 996, 998, 999, 999, 1000,
    1000
};

int gfx_sin_deg(int deg)
{
    deg %= 360;
    if (deg < 0) deg += 360;
    if (deg <= 90)  return  sin_table[deg];
    if (deg <= 180) return  sin_table[180 - deg];
    if (deg <= 270) return -sin_table[deg - 180];
    return -sin_table[360 - deg];
}

int gfx_cos_deg(int deg) { return gfx_sin_deg(deg + 90); }

static void gfx_try_gpu_init(void)
{
    if (gfx_gpu_tried) return;
    gfx_gpu_tried = true;
    ixg_driver_init();
    if (ixg_driver_is_accel_ready()) {
        gfx_gpu_active = true;
    }
}

void gfx_gpu_fallback(void) { gfx_gpu_active = false; }
bool gfx_gpu_available(void) { return gfx_gpu_active; }

void gfx_init(void)
{
    fb       = (uint8_t*)(uintptr_t)framebuffer_addr;
    fb_w     = framebuffer_width;
    fb_h     = framebuffer_height;
    fb_pitch = framebuffer_pitch;

    if (fb_w > 1920) fb_w = 1920;
    if (fb_h > 1200) fb_h = 1200;

    backbuffer = (uint32_t*)malloc(fb_w * fb_h * sizeof(uint32_t));
    blurtmp    = (uint32_t*)malloc(fb_w * fb_h * sizeof(uint32_t));
    if (!backbuffer || !blurtmp) {
        fb_w = GUI_W;
        fb_h = GUI_H;
        backbuffer = (uint32_t*)malloc(fb_w * fb_h * sizeof(uint32_t));
        blurtmp    = (uint32_t*)malloc(fb_w * fb_h * sizeof(uint32_t));
    }
    if (!backbuffer || !blurtmp) {
        fb_w = 640;
        fb_h = 480;
        backbuffer = (uint32_t*)malloc(fb_w * fb_h * sizeof(uint32_t));
        blurtmp    = (uint32_t*)malloc(fb_w * fb_h * sizeof(uint32_t));
    }

    gfx_try_gpu_init();
}

uint32_t gfx_width(void)  { return fb_w; }
uint32_t gfx_height(void) { return fb_h; }
uint32_t gfx_fb_width(void)  { return fb_w; }
uint32_t gfx_fb_height(void) { return fb_h; }
uint32_t *gfx_backbuffer(void) { return backbuffer; }

void gfx_present(void)
{
    for (uint32_t y = 0; y < fb_h; y++) {
        uint32_t *dst = (uint32_t*)(fb + y * fb_pitch);
        uint32_t *src = &backbuffer[y * fb_w];
        for (uint32_t x = 0; x < fb_w; x++)
            dst[x] = src[x];
    }
    if (gfx_gpu_active) {
        ixg_driver_flush();
    }
}

void gfx_pixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return;
    backbuffer[y * fb_w + x] = color;
}

uint32_t gfx_get_pixel(int x, int y)
{
    if (x < 0 || y < 0 || x >= fb_w || y >= fb_h) return 0;
    return backbuffer[y * fb_w + x];
}

void gfx_clear_rgb(uint32_t color)
{
    uint32_t n = fb_w * fb_h;
    for (uint32_t i = 0; i < n; i++) backbuffer[i] = color;
}

uint32_t gfx_blend(uint32_t dst, uint32_t src)
{
    uint32_t a = (src >> 24) & 0xFF;
    if (a == 0)   return dst;
    if (a == 255) return src | 0xFF000000;

    uint32_t ia = 255 - a;
    uint32_t sr = (src >> 16) & 0xFF;
    uint32_t sg = (src >> 8)  & 0xFF;
    uint32_t sb = (src)       & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8)  & 0xFF;
    uint32_t db = (dst)       & 0xFF;
    uint32_t rr = (sr * a + dr * ia) / 255;
    uint32_t rg = (sg * a + dg * ia) / 255;
    uint32_t rb = (sb * a + db * ia) / 255;
    return 0xFF000000 | (rr << 16) | (rg << 8) | rb;
}

uint32_t gfx_lerp_color(uint32_t a, uint32_t b, int t)
{
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    int it = 256 - t;
    uint32_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    uint32_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    uint32_t rr = (ar * it + br * t) >> 8;
    uint32_t rg = (ag * it + bg * t) >> 8;
    uint32_t rb = (ab * it + bb * t) >> 8;
    return 0xFF000000 | (rr << 16) | (rg << 8) | rb;
}

void gfx_fill_rect(int x, int y, int w, int h, uint32_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    if (gfx_gpu_active && ixg_driver_is_accel_ready()) {
        ixg_driver_accel_rect(x, y, w, h, color);
    }
    for (int j = 0; j < h; j++) {
        uint32_t *row = &backbuffer[(y + j) * fb_w + x];
        for (int i = 0; i < w; i++) row[i] = color;
    }
}

void gfx_fill_rect_alpha(int x, int y, int w, int h, uint32_t argb)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    if (gfx_gpu_active && ixg_driver_is_accel_ready()) {
        if (ixg_driver_accel_rect_blend(x, y, w, h, argb, 0) == IXG_BIND_OK)
            return;
    }
    for (int j = 0; j < h; j++) {
        uint32_t *row = &backbuffer[(y + j) * fb_w + x];
        for (int i = 0; i < w; i++) row[i] = gfx_blend(row[i], argb);
    }
}

static int in_rounded(int px, int py, int w, int h, int r)
{
    if (px >= r && px < w - r) return 1;
    if (py >= r && py < h - r) return 1;
    int cx, cy;
    if (px < r)      cx = r;       else cx = w - 1 - r;
    if (py < r)      cy = r;       else cy = h - 1 - r;
    int dx = px - cx;
    int dy = py - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void gfx_rounded_rect(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (in_rounded(i, j, w, h, radius))
                gfx_pixel(x + i, y + j, color);
}

void gfx_rounded_rect_alpha(int x, int y, int w, int h, int radius, uint32_t argb)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (in_rounded(i, j, w, h, radius)) {
                int gx = x + i, gy = y + j;
                if (gx < 0 || gy < 0 || gx >= fb_w || gy >= fb_h) continue;
                uint32_t *p = &backbuffer[gy * LOG_W + gx];
                *p = gfx_blend(*p, argb);
            }
}

void gfx_hline(int x, int y, int w, uint32_t color)
{
    for (int i = 0; i < w; i++) gfx_pixel(x + i, y, color);
}

void gfx_vline(int x, int y, int h, uint32_t color)
{
    for (int j = 0; j < h; j++) gfx_pixel(x, y + j, color);
}

void gfx_rect_outline(int x, int y, int w, int h, uint32_t color)
{
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_rounded_outline(int x, int y, int w, int h, int radius, uint32_t color)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            int inside = in_rounded(i, j, w, h, radius);
            int inner  = (i >= 1 && j >= 1 && i < w - 1 && j < h - 1)
                         ? in_rounded(i, j, w, h, radius) &&
                           in_rounded(i - 1, j, w, h, radius) &&
                           in_rounded(i + 1, j, w, h, radius) &&
                           in_rounded(i, j - 1, w, h, radius) &&
                           in_rounded(i, j + 1, w, h, radius)
                         : 0;
            if (inside && !inner)
                gfx_pixel(x + i, y + j, color);
        }
}

void gfx_line(int x0, int y0, int x1, int y1, uint32_t color)
{
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int err = (adx > ady ? adx : -ady) / 2;
    while (1) {
        gfx_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err;
        if (e2 > -adx) { err -= ady; x0 += sx; }
        if (e2 <  ady) { err += adx; y0 += sy; }
    }
}

void gfx_vgradient(int x, int y, int w, int h, uint32_t top, uint32_t bottom)
{
    for (int j = 0; j < h; j++) {
        int t = (h > 1) ? (j * 256) / (h - 1) : 0;
        uint32_t c = gfx_lerp_color(top, bottom, t);
        gfx_hline(x, y + j, w, c);
    }
}

void gfx_blur_region(int x, int y, int w, int h, int passes)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    for (int p = 0; p < passes; p++) {
        for (int j = 0; j < h; j++) {
            for (int i = 0; i < w; i++) {
                int gx = x + i, gy = y + j;
                uint32_t cc = backbuffer[gy * LOG_W + gx];
                uint32_t l  = (i > 0)     ? backbuffer[gy * LOG_W + gx - 1] : cc;
                uint32_t r  = (i < w - 1) ? backbuffer[gy * LOG_W + gx + 1] : cc;
                uint32_t u  = (j > 0)     ? backbuffer[(gy - 1) * LOG_W + gx] : cc;
                uint32_t d  = (j < h - 1) ? backbuffer[(gy + 1) * LOG_W + gx] : cc;
                uint32_t rr = (((cc>>16)&0xFF)*2 + ((l>>16)&0xFF) + ((r>>16)&0xFF) + ((u>>16)&0xFF) + ((d>>16)&0xFF)) / 6;
                uint32_t rg = (((cc>>8)&0xFF)*2  + ((l>>8)&0xFF)  + ((r>>8)&0xFF)  + ((u>>8)&0xFF)  + ((d>>8)&0xFF))  / 6;
                uint32_t rb = (((cc)&0xFF)*2     + ((l)&0xFF)     + ((r)&0xFF)     + ((u)&0xFF)     + ((d)&0xFF))     / 6;
                blurtmp[gy * LOG_W + gx] = 0xFF000000 | (rr << 16) | (rg << 8) | rb;
            }
        }
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                int gx = x + i, gy = y + j;
                backbuffer[gy * LOG_W + gx] = blurtmp[gy * LOG_W + gx];
            }
    }
}

void gfx_glass_region(int x, int y, int w, int h, int radius, uint32_t tint_argb, int blur_passes)
{
    gfx_blur_region(x, y, w, h, blur_passes);
    gfx_rounded_rect_alpha(x, y, w, h, radius, tint_argb);
    gfx_rounded_outline(x, y, w, h, radius, 0x40FFFFFF);
}

void gfx_glass_highlight(int x, int y, int w, int h, int radius)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    if (radius < 1) radius = 1;
    int hl_h = h / 3;
    if (hl_h < 2) hl_h = 2;
    for (int j = 0; j < hl_h && j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (!in_rounded(i, j, w, h, radius)) continue;
            int gx = x + i, gy = y + j;
            if (gx < 0 || gy < 0 || gx >= fb_w || gy >= fb_h) continue;
            int alpha = 0x60 - (j * 0x60 / hl_h);
            if (alpha < 0) alpha = 0;
            uint32_t hl = ((uint32_t)alpha << 24) | 0x00FFFFFF;
            uint32_t *p = &backbuffer[gy * LOG_W + gx];
            *p = gfx_blend(*p, hl);
        }
    }
}

void gfx_glass_specular(int x, int y, int w, int h, int radius)
{
    if (radius * 2 > w) radius = w / 2;
    if (radius * 2 > h) radius = h / 2;
    if (radius < 1) radius = 1;
    int cx = w / 4;
    int cy = h / 4;
    int sr = (w < h ? w : h) / 6;
    if (sr < 2) sr = 2;
    for (int j = -sr; j <= sr; j++) {
        for (int i = -sr; i <= sr; i++) {
            int dist = i * i + j * j;
            if (dist > sr * sr) continue;
            int gx = x + cx + i, gy = y + cy + j;
            if (gx < 0 || gy < 0 || gx >= fb_w || gy >= fb_h) continue;
            if (!in_rounded(cx + i, cy + j, w, h, radius)) continue;
            int alpha = 0x90 - (dist * 0x90 / (sr * sr));
            if (alpha < 0) alpha = 0;
            uint32_t sp = ((uint32_t)alpha << 24) | 0x00FFFFFF;
            uint32_t *p = &backbuffer[gy * LOG_W + gx];
            *p = gfx_blend(*p, sp);
        }
    }
}

void gfx_glass_shadow(int x, int y, int w, int h, int radius, int blur_passes)
{
    int sh_off = 3;
    gfx_rounded_rect_alpha(x + sh_off, y + sh_off, w, h, radius, 0x30000000);
    gfx_blur_region(x + sh_off, y + sh_off, w, h, blur_passes);
}

void gfx_liquid_glass(int x, int y, int w, int h, int radius, uint32_t tint_argb, int blur_passes)
{
    gfx_glass_shadow(x, y, w, h, radius, blur_passes);
    gfx_blur_region(x, y, w, h, blur_passes);
    gfx_rounded_rect_alpha(x, y, w, h, radius, tint_argb);
    gfx_rounded_outline(x, y, w, h, radius, 0x60FFFFFF);
    gfx_glass_highlight(x, y, w, h, radius);
    gfx_glass_specular(x, y, w, h, radius);
    int edge_alpha = 0x30;
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (!in_rounded(i, j, w, h, radius)) continue;
            int gx = x + i, gy = y + j;
            if (gx < 0 || gy < 0 || gx >= fb_w || gy >= fb_h) continue;
            if (j >= h - 3) {
                int fade = (h - 1 - j) * edge_alpha / 3;
                uint32_t sh = ((uint32_t)fade << 24);
                uint32_t *p = &backbuffer[gy * LOG_W + gx];
                *p = gfx_blend(*p, sh);
            }
        }
    }
}

void gfx_draw_char_8x8(char c, int x, int y, uint32_t fg, uint32_t bg, bool bgon)
{
    const unsigned char *g = font8x8_basic[(uint8_t)c];
    for (int cy = 0; cy < 8; cy++) {
        uint8_t row = g[cy];
        for (int cx = 0; cx < 8; cx++) {
            if (row & (1 << cx)) gfx_pixel(x + cx, y + cy, fg);
            else if (bgon)       gfx_pixel(x + cx, y + cy, bg);
        }
    }
}

void gfx_draw_string_8x8(const char *s, int x, int y, uint32_t fg, uint32_t bg, bool bgon)
{
    while (*s) { gfx_draw_char_8x8(*s++, x, y, fg, bg, bgon); x += 8; }
}

void gfx_text(const char *s, int x, int y, uint32_t color)
{
    int ox = x;
    while (*s) {
        if (*s == '\n') { y += 9; x = ox; s++; continue; }
        gfx_draw_char_8x8(*s++, x, y, color, 0, false);
        x += 8;
    }
}

void gfx_text_scaled(const char *s, int x, int y, uint32_t color, int scale)
{
    int ox = x;
    while (*s) {
        if (*s == '\n') { y += 9 * scale; x = ox; s++; continue; }
        const unsigned char *g = font8x8_basic[(uint8_t)*s];
        for (int cy = 0; cy < 8; cy++) {
            uint8_t row = g[cy];
            for (int cx = 0; cx < 8; cx++)
                if (row & (1 << cx))
                    gfx_fill_rect(x + cx * scale, y + cy * scale, scale, scale, color);
        }
        x += 8 * scale;
        s++;
    }
}

int gfx_text_width(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n * 8;
}

void gfx_blit_argb(const uint32_t *src, int sw, int sh, int dx, int dy)
{
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sw; i++) {
            uint32_t c = src[j * sw + i];
            int gx = dx + i, gy = dy + j;
            if (gx < 0 || gy < 0 || gx >= fb_w || gy >= fb_h) continue;
            uint32_t *p = &backbuffer[gy * LOG_W + gx];
            *p = gfx_blend(*p, c);
        }
}

void gfx_blit_argb_scaled(const uint32_t *src, int sw, int sh, int dx, int dy, int dw, int dh)
{
    if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
    for (int j = 0; j < dh; j++) {
        int sy = j * sh / dh;
        for (int i = 0; i < dw; i++) {
            int sx = i * sw / dw;
            uint32_t c = src[sy * sw + sx];
            int gx = dx + i, gy = dy + j;
            if (gx < 0 || gy < 0 || gx >= fb_w || gy >= fb_h) continue;
            uint32_t *p = &backbuffer[gy * LOG_W + gx];
            *p = gfx_blend(*p, c);
        }
    }
}

void gfx_clear(uint32_t c)        { gfx_clear_rgb(c); }
void gfx_putpixel_raw(int x, int y, uint32_t c) { gfx_pixel(x, y, c); }

void gfx_rect(int x, int y, int w, int h, uint32_t c)
{
    gfx_fill_rect(x, y, w, h, c);
}

void gfx_circle(int cx, int cy, int r, uint32_t c)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x*x + y*y <= r*r)
                gfx_pixel(cx + x, cy + y, c);
}

void draw_rectangle(int x, int y, int w, int h, uint8_t color)
{
    (void)color;
    gfx_fill_rect(x, y, w, h, 0xFFAAAAAA);
}

void draw_circle(int x, int y, int radius, uint8_t color)
{
    (void)color;
    gfx_circle(x, y, radius, 0xFFAAAAAA);
}

void draw_vga_character(uint8_t c, int x, int y, int fg, int bg, bool bgon)
{
    int mask[8] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    unsigned char *glyph = (uint8_t*)vgafnt + (int)c * 16;
    for (int cy = 0; cy < 16; cy++)
        for (int cx = 0; cx < 8; cx++) {
            if (glyph[cy] & mask[cx]) gfx_pixel(x + cx, y + cy, fg);
            else if (bgon)            gfx_pixel(x + cx, y + cy, bg);
        }
}

void draw_text_string(const char *text, int x, int y, int fg, int bg, bool bgon)
{
    while (*text) { draw_vga_character(*text++, x, y, fg, bg, bgon); x += 8; }
}

void draw_vga_character_8x8(uint8_t c, int x, int y, int fg, int bg, bool bgon)
{
    for (int cy = 0; cy < 8; cy++) {
        uint8_t row = font8x8_basic[c][cy];
        for (int cx = 0; cx < 8; cx++) {
            if (row & (1 << cx)) gfx_pixel(x + cx, y + cy, fg);
            else if (bgon)       gfx_pixel(x + cx, y + cy, bg);
        }
    }
}

void draw_text_string_8x8(const char *text, int x, int y, int fg, int bg, bool bgon)
{
    while (*text) { draw_vga_character_8x8(*text++, x, y, fg, bg, bgon); x += 8; }
}