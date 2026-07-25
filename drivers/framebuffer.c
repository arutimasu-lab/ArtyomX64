#include "framebuffer.h"
#include "../boot/multiboot.h"

extern uint64_t framebuffer_addr;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;
extern uint32_t framebuffer_bpp;

static uint32_t *framebuffer = NULL;
static uint32_t width = 0;
static uint32_t height = 0;
static uint32_t pitch = 0;
static uint32_t bpp = 0;

static const uint8_t simple_font[128][8] = {
    ['A'] = {0x18, 0x3C, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
    ['B'] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C, 0x00},
    ['C'] = {0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C, 0x00},
    ['0'] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
    ['1'] = {0x18, 0x18, 0x38, 0x18, 0x18, 0x18, 0x7E, 0x00},
    ['2'] = {0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00},
    [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    ['!'] = {0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00},
};

void fb_init(uint32_t *mboot_ptr) {
    framebuffer = (uint32_t*)framebuffer_addr;
    width = framebuffer_width;
    height = framebuffer_height;
    pitch = framebuffer_pitch;
    bpp = framebuffer_bpp;

    if (framebuffer && framebuffer != (uint32_t*)0xB8000) {
        fb_draw_rect(10, 10, 50, 50, COLOR_RED);
    }
}

int fb_is_available(void) {
    return framebuffer != NULL && framebuffer != (uint32_t*)0xB8000;
}

uint32_t* fb_get_pixel_ptr(uint32_t x, uint32_t y) {
    if (!framebuffer || x >= width || y >= height) {
        return NULL;
    }

    uint64_t offset = (uint64_t)y * pitch + (uint64_t)x * (bpp / 8);
    return (uint32_t*)((uint8_t*)framebuffer + offset);
}

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    uint32_t *pixel = fb_get_pixel_ptr(x, y);
    if (pixel) {
        *pixel = color;
    }
}

void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t dy = 0; dy < h; dy++) {
        for (uint32_t dx = 0; dx < w; dx++) {
            fb_put_pixel(x + dx, y + dy, color);
        }
    }
}

void fb_clear(uint32_t color) {
    if (!framebuffer) return;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            fb_put_pixel(x, y, color);
        }
    }
}

void fb_put_char(char c, uint32_t x, uint32_t y, uint32_t color) {
    if (c < 0 || c >= 128) return;

    const uint8_t *char_data = simple_font[(int)c];

    for (uint32_t dy = 0; dy < 8; dy++) {
        uint8_t row = char_data[dy];
        for (uint32_t dx = 0; dx < 8; dx++) {
            if (row & (1 << (7 - dx))) {
                fb_put_pixel(x + dx, y + dy, color);
            }
        }
    }
}

void fb_put_string(const char *str, uint32_t x, uint32_t y, uint32_t color) {
    uint32_t current_x = x;

    while (*str) {
        if (*str == '\n') {
            y += 10;
            current_x = x;
        } else {
            fb_put_char(*str, current_x, y, color);
            current_x += 9;
        }
        str++;
    }
}
