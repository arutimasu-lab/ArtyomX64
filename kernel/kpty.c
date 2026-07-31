#include "kpty.h"
#include "../mm/malloc.h"
#include "../lib/common.h"
#include <stddef.h>

struct kpty_pair {
    uint8_t  used;
    uint8_t  master_closed;
    uint8_t  slave_closed;
    uint32_t index;
    kpty_termios_t termios;
    kpty_winsize_t winsize;
    uint8_t  to_master[KPTY_BUF_SIZE];
    volatile uint32_t to_master_head;
    volatile uint32_t to_master_tail;
    uint8_t  to_slave[KPTY_BUF_SIZE];
    volatile uint32_t to_slave_head;
    volatile uint32_t to_slave_tail;
};

static kpty_pair_t kpty_pairs[KPTY_MAX];
static volatile uint32_t kpty_lock;

static void kpty_spin_lock(void)
{
    while (__sync_lock_test_and_set(&kpty_lock, 1u))
        __asm__ volatile("pause" ::: "memory");
}

static void kpty_spin_unlock(void)
{
    __sync_lock_release(&kpty_lock);
}

void kpty_init(void)
{
    memset(kpty_pairs, 0, sizeof(kpty_pairs));
}

static void kpty_default_termios(kpty_termios_t *t)
{
    memset(t, 0, sizeof(*t));
    t->c_iflag = 0x0500;
    t->c_oflag = 0x0005;
    t->c_cflag = 0x00BF;
    t->c_lflag = 0x8A3B;
    t->c_cc[0] = 3;
    t->c_cc[1] = 28;
    t->c_cc[2] = 127;
    t->c_cc[3] = 21;
    t->c_cc[4] = 4;
    t->c_cc[5] = 0;
    t->c_cc[6] = 0;
    t->c_cc[7] = 0;
    t->c_cc[8] = 17;
    t->c_cc[9] = 19;
    t->c_cc[10] = 26;
    t->c_ispeed = 38400;
    t->c_ospeed = 38400;
}

kpty_pair_t *kpty_alloc(void)
{
    kpty_spin_lock();
    for (uint32_t i = 0; i < KPTY_MAX; i++) {
        if (!kpty_pairs[i].used) {
            memset(&kpty_pairs[i], 0, sizeof(kpty_pair_t));
            kpty_pairs[i].used = 1;
            kpty_pairs[i].index = i;
            kpty_default_termios(&kpty_pairs[i].termios);
            kpty_pairs[i].winsize.ws_row = 24;
            kpty_pairs[i].winsize.ws_col = 80;
            kpty_pairs[i].winsize.ws_xpixel = 640;
            kpty_pairs[i].winsize.ws_ypixel = 384;
            kpty_spin_unlock();
            return &kpty_pairs[i];
        }
    }
    kpty_spin_unlock();
    return NULL;
}

static int kpty_ring_write(uint8_t *buf, volatile uint32_t *head, volatile uint32_t *tail,
                           const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t*)data;
    uint32_t h = *head;
    uint32_t t = *tail;
    uint32_t used = (h - t) % KPTY_BUF_SIZE;
    uint32_t space = KPTY_BUF_SIZE - 1 - used;
    uint32_t chunk = len < space ? len : space;
    for (uint32_t i = 0; i < chunk; i++)
        buf[(h + i) % KPTY_BUF_SIZE] = p[i];
    __sync_synchronize();
    *head = (h + chunk) % KPTY_BUF_SIZE;
    return (int)chunk;
}

static int kpty_ring_read(uint8_t *buf, volatile uint32_t *head, volatile uint32_t *tail,
                          void *out, uint32_t len)
{
    uint8_t *p = (uint8_t*)out;
    uint32_t h = *head;
    uint32_t t = *tail;
    uint32_t avail = (h - t) % KPTY_BUF_SIZE;
    uint32_t chunk = len < avail ? len : avail;
    for (uint32_t i = 0; i < chunk; i++)
        p[i] = buf[(t + i) % KPTY_BUF_SIZE];
    *tail = (t + chunk) % KPTY_BUF_SIZE;
    return (int)chunk;
}

