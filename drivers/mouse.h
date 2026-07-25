#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

#define byte unsigned char
#define sbyte signed char
#define dword unsigned int

void mouse_install(void);
void handle_mouse(void);

#endif