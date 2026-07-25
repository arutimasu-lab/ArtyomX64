#ifndef DEV_TTY_H
#define DEV_TTY_H

#include <stdbool.h>
#include <stdint.h>
#include "../lib/common.h"
#include "../fs/fs.h"

#define TTY_BUFFER_SIZE 1024
#define ISIG   0x0001
#define ICANON 0x0002
#define ECHO   0x0004

typedef struct {
    char data[TTY_BUFFER_SIZE];
    u32int head;
    u32int tail;
    u32int count;
} pty_buffer_t;

typedef struct tty_device {
    pty_buffer_t input;
    pty_buffer_t output;
    u32int termios_c_lflag;
    bool has_data;
    fs_node_t node;
    char name[16];
} tty_device_t;

extern tty_device_t *active_master_tty;

void tty_init(void);
tty_device_t *tty_create(const char *name);
void tty_destroy(tty_device_t *tty);
int pty_buffer_write(pty_buffer_t *buf, char ch);
int pty_buffer_read(pty_buffer_t *buf, char *out);
void tty_master_feed(tty_device_t *tty, char ch);
u32int tty_slave_read(fs_node_t *node, u32int offset, u32int size, u8int *buffer);
u32int tty_slave_write(fs_node_t *node, u32int offset, u32int size, u8int *buffer);

#endif
