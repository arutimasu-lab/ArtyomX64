#include "../lib/libax.h"

#define CW 300
#define CH 220

static int rx, ry, opn;
static long acc;
static char disp[24];
static int dlen;

static void putnum(long n)
{
    dlen = 0;
    if (n == 0) { disp[dlen++] = '0'; disp[dlen] = 0; return; }
    char tmp[24]; int t = 0;
    int neg = n < 0; if (neg) n = -n;
    while (n) { tmp[t++] = '0' + n % 10; n /= 10; }
    if (neg) disp[dlen++] = '-';
    while (t) disp[dlen++] = tmp[--t];
    disp[dlen] = 0;
}

static const char *keys[4][4] = {
    {"7","8","9","/"},
    {"4","5","6","*"},
    {"1","2","3","-"},
    {"0","C","=","+"},
};

static void render(void)
{
    ax_fill(AX_ARGB(0xF0,24,24,30));
    ax_rect(0, 0, CW, 22, AX_RGB(40,40,50));
    ax_text("Calculator", 10, 7, AX_RGB(255,255,255));

    ax_rect(14, 30, CW - 28, 36, AX_RGB(18,18,24));
    int tw = dlen * 8;
    ax_text(disp, CW - 24 - tw, 44, AX_RGB(120,255,160));

    int bw = (CW - 28) / 4;
    int bh = 32;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++) {
            int bx = 14 + c * bw;
            int by = 76 + r * (bh + 6);
            ax_rect(bx, by, bw - 6, bh, AX_RGB(50,50,62));
            ax_text(keys[r][c], bx + bw/2 - 4, by + 12, AX_RGB(230,230,240));
        }
}

static void press(const char *k)
{
    char c = k[0];
    if (c >= '0' && c <= '9') {
        rx = rx * 10 + (c - '0');
        putnum(rx);
    } else if (c == 'C') {
        rx = 0; acc = 0; opn = 0; putnum(0);
    } else if (c == '=') {
        long res = acc;
        switch (opn) {
            case '+': res = acc + rx; break;
            case '-': res = acc - rx; break;
            case '*': res = acc * rx; break;
            case '/': res = rx ? acc / rx : 0; break;
            default: res = rx; break;
        }
        putnum(res); acc = res; rx = 0; opn = 0;
    } else {
        if (opn) {
            switch (opn) {
                case '+': acc += rx; break;
                case '-': acc -= rx; break;
                case '*': acc *= rx; break;
                case '/': acc = rx ? acc / rx : 0; break;
            }
        } else acc = rx;
        putnum(acc);
        rx = 0; opn = c;
    }
}

void _start(void)
{
    g_canvas = ax_surface("Calculator", CW, CH);
    ax_canvas_dims(CW, CH);
    if (!g_canvas) { for(;;); }
    putnum(0);
    render();
    ax_commit(g_canvas);

    ax_event ev;
    for (;;) {
        while (ax_poll(g_canvas, &ev)) {
            if (ev.type == AX_EV_CLOSE) return;
            if (ev.type == AX_EV_MOUSE) {
                int bw = (CW - 28) / 4, bh = 32;
                for (int r = 0; r < 4; r++)
                    for (int c = 0; c < 4; c++) {
                        int bx = 14 + c * bw, by = 76 + r * (bh + 6);
                        if (ev.mx >= bx && ev.mx < bx + bw - 6 &&
                            ev.my >= by && ev.my < by + bh) {
                            press(keys[r][c]);
                        }
                    }
                render();
                ax_commit(g_canvas);
            }
        }
        __asm__ volatile("pause");
    }
    (void)rx; (void)ry;
}
