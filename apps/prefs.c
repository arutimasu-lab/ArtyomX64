#include "../lib/libax.h"

#define CW 380
#define CH 240

static void render(void)
{
    ax_fill(AX_ARGB(0xF0,28,28,38));
    ax_rect(0, 0, CW, 22, AX_RGB(40,40,52));
    ax_text("System Settings", 10, 7, AX_RGB(255,255,255));

    ax_text("ArtyomXOS", 20, 36, AX_RGB(255,255,255));
    ax_text("Version 1.0  -  AXShell", 20, 52, AX_RGB(150,160,200));

    ax_text("Display:", 20, 80, AX_RGB(220,220,230));
    ax_screen_t sc; ax_screen(&sc);
    char res[32]; int p = 0;
    int w = sc.width, h = sc.height;
    char tw[8]; int t = 0;
    if (w == 0) w = 1;
    while (w) { tw[t++] = '0' + w % 10; w /= 10; }
    while (t) res[p++] = tw[--t];
    res[p++] = 'x';
    t = 0; if (h == 0) h = 1;
    while (h) { tw[t++] = '0' + h % 10; h /= 10; }
    while (t) res[p++] = tw[--t];
    res[p] = 0;
    ax_text(res, 100, 80, AX_RGB(120,200,255));

    ax_text("Time:", 20, 104, AX_RGB(220,220,230));
    ax_time_t tm; ax_time(&tm);
    char clk[16];
    clk[0]='0'+tm.hour/10; clk[1]='0'+tm.hour%10; clk[2]=':';
    clk[3]='0'+tm.minute/10; clk[4]='0'+tm.minute%10; clk[5]=':';
    clk[6]='0'+tm.second/10; clk[7]='0'+tm.second%10; clk[8]=0;
    ax_text(clk, 100, 104, AX_RGB(120,255,160));

    ax_text("Accent:", 20, 132, AX_RGB(220,220,230));
    uint32_t accents[] = {
        AX_RGB(10,132,255), AX_RGB(255,69,58), AX_RGB(48,209,88),
        AX_RGB(255,159,10), AX_RGB(191,90,242)
    };
    for (int i = 0; i < 5; i++)
        ax_rect(100 + i * 36, 130, 28, 18, accents[i]);

    ax_text("ArtyomX project (c) 2026", 20, CH - 24, AX_RGB(120,120,140));
}

void _start(void)
{
    g_canvas = ax_surface("System Settings", CW, CH);
    ax_canvas_dims(CW, CH);
    if (!g_canvas) { for(;;); }

    ax_event ev;
    int tick = 0;
    for (;;) {
        while (ax_poll(g_canvas, &ev))
            if (ev.type == AX_EV_CLOSE) return;
        if ((tick++ & 0x3FFFF) == 0) {
            render();
            ax_commit(g_canvas);
        }
        __asm__ volatile("pause");
    }
}
