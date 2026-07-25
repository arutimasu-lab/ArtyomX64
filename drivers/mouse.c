#include <stdint.h>
#include "../kernel/isr.h"
#include "../lib/common.h"
#include "../lib/gfxlib.h"
#include "usb_hid_mouse.h"

volatile int mouse_dx = 0;
volatile int mouse_dy = 0;
volatile int mouse_buttons = 0;

int m_cursor_x = 320;
int m_cursor_y = 240;

static int mouse_present = 0;

static int mouse_wait_read(unsigned int timeout)
{
    while (timeout--) {
        if (inb(0x64) & 1) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static int mouse_wait_write(unsigned int timeout)
{
    while (timeout--) {
        if ((inb(0x64) & 2) == 0) return 1;
        __asm__ volatile("pause");
    }
    return 0;
}

static void mouse_write_wait(unsigned int timeout, uint8_t data)
{
    if (!mouse_wait_write(timeout)) return;
    outb(0x64, 0xD4);
    if (!mouse_wait_write(timeout)) return;
    outb(0x60, data);
}

static int mouse_read_wait(unsigned int timeout, uint8_t *out)
{
    if (!mouse_wait_read(timeout)) return 0;
    *out = inb(0x60);
    return 1;
}

void mouse_handler(registers_t *r)
{
    (void)r;
    static uint8_t cycle = 0;
    static int8_t packet[3];

    if (!(inb(0x64) & 1)) return;
    packet[cycle++] = (int8_t)inb(0x60);

    if (cycle == 3) {
        cycle = 0;
        mouse_dx += packet[1];
        mouse_dy -= packet[2];
        mouse_buttons = packet[0] & 0x07;
    }

    outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void mouse_install(void)
{
    unsigned int timeout = 80000;

    if (!mouse_wait_write(timeout)) return;
    outb(0x64, 0xA8);

    if (!mouse_wait_write(timeout)) return;
    outb(0x64, 0x20);
    {
        uint8_t status;
        if (!mouse_wait_read(timeout)) return;
        status = inb(0x60);
        status |= 2;

        if (!mouse_wait_write(timeout)) return;
        outb(0x64, 0x60);
        if (!mouse_wait_write(timeout)) return;
        outb(0x60, status);
    }

    uint8_t ack;
    mouse_write_wait(timeout, 0xF6);
    if (!mouse_read_wait(timeout, &ack) || ack != 0xFA) return;

    mouse_write_wait(timeout, 0xF4);
    if (!mouse_read_wait(timeout, &ack) || ack != 0xFA) return;

    mouse_present = 1;
    register_interrupt_handler(IRQ12, mouse_handler);
}

void handle_mouse(void)
{
    /* USB-мышь (если найдена и настроена в usb_init()) опрашивается всегда,
       независимо от того, есть ли PS/2-мышь — обе пишут в одни и те же
       mouse_dx/mouse_dy/mouse_buttons. */
    //usb_hid_mouse_poll();

    if (!mouse_present && mouse_dx == 0 && mouse_dy == 0) return;

    if (mouse_dx != 0 || mouse_dy != 0) {
        m_cursor_x += mouse_dx;
        m_cursor_y += mouse_dy;

        if (m_cursor_x < 0) m_cursor_x = 0;
        if (m_cursor_y < 0) m_cursor_y = 0;
        if (m_cursor_x >= gfx_width()) m_cursor_x = 639;
        if (m_cursor_y >= gfx_height()) m_cursor_y = 479;

        mouse_dx = 0;
        mouse_dy = 0;
    }
}