int kpty_master_read(kpty_pair_t *p, void *buf, uint32_t len, int nonblock)
{
    if (!p || !p->used) return -9;
    if (p->master_closed) return -32;
    int got = kpty_ring_read(p->to_master, &p->to_master_head, &p->to_master_tail, buf, len);
    if (got == 0 && p->slave_closed) return 0;
    if (got == 0 && nonblock) return -11;
    return got;
}

int kpty_master_write(kpty_pair_t *p, const void *buf, uint32_t len)
{
    if (!p || !p->used) return -9;
    if (p->master_closed) return -32;
    if (p->slave_closed) return 0;
    return kpty_ring_write(p->to_slave, &p->to_slave_head, &p->to_slave_tail, buf, len);
}

int kpty_slave_read(kpty_pair_t *p, void *buf, uint32_t len, int nonblock)
{
    if (!p || !p->used) return -9;
    if (p->slave_closed) return -32;
    int got = kpty_ring_read(p->to_slave, &p->to_slave_head, &p->to_slave_tail, buf, len);
    if (got == 0 && p->master_closed) return 0;
    if (got == 0 && nonblock) return -11;
    return got;
}

int kpty_slave_write(kpty_pair_t *p, const void *buf, uint32_t len)
{
    if (!p || !p->used) return -9;
    if (p->slave_closed) return -32;
    if (p->master_closed) return 0;
    const uint8_t *in = (const uint8_t*)buf;
    uint32_t written = 0;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t c = in[i];
        if (c == '\n') {
            uint8_t cr = '\r';
            kpty_ring_write(p->to_master, &p->to_master_head, &p->to_master_tail, &cr, 1);
        }
        written += (uint32_t)kpty_ring_write(p->to_master, &p->to_master_head, &p->to_master_tail, &c, 1);
    }
    return (int)written;
}

int kpty_slave_avail(kpty_pair_t *p)
{
    if (!p || !p->used) return 0;
    return (int)((p->to_slave_head - p->to_slave_tail) % KPTY_BUF_SIZE);
}

int kpty_master_avail(kpty_pair_t *p)
{
    if (!p || !p->used) return 0;
    return (int)((p->to_master_head - p->to_master_tail) % KPTY_BUF_SIZE);
}

int kpty_slave_write_space(kpty_pair_t *p)
{
    if (!p || !p->used) return 0;
    uint32_t used = (p->to_slave_head - p->to_slave_tail) % KPTY_BUF_SIZE;
    return (int)(KPTY_BUF_SIZE - 1 - used);
}

int kpty_master_write_space(kpty_pair_t *p)
{
    if (!p || !p->used) return 0;
    uint32_t used = (p->to_master_head - p->to_master_tail) % KPTY_BUF_SIZE;
    return (int)(KPTY_BUF_SIZE - 1 - used);
}

int kpty_ioctl(kpty_pair_t *p, uint64_t req, void *arg, int is_master)
{
    (void)is_master;
    if (!p || !p->used) return -9;
    switch (req) {
        case KPTY_TIOCGETA:
            if (!arg) return -14;
            memcpy(arg, &p->termios, sizeof(kpty_termios_t));
            return 0;
        case KPTY_TIOCSETA:
            if (!arg) return -14;
            memcpy(&p->termios, arg, sizeof(kpty_termios_t));
            return 0;
        case KPTY_TIOCGWINSZ:
            if (!arg) return -14;
            memcpy(arg, &p->winsize, sizeof(kpty_winsize_t));
            return 0;
        case KPTY_TIOCSWINSZ:
            if (!arg) return -14;
            memcpy(&p->winsize, arg, sizeof(kpty_winsize_t));
            return 0;
        case KPTY_TIOCGPGRP:
            if (!arg) return -14;
            *(int*)arg = 0;
            return 0;
        case KPTY_TIOCSPGRP:
            return 0;
        default:
            return -25;
    }
}

void kpty_close_master(kpty_pair_t *p)
{
    if (!p) return;
    p->master_closed = 1;
    if (p->slave_closed)
        p->used = 0;
}

void kpty_close_slave(kpty_pair_t *p)
{
    if (!p) return;
    p->slave_closed = 1;
    if (p->master_closed)
        p->used = 0;
}

kpty_pair_t *kpty_by_index(uint32_t idx)
{
    if (idx >= KPTY_MAX) return NULL;
    return kpty_pairs[idx].used ? &kpty_pairs[idx] : NULL;
}
