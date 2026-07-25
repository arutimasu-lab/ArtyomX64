// vga.h
#ifndef VGA_H
#define VGA_H

#include <stdint.h>

// VGA режим 320x200, 256 цветов
#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_BUFFER 0xA0000

// Цвета (палитра VGA)
enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GRAY = 7,
    VGA_COLOR_DARK_GRAY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_YELLOW = 14,
    VGA_COLOR_WHITE = 15,
};

// Инициализация VGA режима
void vga_init();

// Установка пикселя
void vga_set_pixel(uint16_t x, uint16_t y, uint8_t color);

// Получение цвета пикселя
uint8_t vga_get_pixel(uint16_t x, uint16_t y);

// Очистка экрана
void vga_clear(uint8_t color);

#endif
