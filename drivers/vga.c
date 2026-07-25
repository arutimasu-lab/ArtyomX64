// vga.c
#include "vga.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
//#include <string.h>
#include "../lib/common.h"
#include "../lib/ipc.h"

// Порты VGA
#define VGA_CTRL_REG 0x3D4
#define VGA_DATA_REG 0x3D5

// Регистры VGA
#define VGA_REG_MISC_WRITE 0x3C2
#define VGA_REG_MISC_READ 0x3CC
#define VGA_REG_SEQ_INDEX 0x3C4
#define VGA_REG_SEQ_DATA 0x3C5
#define VGA_REG_CRTC_INDEX 0x3D4
#define VGA_REG_CRTC_DATA 0x3D5
#define VGA_REG_GC_INDEX 0x3CE
#define VGA_REG_GC_DATA 0x3CF
#define VGA_REG_AC_INDEX 0x3C0
#define VGA_REG_AC_DATA 0x3C1
#define VGA_REG_AC_RESET 0x3DA


//IPC
int gfx_ipc(ipc_msg_t *m) {
    switch (m->type) {
        case GFX_INIT:
            vga_init();
            break;
        case GFX_CLEAR:
            vga_clear(m->arg0);
            break;
        case GFX_SET_PIXEL:
            vga_set_pixel(m->arg0, m->arg1, m->arg2);
            break;
    }
    return 0;
}

/*


// Вспомогательные функции для работы с портами
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
*/
// Установка режима 320x200 256 цветов
void vga_init() {
    outb(VGA_REG_MISC_WRITE, 0x63);

    outb(VGA_REG_SEQ_INDEX, 0x00);
    outb(VGA_REG_SEQ_DATA, 0x03);

    outb(VGA_REG_SEQ_INDEX, 0x01);
    outb(VGA_REG_SEQ_DATA, 0x01);

    outb(VGA_REG_SEQ_INDEX, 0x02);
    outb(VGA_REG_SEQ_DATA, 0x0F);

    outb(VGA_REG_SEQ_INDEX, 0x03);
    outb(VGA_REG_SEQ_DATA, 0x00);

    outb(VGA_REG_SEQ_INDEX, 0x04);
    outb(VGA_REG_SEQ_DATA, 0x06);

    outb(VGA_REG_GC_INDEX, 0x00);
    outb(VGA_REG_GC_DATA, 0x00);

    outb(VGA_REG_GC_INDEX, 0x01);
    outb(VGA_REG_GC_DATA, 0x00);

    outb(VGA_REG_GC_INDEX, 0x02);
    outb(VGA_REG_GC_DATA, 0x00);

    outb(VGA_REG_GC_INDEX, 0x03);
    outb(VGA_REG_GC_DATA, 0x00);

    outb(VGA_REG_GC_INDEX, 0x04);
    outb(VGA_REG_GC_DATA, 0x00);

    outb(VGA_REG_GC_INDEX, 0x05);
    outb(VGA_REG_GC_DATA, 0x40);

    outb(VGA_REG_GC_INDEX, 0x06);
    outb(VGA_REG_GC_DATA, 0x05);

    outb(VGA_REG_GC_INDEX, 0x07);
    outb(VGA_REG_GC_DATA, 0x0F);

    outb(VGA_REG_GC_INDEX, 0x08);
    outb(VGA_REG_GC_DATA, 0xFF);

    for (uint8_t i = 0; i < 0x10; i++) {
        inb(VGA_REG_AC_RESET);
        outb(VGA_REG_AC_INDEX, i);
        outb(VGA_REG_AC_DATA, i);
    }

    inb(VGA_REG_AC_RESET);
    outb(VGA_REG_AC_INDEX, 0x10);
    outb(VGA_REG_AC_DATA, 0x0C);

    inb(VGA_REG_AC_RESET);
    outb(VGA_REG_AC_INDEX, 0x20);
}

void vga_set_pixel(uint16_t x, uint16_t y, uint8_t color) {
    if (x >= VGA_WIDTH || y >= VGA_HEIGHT) return;
    ((uint8_t*)VGA_BUFFER)[y * VGA_WIDTH + x] = color;
}
// Получение цвета пикселя
uint8_t vga_get_pixel(uint16_t x, uint16_t y) {
    if (x >= VGA_WIDTH  || y >= VGA_HEIGHT) return 0;
    
    uint8_t *vga = (uint8_t*)VGA_BUFFER;
    return vga[y * VGA_WIDTH + x];
}

// Очистка экрана
void vga_clear(uint8_t color) {
    uint8_t *vga = (uint8_t*)VGA_BUFFER;
    for (uint32_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = color;
    }
}
/*

int strlen(char *src)
{
    int i = 0;
    while (*src++)
        i++;
    return i;
}
*/