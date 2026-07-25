#include "../lib/axipc.h"
#include "../lib/libax.h"
#include "../lib/unistd.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define COL_BG      AX_ARGB(0xFF, 0x1E, 0x1E, 0x2E)
#define COL_FG      AX_ARGB(0xFF, 0xCD, 0xD6, 0xF4)
#define COL_PROMPT  AX_ARGB(0xFF, 0x89, 0xB4, 0xFA)
#define COL_CURSOR  AX_ARGB(0xFF, 0xF5, 0xE0, 0xDC)
#define COL_ERR     AX_ARGB(0xFF, 0xF3, 0x8B, 0xA8)

#define TERM_LINES 1024
#define TERM_COLS  80
#define TERM_INPUT 512

typedef struct {
    char    lines[TERM_LINES][TERM_COLS + 1];
    int     count;
    char    input[TERM_INPUT];
    int     pos;
    bool    alive;
} term_t;

static term_t t;
static uint32_t *cb;
static int cw, ch;

static void t_scroll(void)
{
    if (t.count >= TERM_LINES) {
        for (int i = 0; i < TERM_LINES - 1; i++)
            for (int j = 0; j <= TERM_COLS; j++)
                t.lines[i][j] = t.lines[i + 1][j];
        t.count = TERM_LINES - 1;
    }
    t.lines[t.count][0] = 0;
    t.count++;
}

static void t_putc(char c)
{
    if (t.count == 0) { t.count = 1; t.lines[0][0] = 0; }
    int len = 0;
    while (t.lines[t.count - 1][len] && len < TERM_COLS) len++;
    if (c == '\n') { t_scroll(); return; }
    if (len < TERM_COLS) {
        t.lines[t.count - 1][len] = c;
        t.lines[t.count - 1][len + 1] = 0;
    }
}

static void t_puts(const char *s) { while (*s) t_putc(*s++); }

static void t_putln(const char *s) { t_puts(s); t_scroll(); }

