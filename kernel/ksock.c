#include "ksock.h"
#include "../mm/malloc.h"
#include "../lib/common.h"
#include <stddef.h>

#define KSOCK_BUF_SIZE 65536u
#define KSOCK_MAX_LISTENERS 16u
#define KSOCK_BACKLOG_CAP 16u
#define KSOCK_PATH_MAX 108u

struct ksock {
    ksock_t *peer;
    ksock_t *listener;
    uint8_t  buf[KSOCK_BUF_SIZE];
    volatile uint32_t r_head;
    volatile uint32_t r_tail;
    volatile uint8_t  r_closed;
    volatile uint8_t  w_closed;
    ksock_t *pend[KSOCK_BACKLOG_CAP];
    volatile uint32_t pend_head;
    volatile uint32_t pend_tail;
};

typedef struct {
    char     path[KSOCK_PATH_MAX];
    ksock_t *listener;
    uint8_t  used;
} ksock_binding_t;

static ksock_binding_t ksock_bindings[KSOCK_MAX_LISTENERS];
static volatile uint32_t ksock_lock;

static void ksock_spin_lock(void)
{
    while (__sync_lock_test_and_set(&ksock_lock, 1u))
        __asm__ volatile("pause" ::: "memory");
}

static void ksock_spin_unlock(void)
{
    __sync_lock_release(&ksock_lock);
}

static int ksock_path_eq(const char *a, const char *b)
{
    uint32_t i = 0;
    while (i < KSOCK_PATH_MAX) {
        if (a[i] != b[i]) return 0;
        if (!a[i]) return 1;
        i++;
    }
    return 1;
}

void ksock_init(void)
{
    memset(ksock_bindings, 0, sizeof(ksock_bindings));
}

ksock_t *ksock_create(void)
{
    ksock_t *s = (ksock_t*)malloc(sizeof(ksock_t));
    if (!s) return NULL;
    memset(s, 0, sizeof(*s));
    return s;
}

int ksock_bind(ksock_t *s, const char *path)
{
    if (!s || !path) return KSOCK_EINVAL;
    ksock_spin_lock();
    for (uint32_t i = 0; i < KSOCK_MAX_LISTENERS; i++) {
        if (ksock_bindings[i].used && ksock_path_eq(ksock_bindings[i].path, path)) {
            ksock_spin_unlock();
            return KSOCK_EINVAL;
        }
    }
    for (uint32_t i = 0; i < KSOCK_MAX_LISTENERS; i++) {
        if (!ksock_bindings[i].used) {
            ksock_bindings[i].used = 1;
            uint32_t k = 0;
            while (path[k] && k < KSOCK_PATH_MAX - 1) {
                ksock_bindings[i].path[k] = path[k];
                k++;
            }
            ksock_bindings[i].path[k] = 0;
            ksock_bindings[i].listener = s;
            s->listener = s;
            ksock_spin_unlock();
            return 0;
        }
    }
    ksock_spin_unlock();
    return KSOCK_EINVAL;
}

int ksock_listen(ksock_t *s, int backlog)
{
    (void)backlog;
    if (!s) return KSOCK_EINVAL;
    return 0;
}

ksock_t *ksock_accept(ksock_t *listener)
{
    if (!listener) return NULL;
    ksock_spin_lock();
    if (listener->pend_head == listener->pend_tail) {
        ksock_spin_unlock();
        return NULL;
    }
    ksock_t *c = listener->pend[listener->pend_tail];
    listener->pend[listener->pend_tail] = NULL;
    listener->pend_tail = (listener->pend_tail + 1) % KSOCK_BACKLOG_CAP;
    ksock_spin_unlock();

    ksock_t *srv = ksock_create();
    if (!srv) { ksock_close(c); return NULL; }
    srv->peer = c;
    c->peer = srv;
    return srv;
}

ksock_t *ksock_connect(const char *path)
{
    if (!path) return NULL;
    ksock_spin_lock();
    ksock_t *listener = NULL;
    for (uint32_t i = 0; i < KSOCK_MAX_LISTENERS; i++) {
        if (ksock_bindings[i].used && ksock_path_eq(ksock_bindings[i].path, path)) {
            listener = ksock_bindings[i].listener;
            break;
        }
    }
    if (!listener) { ksock_spin_unlock(); return NULL; }
    uint32_t next = (listener->pend_head + 1) % KSOCK_BACKLOG_CAP;
    if (next == listener->pend_tail) { ksock_spin_unlock(); return NULL; }

    ksock_t *cli = ksock_create();
    if (!cli) { ksock_spin_unlock(); return NULL; }
    cli->listener = listener;
    listener->pend[listener->pend_head] = cli;
    listener->pend_head = next;
    ksock_spin_unlock();
    return cli;
}

