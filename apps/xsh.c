#include "../lib/libax.h"
#include "../lib/unistd.h"
#include <stdbool.h>

#define CW 640
#define CH 360
#define MAX_LINES 128
#define LINE_LEN 80
#define INPUT_LEN 128

static char lines[MAX_LINES][LINE_LEN];
static int line_count;
static char input[INPUT_LEN];
static int input_len;
static uint32_t *canvas;

static int raw_read(int fd, void *buf, unsigned long count)
{
    int ret;
    __asm__ volatile("int $0x80"
        : "=a"(ret)
        : "a"(3), "D"((long)fd), "S"((long)buf), "d"((long)count)
        : "memory");
    return ret;
}

static int xstrcmp(const char *a, const char *b) { while (*a && *b && *a == *b) { a++; b++; } return (unsigned char)*a - (unsigned char)*b; }

static void copy_line(char *dst, const char *src)
{
    int i = 0;
    while (src && src[i] && i < LINE_LEN - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void push_line(const char *s)
{
    if (line_count >= MAX_LINES) {
        for (int i = 1; i < MAX_LINES; i++) copy_line(lines[i - 1], lines[i]);
        line_count = MAX_LINES - 1;
    }
    copy_line(lines[line_count++], s ? s : "");
}

static void push_prefixed(const char *a, const char *b)
{
    char out[LINE_LEN];
    int p = 0;
    while (a && *a && p < LINE_LEN - 1) out[p++] = *a++;
    while (b && *b && p < LINE_LEN - 1) out[p++] = *b++;
    out[p] = 0;
    push_line(out);
}

static void split(char *cmd, char **arg)
{
    while (*cmd == ' ') cmd++;
    *arg = cmd;
    while (**arg && **arg != ' ') (*arg)++;
    if (**arg) { **arg = 0; (*arg)++; }
    while (**arg == ' ') (*arg)++;
}

static void cmd_help(void)
{
    push_line("XSH commands:");
    push_line(" help  ver  clear  time  echo  ls  cat  exec  exit");
}

static void cmd_time(void)
{
    ax_time_t t; ax_time(&t);
    char b[16];
    b[0] = '0' + t.hour / 10; b[1] = '0' + t.hour % 10; b[2] = ':';
    b[3] = '0' + t.minute / 10; b[4] = '0' + t.minute % 10; b[5] = ':';
    b[6] = '0' + t.second / 10; b[7] = '0' + t.second % 10; b[8] = 0;
    push_line(b);
}

static void cmd_ls(void)
{
    int fd = open("/", 0, 0);
    if (fd < 0) { push_line("xsh: cannot open /"); return; }
    static char dbuf[4096];
    int n = getdents(fd, dbuf, sizeof(dbuf));
    if (n <= 0) { push_line("(empty)"); return; }
    char *p = dbuf;
    while (n > 0) {
        int reclen = ((unsigned char)p[4]) | ((unsigned char)p[5] << 8);
        push_line(p + 7);
        if (reclen <= 0) break;
        p += reclen;
        n -= reclen;
    }
}

static void cmd_cat(const char *name)
{
    if (!name || !*name) { push_line("usage: cat <file>"); return; }
    int fd = open((void*)name, 0, 0);
    if (fd < 0) { push_prefixed("xsh: not found: ", name); return; }
    static char buf[1024];
    int n = raw_read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { push_line("(empty)"); return; }
    buf[n] = 0;
    char line[LINE_LEN]; int p = 0;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n' || p == LINE_LEN - 1) { line[p] = 0; push_line(line); p = 0; }
        else if (buf[i] != '\r') line[p++] = buf[i];
    }
    if (p) { line[p] = 0; push_line(line); }
}

static bool run_cmd(char *raw)
{
    char *arg;
    split(raw, &arg);
    if (!*raw) return true;
    if (xstrcmp(raw, "help") == 0) cmd_help();
    else if (xstrcmp(raw, "ver") == 0) push_line("ArtyomXOS x86_64 - XSH 0.1");
    else if (xstrcmp(raw, "clear") == 0) line_count = 0;
    else if (xstrcmp(raw, "time") == 0) cmd_time();
    else if (xstrcmp(raw, "echo") == 0) push_line(arg);
    else if (xstrcmp(raw, "ls") == 0) cmd_ls();
    else if (xstrcmp(raw, "cat") == 0) cmd_cat(arg);
    else if (xstrcmp(raw, "exec") == 0) { if (*arg) exec(arg); else push_line("usage: exec <file>"); }
    else if (xstrcmp(raw, "exit") == 0) return false;
    else push_prefixed("xsh: command not found: ", raw);
    return true;
}

static void render(void)
{
    ax_fill(AX_RGB(8, 10, 14));
    ax_rect(0, 0, CW, 22, AX_RGB(24, 28, 36));
    ax_text("XSH pseudo-terminal", 10, 7, AX_RGB(100, 200, 255));
    int rows = (CH - 40) / 10;
    int start = line_count - rows;
    if (start < 0) start = 0;
    int y = 30;
    for (int i = start; i < line_count; i++, y += 10) ax_text(lines[i], 10, y, AX_RGB(205, 220, 205));
    ax_text("$ ", 10, CH - 16, AX_RGB(100, 200, 255));
    ax_text(input, 26, CH - 16, AX_RGB(230, 240, 230));
    ax_rect(26 + input_len * 8, CH - 16, 6, 8, AX_RGB(100, 200, 255));
}

void _start(void)
{
    canvas = ax_surface("XSH", CW, CH);
    ax_canvas_dims(CW, CH);
    if (!canvas) for (;;) yield();
    g_canvas = canvas;
    push_line("XSH 0.1 - type 'help'");
    bool alive = true;
    while (alive) {
        render();
        ax_commit(canvas);
        ax_event ev;
        while (ax_poll(canvas, &ev) > 0) {
            if (ev.type == AX_EV_CLOSE) return;
            if (ev.type != AX_EV_KEY) continue;
            char k = (char)ev.key;
            if (k == '\n' || k == '\r') {
                input[input_len] = 0;
                push_prefixed("$ ", input);
                alive = run_cmd(input);
                input_len = 0; input[0] = 0;
            } else if (k == '\b' || k == 127) {
                if (input_len > 0) input[--input_len] = 0;
            } else if (k >= 32 && k <= 126 && input_len < INPUT_LEN - 1) {
                input[input_len++] = k;
                input[input_len] = 0;
            }
        }
        yield();
    }
}
