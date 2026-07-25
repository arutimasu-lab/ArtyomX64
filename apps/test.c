#include "../lib/axipc.h"
#include "../lib/libax.h"
#include "../lib/unistd.h"
#include <stdint.h>
#include <stdbool.h>

void _start(void)
{
    ax_screen_t sc;
    ax_screen(&sc);
    int w = 320, h = 200;

    uint32_t *cb = ax_surface("Test App", w, h);
    if (!cb) { for (;;) __asm__ volatile("hlt"); }

    for (int i = 0; i < w * h; i++)
        cb[i] = AX_ARGB(0xFF, 0x1E, 0x1E, 0x2E);

    const char *msgs[] = {
        "Hello from Ring 3!",
        "ArtyomX ELF App",
        "Press ESC to close"
    };
    uint32_t colors[] = {
        AX_ARGB(0xFF, 0xCD, 0xD6, 0xF4),
        AX_ARGB(0xFF, 0x89, 0xB4, 0xFA),
        AX_ARGB(0xFF, 0xA6, 0xAD, 0xC8)
    };

    for (int m = 0; m < 3; m++) {
        for (int i = 0; msgs[m][i]; i++) {
            const unsigned char *g = font8x8_basic[(unsigned char)msgs[m][i]];
            for (int cy = 0; cy < 8; cy++) {
                uint8_t row = g[cy];
                for (int cx = 0; cx < 8; cx++)
                    if (row & (1 << cx)) {
                        int px = 24 + i * 8 + cx;
                        int py = 40 + m * 22 + cy;
                        if (px < w && py < h)
                            cb[py * w + px] = colors[m];
                    }
            }
        }
    }

    int anim = 0;
    while (1) {
        for (int x = 0; x < w; x++) {
            int dist = anim + x;
            if (dist >= w) dist -= w;
            int r = (dist < w / 2) ? (w / 2 - dist) : (dist - w / 2);
            int cr = 0x1E + (r * 48 / (w / 2));
            if (cr > 0x4E) cr = 0x4E;
            uint32_t col = 0xFF000000 | (cr << 16) | 0x2E2E;
            for (int dy = 0; dy < 3; dy++)
                if (h - 4 + dy < h)
                    cb[(h - 4 + dy) * w + x] = col;
        }
        anim = (anim + 2) % w;

        ax_event ev;
        while (ax_poll(cb, &ev)>0) {
            if (ev.type == AX_EV_KEY && ev.key == 27) return;
            if (ev.type == AX_EV_CLOSE) return;
        }
        //__asm__ volatile("hlt");
         yield();
    }
}
