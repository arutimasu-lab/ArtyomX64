#include "../lib/libax.h"
#include "../lib/unistd.h"
typedef unsigned long long  u64int;
typedef          long long  s64int;
typedef unsigned int        u32int;
typedef          int        s32int;
typedef unsigned short      u16int;
typedef          short      s16int;
typedef unsigned char       u8int;
typedef          char       s8int;

void outb(u16int port, u8int value)
{
    __asm__ volatile ("outb %1, %0" : : "dN" (port), "a" (value));
}

#define CW 360
#define CH 280

static char buffer[8192];
static int  blen = 0;

static void render(void)
{
    ax_fill(AX_ARGB(0xF0,28,28,34));
    ax_rect(0, 0, CW, 22, AX_RGB(40,40,50));
    ax_text("Notes", 10, 7, AX_RGB(255,255,255));

    int x = 10, y = 32;
    for (int i = 0; i < blen; i++) {
        char c = buffer[i];
        if (c == '\n') { x = 10; y += 10; continue; }
        ax_char(c, x, y, AX_RGB(230,230,230));
        x += 8;
        if (x > CW - 16) { x = 10; y += 10; }
    }
    ax_rect(x, y, 2, 8, AX_RGB(120,180,255));
}

void _start(void)
{
     // <-- буквально первая инструкция функции
    g_canvas = ax_surface("Notes", CW, CH);
    ax_canvas_dims(CW, CH);
    if (!g_canvas) { for(;;); }

    render();
    ax_commit(g_canvas);

    ax_event ev;
    for (;;) {
        while (ax_poll(g_canvas, &ev)>0) {
            if (ev.type == AX_EV_CLOSE) return;
            if (ev.type == AX_EV_KEY) {
                if (ev.key == '\b') { if (blen > 0) blen--; }
                else if (blen < (int)sizeof(buffer) - 1) buffer[blen++] = ev.key;
                render();
                ax_commit(g_canvas);
            }
        }
        //__asm__ volatile("pause");
        yield();
    }
}
