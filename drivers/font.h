// font.h - простой 8x8 растровый шрифт
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// Ширина и высота символа
#define FONT_WIDTH 8
#define FONT_HEIGHT 8

// Получить данные символа (возвращает 8 байт, каждый бит = пиксель)
const uint8_t* font_get_char(char c);

#endif