#include <stddef.h>
#include "common.h"
#include "../drivers/monitor.h"

void outb(u16int port, u8int value)
{
    __asm__ volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

u8int inb(u16int port)
{
    u8int ret;
    __asm__ volatile("inb %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

u16int inw(u16int port)
{
    u16int ret;
    __asm__ volatile ("inw %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

void outw(u16int port, u16int value)
{
    __asm__ volatile ("outw %1, %0" : : "dN" (port), "a" (value));
}

void outl(u16int port, u32int value)
{
    __asm__ volatile ("outl %1, %0" : : "dN" (port), "a" (value));
}

u32int inl(u16int port)
{
    u32int ret;
    __asm__ volatile ("inl %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

extern void panic(const char *message, const char *file, u32int line)
{
    __asm__ volatile("cli");

    monitor_write("PANIC(");
    monitor_write(message);
    monitor_write(") at ");
    monitor_write(file);
    monitor_write(":");
    monitor_write_dec(line);
    monitor_write("\n");
    for(;;);
}

extern void panic_assert(const char *file, u32int line, const char *desc)
{
    __asm__ volatile("cli");

    monitor_write("ASSERTION-FAILED(");
    monitor_write(desc);
    monitor_write(") at ");
    monitor_write(file);
    monitor_write(":");
    monitor_write_dec(line);
    monitor_write("\n");
    for(;;);
}

void *memmove(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = (unsigned char*)dst;
    const unsigned char *s = (const unsigned char*)src;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (unsigned long i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (unsigned long i = n; i > 0; i--) d[i - 1] = s[i - 1];
    }
    return dst;
}