static void t_str(int n, char *out)
{
    if (n == 0) { out[0] = '0'; out[1] = 0; return; }
    char tmp[12]; int p = 0;
    while (n) { tmp[p++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (p) out[j++] = tmp[--p];
    out[j] = 0;
}

static void t_char(char c, int x, int y, uint32_t fg)
{
    if (x < 0 || y < 0 || x + 7 >= cw || y + 7 >= ch) return;
    const unsigned char *g = font8x8_basic[(unsigned char)c];
    for (int cy = 0; cy < 8; cy++) {
        uint8_t row = g[cy];
        for (int cx = 0; cx < 8; cx++)
            if (row & (1 << cx))
                cb[(y + cy) * cw + (x + cx)] = fg;
    }
}

static void t_draw(void)
{
    for (int i = 0; i < cw * ch; i++) cb[i] = COL_BG;

    int vis = ch / 9 - 1;
    if (vis < 1) vis = 1;
    int start = t.count - vis;
    if (start < 0) start = 0;

    for (int i = 0; i < vis && (start + i) < t.count; i++) {
        int ly = i * 9;
        for (int x = 0; x < TERM_COLS; x++) {
            char c = t.lines[start + i][x];
            if (!c) break;
            t_char(c, x * 8, ly, COL_FG);
        }
    }

    ax_time_t tm;
    ax_time(&tm);
    char pr[16];
    pr[0] = '['; pr[1] = '0' + tm.hour / 10; pr[2] = '0' + tm.hour % 10;
    pr[3] = ':'; pr[4] = '0' + tm.minute / 10; pr[5] = '0' + tm.minute % 10;
    pr[6] = ']'; pr[7] = ' '; pr[8] = '$'; pr[9] = ' '; pr[10] = 0;

    int py = ch - 9;
    for (int i = 0; pr[i]; i++)
        t_char(pr[i], i * 8, py, COL_PROMPT);

    int px = 10 * 8;
    for (int i = 0; i < t.pos; i++)
        t_char(t.input[i], px + i * 8, py, COL_FG);

    int cx = px + t.pos * 8;
    for (int dy = 0; dy < 8; dy++)
        if (cx < cw && py + dy < ch)
            cb[(py + dy) * cw + cx] = COL_CURSOR;
}

static int t_cmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void t_copy(char *d, const char *s, int n)
{
    for (int i = 0; i < n; i++) d[i] = s[i];
}

static int t_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void t_trim(char *s)
{
    int l = t_len(s);
    while (l > 0 && (s[l - 1] == '\n' || s[l - 1] == '\r' || s[l - 1] == ' '))
        s[--l] = 0;
}

static void cmd_help(void)
{
    t_putln("AXTerm v1.0 - Ring 3 ELF Terminal");
    t_putln("");
    t_putln(" help     clear    echo    date    ls    cat");
    t_putln(" exec     exit");
}

static void cmd_clear(void)
{
    for (int i = 0; i < TERM_LINES; i++) t.lines[i][0] = 0;
    t.count = 0;
}

static void cmd_date(void)
{
    ax_time_t tm;
    ax_time(&tm);
    static const char *mo[] = {"","Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    char b[64]; int p = 0;
    t_str(tm.day, b + p); p = t_len(b); b[p++] = ' ';
    const char *mn = mo[tm.month >= 1 && tm.month <= 12 ? tm.month : 1];
    while (*mn) b[p++] = *mn++;
    b[p++] = ' ';
    t_str(tm.year, b + p); p = t_len(b); b[p++] = ' ';
    t_str(tm.hour, b + p); p = t_len(b); b[p++] = ':';
    t_str(tm.minute, b + p); p = t_len(b); b[p++] = ':';
    t_str(tm.second, b + p); b[t_len(b)] = 0;
    t_putln(b);
}

static void cmd_echo(char *a) { if (a && *a) t_putln(a); else t_scroll(); }

static void cmd_ls(void)
{
    int fd = open("/", 0, 0);
    if (fd < 0) { t_putln("(cannot open /)"); return; }
    static char dbuf[4096];
    int n = getdents(fd, dbuf, sizeof(dbuf) - 1);
    if (n > 0) {
        char *p = dbuf;
        while (n > 0) {
            p += 4;
            int reclen = ((unsigned char)p[0]) | ((unsigned char)p[1] << 8);
            p += 3;
            t_putln(p);
            int nl = t_len(p) + 1;
            p += nl;
            n -= reclen;
        }
    } else {
        t_putln("(empty)");
    }
}

static void cmd_cat(char *a)
{
    if (!a || !*a) { t_putln("Usage: cat <file>"); return; }
    int fd = open(a, 0, 0);
    if (fd < 0) { t_putln("File not found"); return; }
    static char buf[4096];
    int n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        char *p = buf;
        while (*p) {
            char *nl = p;
            while (*nl && *nl != '\n') nl++;
            char sv = *nl; *nl = 0;
            t_putln(p);
            if (!sv) break;
            p = nl + 1;
        }
    } else {
        t_putln("(empty)");
    }
}

static void cmd_exec(char *a)
{
    if (!a || !*a) { t_putln("Usage: exec <file>"); return; }
    t_puts("exec: "); t_putln(a);
    t_putln("(use dock to launch ELF apps)");
    exec(a);
}

static void cmd_unknown(const char *c)
{
    t_puts(c); t_putln(": command not found");
}

static void t_exec(void)
{
    t_trim(t.input);
    if (t.input[0] == 0) { t_scroll(); return; }

    char cmd[64], args[512];
    int i = 0, j = 0;
    while (i < 63 && t.input[i] && t.input[i] != ' ') cmd[i++] = t.input[i];
    cmd[i] = 0;
    while (t.input[i] == ' ') i++;
    while (j < 511 && t.input[i]) args[j++] = t.input[i++];
    args[j] = 0;

         if (t_cmp(cmd, "help") == 0)  cmd_help();
    else if (t_cmp(cmd, "clear") == 0) cmd_clear();
    else if (t_cmp(cmd, "date") == 0)  cmd_date();
    else if (t_cmp(cmd, "echo") == 0)  cmd_echo(args);
    else if (t_cmp(cmd, "ls") == 0)    cmd_ls();
    else if (t_cmp(cmd, "cat") == 0)   cmd_cat(args);
    else if (t_cmp(cmd, "exec") == 0)  cmd_exec(args);
    else if (t_cmp(cmd, "exit") == 0)  { t.alive = false; return; }
    else cmd_unknown(cmd);

    t.input[0] = 0; t.pos = 0;
}

void _start(void)
{
    ax_screen_t sc;
    ax_screen(&sc);
    cw = sc.width;
    ch = sc.height / 2;
    if (cw < 640) cw = 640;
    if (ch < 300) ch = 300;
    if (cw > (int)sc.width) cw = sc.width;
    if (ch > (int)sc.height) ch = sc.height;

    uint32_t *c = ax_surface("Terminal", cw, ch);
    if (!c) { for (;;) __asm__ volatile("hlt"); }
    cb = c;

    for (int i = 0; i < TERM_LINES; i++) t.lines[i][0] = 0;
    t.count = 0; t.pos = 0; t.alive = true;

    t_putln("AXTerm v1.0 - ArtyomX Ring 3 Terminal");
    t_putln("Type 'help' for available commands.");
    t_putln("");

    while (t.alive) {
        t_draw();
        ax_event ev;
        while (ax_poll(cb, &ev) > 0) {
            if (ev.type == AX_EV_KEY) {
                char k = (char)ev.key;
                if (k == '\n' || k == '\r') {
                    t_putln(t.input);
                    t_exec();
                } else if (k == '\b' || k == 127) {
                    if (t.pos > 0) t.input[--t.pos] = 0;
                } else if (k >= 32 && k <= 126) {
                    if (t.pos < TERM_INPUT - 1) {
                        t.input[t.pos++] = k;
                        t.input[t.pos] = 0;
                    }
                }
            } else if (ev.type == AX_EV_CLOSE || ev.type == AX_EV_RESIZE) {
                t.alive = false;
            }
        }
        //__asm__ volatile("hlt");
         yield();
    }
}