int ksock_send(ksock_t *s, const void *buf, uint32_t len)
{
    if (!s) return KSOCK_EPIPE;
    ksock_t *peer = s->peer;
    if (!peer || peer->r_closed) return KSOCK_EPIPE;
    if (!buf || len == 0) return 0;

    const uint8_t *p = (const uint8_t*)buf;
    uint32_t sent = 0;
    while (sent < len) {
        uint32_t head = peer->r_head;
        uint32_t tail = peer->r_tail;
        uint32_t used = (head - tail) % KSOCK_BUF_SIZE;
        uint32_t space = KSOCK_BUF_SIZE - 1 - used;
        if (space == 0) break;
        uint32_t chunk = len - sent;
        if (chunk > space) chunk = space;
        for (uint32_t i = 0; i < chunk; i++) {
            peer->buf[(head + i) % KSOCK_BUF_SIZE] = p[sent + i];
        }
        __sync_synchronize();
        peer->r_head = (head + chunk) % KSOCK_BUF_SIZE;
        sent += chunk;
    }
    return (int)sent;
}

int ksock_recv(ksock_t *s, void *buf, uint32_t len, int nonblock)
{
    if (!s || !buf) return KSOCK_EINVAL;
    if (len == 0) return 0;
    uint8_t *p = (uint8_t*)buf;
    uint32_t got = 0;
    while (got < len) {
        uint32_t head = s->r_head;
        uint32_t tail = s->r_tail;
        uint32_t avail = (head - tail) % KSOCK_BUF_SIZE;
        if (avail == 0) break;
        uint32_t chunk = len - got;
        if (chunk > avail) chunk = avail;
        for (uint32_t i = 0; i < chunk; i++)
            p[got + i] = s->buf[(tail + i) % KSOCK_BUF_SIZE];
        s->r_tail = (tail + chunk) % KSOCK_BUF_SIZE;
        got += chunk;
    }
    if (got == 0) {
        if (s->r_closed) return 0;
        return nonblock ? KSOCK_EAGAIN : 0;
    }
    return (int)got;
}

int ksock_avail(ksock_t *s)
{
    if (!s) return 0;
    return (int)((s->r_head - s->r_tail) % KSOCK_BUF_SIZE);
}

int ksock_write_space(ksock_t *s)
{
    if (!s || !s->peer) return 0;
    ksock_t *peer = s->peer;
    uint32_t used = (peer->r_head - peer->r_tail) % KSOCK_BUF_SIZE;
    return (int)(KSOCK_BUF_SIZE - 1 - used);
}

int ksock_readable(ksock_t *s)
{
    if (!s) return 0;
    if (ksock_avail(s) > 0) return 1;
    return s->r_closed ? 1 : 0;
}

int ksock_writable(ksock_t *s)
{
    if (!s) return 0;
    if (s->w_closed) return 0;
    return ksock_write_space(s) > 0 ? 1 : 0;
}

int ksock_has_pending(ksock_t *listener)
{
    if (!listener) return 0;
    return listener->pend_head != listener->pend_tail;
}

int ksock_shutdown(ksock_t *s, int how)
{
    if (!s) return KSOCK_EINVAL;
    if (how == 0 || how == 2) {
        s->r_closed = 1;
    }
    if (how == 1 || how == 2) {
        s->w_closed = 1;
        if (s->peer) s->peer->r_closed = 1;
    }
    return 0;
}

void ksock_close(ksock_t *s)
{
    if (!s) return;
    if (s->peer) {
        s->peer->r_closed = 1;
        s->peer->peer = NULL;
    }
    if (s->listener && s->listener == s) {
        ksock_spin_lock();
        for (uint32_t i = 0; i < KSOCK_MAX_LISTENERS; i++) {
            if (ksock_bindings[i].used && ksock_bindings[i].listener == s) {
                ksock_bindings[i].used = 0;
            }
        }
        ksock_spin_unlock();
    }
    free(s);
}

int ksock_pipe(ksock_t **a, ksock_t **b)
{
    ksock_t *pa = ksock_create();
    ksock_t *pb = ksock_create();
    if (!pa || !pb) {
        if (pa) free(pa);
        if (pb) free(pb);
        return KSOCK_EINVAL;
    }
    pa->peer = pb;
    pb->peer = pa;
    *a = pa;
    *b = pb;
    return 0;
}
