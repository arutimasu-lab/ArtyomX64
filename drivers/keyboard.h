#ifndef KEYBOARD_H
#define KEYBOARD_H
#include "../lib/common.h"

// Текущие объявления
void init_keyboard();
char keyboard_read();
int is_buffer_empty(void);
extern int is_enter_pressed;

// Добавьте эти функции для взаимодействия с syscall
int keyboard_buffer_available();
void keyboard_clear_buffer();
void keyboard_wait_for_enter();

#endif