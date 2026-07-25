#include "../lib/libax.h"

#define CW 460
#define CH 300

static const char *files[] = {
    "Documents/", "Pictures/", "Downloads/", "readme.txt", "hello.bin", "xsh.bin", "notes.bin", "calc.bin"
};
#define NFILES 8

static int sel = -1;

static void render(void)
{
    ax_fill(AX_ARGB(0xF0,30,30,36));
    ax_rect(0, 0, CW, 22, AX_RGB(40,40,50));
    ax_text("Files", 10, 7, AX_RGB(255,255,255));

    ax_rect(0, 22, 130, CH - 22, AX_RGB(36,36,44));
    ax_text("Favorites", 12, 32, AX_RGB(150,150,160));
    ax_text("Home", 12, 50, AX_RGB(90,160,250));
    ax_text("Desktop", 12, 66, AX_RGB(200,200,210));
    ax_text("Trash", 12, 82, AX_RGB(200,200,210));

    int gx = 150, gy = 34;
    for (int i = 0; i < NFILES; i++) {
        int col = i % 3, row = i / 3;
        int ix = gx + col * 100;
        int iy = gy + row * 84;
        if (i == sel) ax_rect(ix - 6, iy - 6, 92, 78, AX_RGB(50,80,140));
        int isdir = files[i][0] >= 'A' && files[i][0] <= 'Z' &&
                    files[i][3] != '.' ;
        uint32_t col_ic = isdir ? AX_RGB(90,160,250) : AX_RGB(200,200,210);
        ax_rect(ix + 14, iy, 48, 40, col_ic);
        ax_rect(ix + 18, iy + 4, 16, 6, AX_RGB(230,240,255));
        ax_text(files[i], ix, iy + 48, AX_RGB(220,220,230));
    }
}

void _start(void)
{
    g_canvas = ax_surface("Files", CW, CH);
    ax_canvas_dims(CW, CH);
    if (!g_canvas) { for(;;); }
    render();
    ax_commit(g_canvas);

    ax_event ev;
    for (;;) {
        while (ax_poll(g_canvas, &ev)) {
            if (ev.type == AX_EV_CLOSE) return;
            if (ev.type == AX_EV_MOUSE) {
                int gx = 150, gy = 34;
                sel = -1;
                for (int i = 0; i < NFILES; i++) {
                    int col = i % 3, row = i / 3;
                    int ix = gx + col * 100, iy = gy + row * 84;
                    if (ev.mx >= ix - 6 && ev.mx < ix + 86 &&
                        ev.my >= iy - 6 && ev.my < iy + 72) sel = i;
                }
                render();
                ax_commit(g_canvas);
            }
        }
        __asm__ volatile("pause");
    }
}
