#include "../lib/libax.h"
#include "../lib/unistd.h"
#define CW 480
#define CH 300
#define MAX_LINES 28
#define LINE_LEN 72

static char scrollback[MAX_LINES][LINE_LEN];
static int  sb_count = 0;
static char cmdline[LINE_LEN];
static int  cl_len = 0;

static void push_line(const char *s)
{
    if (sb_count >= MAX_LINES) {
        for (int i = 1; i < MAX_LINES; i++)
            for (int j = 0; j < LINE_LEN; j++)
                scrollback[i-1][j] = scrollback[i][j];
        sb_count = MAX_LINES - 1;
    }
    int j = 0;
    while (s[j] && j < LINE_LEN - 1) { scrollback[sb_count][j] = s[j]; j++; }
    scrollback[sb_count][j] = 0;
    sb_count++;
}

static int streq(const char *a, const char *b)
{
    while (*a && *b) { if (*a != *b) return 0; a++; b++; }
    return *a == *b;
}

static void itoa_simple(int v, char *out)
{
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    char tmp[12]; int t = 0;
    int neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    while (v) { tmp[t++] = '0' + v % 10; v /= 10; }
    int p = 0;
    if (neg) out[p++] = '-';
    while (t) out[p++] = tmp[--t];
    out[p] = 0;
}

// Парсинг аргументов: разбивает cmd на argv, возвращает argc
static int parse_args(char *cmd, char **argv, int max_args)
{
    int argc = 0;
    char *p = cmd;
    while (*p == ' ') p++;
    while (*p && argc < max_args) {
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) { *p = 0; p++; }
        while (*p == ' ') p++;
    }
    return argc;
}

static void run_cmd(char *cmd)
{
    if (cmd[0] == 0) return;
    
    char *argv[8];
    int argc = parse_args(cmd, argv, 8);
    if (argc == 0) return;
    
    if (streq(argv[0], "help")) {
        push_line("cmds: help ver clear time echo");
    } else if (streq(argv[0], "ver")) {
        push_line("ArtyomXOS 1.0 x86_64 AXShell");
    } else if (streq(argv[0], "clear")) {
        sb_count = 0;
    } else if (streq(argv[0], "time")) {
        ax_time_t t; ax_time(&t);
        char buf[32];
        buf[0] = '0' + t.hour / 10; buf[1] = '0' + t.hour % 10; buf[2] = ':';
        buf[3] = '0' + t.minute / 10; buf[4] = '0' + t.minute % 10; buf[5] = ':';
        buf[6] = '0' + t.second / 10; buf[7] = '0' + t.second % 10; buf[8] = 0;
        push_line(buf);
    } else if (streq(argv[0], "echo")) {
        // Вывод всех аргументов через пробел
        char buf[LINE_LEN];
        int p = 0;
        for (int i = 1; i < argc; i++) {
            if (i > 1 && p < LINE_LEN - 1) buf[p++] = ' ';
            for (int j = 0; argv[i][j] && p < LINE_LEN - 1; j++)
                buf[p++] = argv[i][j];
        }
        buf[p] = 0;
        push_line(buf);
    } else {
        push_line("xsh: command not found");
    }
}

static void render(void)
{
    ax_fill(AX_ARGB(0xF2,10,12,16));
    ax_rect(0, 0, CW, 22, AX_RGB(28,30,38));
    ax_text("XSH Shell", 10, 7, AX_RGB(100,200,255));

    int y = 30;
    for (int i = 0; i < sb_count && y < CH - 20; i++) {
        ax_text(scrollback[i], 10, y, AX_RGB(200,220,200));
        y += 10;
    }
    ax_text("$ ", 10, y, AX_RGB(100,200,255));
    ax_text(cmdline, 26, y, AX_RGB(220,240,220));
    ax_rect(26 + cl_len * 8, y, 6, 8, AX_RGB(100,200,255));
}

void _start(void)
{
    g_canvas = ax_surface("XSH", CW, CH);
    ax_canvas_dims(CW, CH);
    if (!g_canvas) { for(;;); }

    push_line("XSH 0.0.3 - type 'help'");
    render();
    ax_commit(g_canvas);

    ax_event ev;
    for (;;) {
        while (ax_poll(g_canvas, &ev)>0) {
            if (ev.type == AX_EV_CLOSE) return;
            if (ev.type == AX_EV_KEY) {
                if (ev.key == '\n') {
                    cmdline[cl_len] = 0;
                    char echo[LINE_LEN];
                    echo[0] = '$'; echo[1] = ' ';
                    int p = 2;
                    for (int i = 0; i < cl_len && p < LINE_LEN - 1; i++)
                        echo[p++] = cmdline[i];
                    echo[p] = 0;
                    push_line(echo);
                    run_cmd(cmdline);
                    cl_len = 0;
                    cmdline[0] = 0;
                } else if (ev.key == '\b') {
                    if (cl_len > 0) cl_len--;
                    cmdline[cl_len] = 0;
                } else if (cl_len < LINE_LEN - 1) {
                    cmdline[cl_len++] = (char)ev.key;
                    cmdline[cl_len] = 0;
                }
                render();
                ax_commit(g_canvas);
            }
        }
        yield();
    }
}