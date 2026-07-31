#ifndef KPTY_H
#define KPTY_H

#include <stdint.h>

#define KPTY_MAX 8u
#define KPTY_BUF_SIZE 4096u

#define KPTY_TIOCGETA   0x5401
#define KPTY_TIOCSETA   0x5402
#define KPTY_TIOCGWINSZ 0x5413
#define KPTY_TIOCSWINSZ 0x5414
#define KPTY_TIOCGPGRP  0x540F
#define KPTY_TIOCSPGRP  0x5410

typedef struct {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[19];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
} kpty_termios_t;

typedef struct {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
} kpty_winsize_t;

typedef struct kpty_pair kpty_pair_t;

void        kpty_init(void);
kpty_pair_t *kpty_alloc(void);
int         kpty_master_fd(kpty_pair_t *p);
int         kpty_slave_fd(kpty_pair_t *p);
int         kpty_master_read(kpty_pair_t *p, void *buf, uint32_t len, int nonblock);
int         kpty_master_write(kpty_pair_t *p, const void *buf, uint32_t len);
int         kpty_slave_read(kpty_pair_t *p, void *buf, uint32_t len, int nonblock);
int         kpty_slave_write(kpty_pair_t *p, const void *buf, uint32_t len);
int         kpty_slave_avail(kpty_pair_t *p);
int         kpty_master_avail(kpty_pair_t *p);
int         kpty_slave_write_space(kpty_pair_t *p);
int         kpty_master_write_space(kpty_pair_t *p);
int         kpty_ioctl(kpty_pair_t *p, uint64_t req, void *arg, int is_master);
void        kpty_close_master(kpty_pair_t *p);
void        kpty_close_slave(kpty_pair_t *p);
kpty_pair_t *kpty_by_index(uint32_t idx);

#endif
