#ifndef KSOCK_H
#define KSOCK_H

#include <stdint.h>

#define KSOCK_EAGAIN (-11)
#define KSOCK_EPIPE  (-32)
#define KSOCK_EINVAL (-22)
#define KSOCK_ECONNREFUSED (-111)

typedef struct ksock ksock_t;

void     ksock_init(void);
ksock_t *ksock_create(void);
int      ksock_bind(ksock_t *s, const char *path);
int      ksock_listen(ksock_t *s, int backlog);
ksock_t *ksock_accept(ksock_t *listener);
ksock_t *ksock_connect(const char *path);
int      ksock_send(ksock_t *s, const void *buf, uint32_t len);
int      ksock_recv(ksock_t *s, void *buf, uint32_t len, int nonblock);
int      ksock_avail(ksock_t *s);
int      ksock_write_space(ksock_t *s);
int      ksock_readable(ksock_t *s);
int      ksock_writable(ksock_t *s);
int      ksock_has_pending(ksock_t *listener);
int      ksock_shutdown(ksock_t *s, int how);
void     ksock_close(ksock_t *s);
int      ksock_pipe(ksock_t **a, ksock_t **b);

#endif
