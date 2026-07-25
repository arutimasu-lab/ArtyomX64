#include "tty.h"
#include "../mm/kheap.h"
#include "../lib/common.h"

#define malloc kmalloc
#define free kfree
#define MAX_PTYS 16

static tty_device_t *ptys[MAX_PTYS];
tty_device_t *active_master_tty = 0;

static void tty_copy_name(char *dst, const char *src)
{
    int i = 0;
    while (src && src[i] && i < 15) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

int pty_buffer_write(pty_buffer_t *buf, char ch)
{
    if (!buf || buf->count >= TTY_BUFFER_SIZE) return 0;
    buf->data[buf->head] = ch;
    buf->head = (buf->head + 1) % TTY_BUFFER_SIZE;
    buf->count++;
    return 1;
}

int pty_buffer_read(pty_buffer_t *buf, char *out)
{
    if (!buf || !out || buf->count == 0) return 0;
    *out = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % TTY_BUFFER_SIZE;
    buf->count--;
    return 1;
}

void tty_init(void)
{
    for (int i = 0; i < MAX_PTYS; i++) ptys[i] = 0;
    active_master_tty = 0;
}

tty_device_t *tty_create(const char *name)
{
    for (int i = 0; i < MAX_PTYS; i++) {
        if (ptys[i]) continue;
        tty_device_t *tty = (tty_device_t*)malloc(sizeof(tty_device_t));
        if (!tty) return 0;
        memset((u8int*)tty, 0, sizeof(tty_device_t));
        tty_copy_name(tty->name, name ? name : "pts");
        tty->termios_c_lflag = ISIG | ICANON | ECHO;
        tty->node.flags = FS_CHARDEVICE;
        tty->node.impl = (u32int)(uintptr_t)tty;
        tty->node.read = tty_slave_read;
        tty->node.write = tty_slave_write;
        tty_copy_name(tty->node.name, tty->name);
        ptys[i] = tty;
        if (!active_master_tty) active_master_tty = tty;
        return tty;
    }
    return 0;
}

void tty_destroy(tty_device_t *tty)
{
    if (!tty) return;
    for (int i = 0; i < MAX_PTYS; i++) if (ptys[i] == tty) ptys[i] = 0;
    if (active_master_tty == tty) active_master_tty = 0;
    free(tty);
}

void tty_master_feed(tty_device_t *tty, char ch)
{
    if (!tty) return;
    if (ch == '\r') ch = '\n';
    if (ch == '\b' || ch == 127) {
        if (tty->input.count > 0) {
            tty->input.head = (tty->input.head + TTY_BUFFER_SIZE - 1) % TTY_BUFFER_SIZE;
            tty->input.count--;
        }
    } else {
        pty_buffer_write(&tty->input, ch);
    }
    tty->has_data = tty->input.count != 0;
}

u32int tty_slave_read(fs_node_t *node, u32int offset, u32int size, u8int *buffer)
{
    (void)offset;
    tty_device_t *tty = node ? (tty_device_t*)(uintptr_t)node->impl : active_master_tty;
    if (!tty || !buffer || size == 0) return 0;
    u32int n = 0;
    char ch;
    while (n < size && pty_buffer_read(&tty->input, &ch)) {
        buffer[n++] = (u8int)ch;
        if ((tty->termios_c_lflag & ICANON) && ch == '\n') break;
    }
    tty->has_data = tty->input.count != 0;
    return n;
}

u32int tty_slave_write(fs_node_t *node, u32int offset, u32int size, u8int *buffer)
{
    (void)offset;
    tty_device_t *tty = node ? (tty_device_t*)(uintptr_t)node->impl : active_master_tty;
    if (!tty || !buffer) return 0;
    u32int n = 0;
    while (n < size && pty_buffer_write(&tty->output, (char)buffer[n])) n++;
    return n;
}
