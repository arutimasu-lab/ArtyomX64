#include "linux_sys.h"
#include "compat.h"
#include "isr.h"
#include "ksock.h"
#include "kpty.h"
#include "../fs/fs.h"
#include "../fs/elf.h"
#include "../fs/task.h"
#include "../mm/malloc.h"
#include "../mm/vmm.h"
#include "../mm/pmm.h"
#include "../drivers/monitor.h"
#include "../drivers/timer.h"
#include "../drivers/net/ixn_socket_api.h"
#include "../lib/common.h"
#include <stddef.h>

extern void yield(void);
extern void task_signal_deliver(void);
extern void task_signal_return(void);
extern int  task_signal_send(int pid, int sig);
extern int  task_clone_thread(uint64_t flags, uint64_t stack, uint64_t tls, int *ctid);

#define LINUX_FD_MAX 128u

#define FD_FREE  0u
#define FD_STD   1u
#define FD_FILE  2u
#define FD_SOCK  3u
#define FD_PIPE  4u
#define FD_INET  5u
#define FD_PTY_MASTER 6u
#define FD_PTY_SLAVE  7u

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY 0x0000
#define O_WRONLY 0x0001
#define O_RDWR   0x0002
#define O_CREAT  0x0040
#define O_TRUNC  0x0200
#define O_APPEND 0x0400
#define O_NONBLOCK 0x0800

#define AF_UNIX  1
#define AF_LOCAL 1
#define AF_INET  2

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct linux_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_atime_nsec;
    int64_t  st_mtime;
    int64_t  st_mtime_nsec;
    int64_t  st_ctime;
    int64_t  st_ctime_nsec;
    int64_t  __unused[3];
};

struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct linux_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct linux_pollfd {
    int      fd;
    int16_t  events;
    int16_t  revents;
};

struct linux_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};

typedef struct {
    uint8_t    kind;
    uint32_t   flags;
    fs_node_t *node;
    uint64_t   pos;
    ksock_t   *sock;
    char       path[112];
    int        ixn_fd;
    kpty_pair_t *pty;
} linux_fd_t;

static linux_fd_t linux_fds[LINUX_FD_MAX];

static int strncmp_len(const char *a, const char *b, uint32_t n);

static int64_t lsys_mremap_stub(void);
static int64_t lsys_setuid_stub(void);
static int64_t lsys_setgid_stub(void);
static int64_t lsys_setreuid_stub(void);
static int64_t lsys_setregid_stub(void);
static int64_t lsys_setresuid_stub(void);
static int64_t lsys_setresgid_stub(void);
static int64_t lsys_getresuid_stub(void);
static int64_t lsys_getresgid_stub(void);
static int64_t lsys_sigpending_stub(void);
static int64_t lsys_chroot_stub(void);
static int64_t lsys_fadvise_stub(void);
static int64_t lsys_inotify_stub(void);

static void lsys_serial_putc(char c){ outb(0x3F8, c); }
static void lsys_serial_puts(const char *s){ while(*s) lsys_serial_putc(*s++); }

static int lsys_alloc_fd(void)
{
    for (int i = 3; i < (int)LINUX_FD_MAX; i++) {
        if (linux_fds[i].kind == FD_FREE) {
            memset(&linux_fds[i], 0, sizeof(linux_fd_t));
            return i;
        }
    }
    return -1;
}

static int lsys_fd_readable(int fd)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return 0;
    linux_fd_t *f = &linux_fds[fd];
    switch (f->kind) {
        case FD_STD:
            if (fd == 0) {
                extern int is_buffer_empty(void);
                return !is_buffer_empty();
            }
            return 1;
        case FD_FILE: {
            if (!f->node) return 0;
            return f->pos < f->node->length;
        }
        case FD_SOCK:
        case FD_PIPE:
            return ksock_readable(f->sock);
        case FD_INET:
            return 1;
        case FD_PTY_MASTER:
            return kpty_master_avail(f->pty) > 0;
        case FD_PTY_SLAVE:
            return kpty_slave_avail(f->pty) > 0;
        default:
            return 0;
    }
}

static int lsys_fd_writable(int fd)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return 0;
    linux_fd_t *f = &linux_fds[fd];
    switch (f->kind) {
        case FD_STD:  return fd >= 1;
        case FD_FILE: return 1;
        case FD_SOCK:
        case FD_PIPE: return ksock_writable(f->sock);
        case FD_INET: return 1;
        case FD_PTY_MASTER: return kpty_master_write_space(f->pty) > 0;
        case FD_PTY_SLAVE:  return kpty_slave_write_space(f->pty) > 0;
        default:      return 0;
    }
}

static int64_t lsys_write(int fd, const void *buf, uint64_t len)
{
    if (!buf || len == 0) return 0;
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];

    if (f->kind == FD_STD && (fd == 1 || fd == 2)) {
        const char *c = (const char*)buf;
        uint64_t n = 0;
        for (uint64_t i = 0; i < len; i++) {
            if (c[i] == 0) break;
            monitor_put(c[i]);
            n++;
        }
        return (int64_t)n;
    }
    if (f->kind == FD_FILE) {
        if (!f->node || !f->node->write) return -9;
        uint32_t written = f->node->write(f->node, (uint32_t)f->pos, (uint32_t)len, (u8int*)buf);
        f->pos += written;
        return (int64_t)written;
    }
    if (f->kind == FD_SOCK || f->kind == FD_PIPE) {
        int sent = ksock_send(f->sock, buf, (uint32_t)len);
        if (sent < 0) return sent;
        if ((uint64_t)sent < len) {
            uint64_t total = (uint64_t)sent;
            uint32_t spins = 0;
            while (total < len && spins < 100000) {
                int more = ksock_send(f->sock, (const uint8_t*)buf + total, (uint32_t)(len - total));
                if (more <= 0) { spins++; yield(); continue; }
                total += (uint64_t)more;
            }
            return (int64_t)total;
        }
        return sent;
    }
    if (f->kind == FD_INET) {
        int sent = ixn_api_send(f->ixn_fd, (const ixn_u8*)buf, (ixn_u32)len, 0);
        if (sent == IXN_E_AGAIN) return -11;
        if (sent < 0) return sent;
        return sent;
    }
    if (f->kind == FD_PTY_MASTER) {
        return kpty_master_write(f->pty, buf, (uint32_t)len);
    }
    if (f->kind == FD_PTY_SLAVE) {
        return kpty_slave_write(f->pty, buf, (uint32_t)len);
    }
    return -9;
}

static int64_t lsys_read(int fd, void *buf, uint64_t len)
{
    if (!buf) return -14;
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];

    if (f->kind == FD_STD && fd == 0) {
        extern int is_buffer_empty(void);
        extern char keyboard_read(void);
        char *c = (char*)buf;
        uint64_t i = 0;
        __asm__ volatile("sti");
        while (i < len - 1) {
            if (!is_buffer_empty()) {
                char ch = keyboard_read();
                if (ch == (char)-1) break;
                c[i++] = ch;
                if (ch == '\n') break;
            } else {
                yield();
            }
        }
        return (int64_t)i;
    }
    if (f->kind == FD_FILE) {
        if (!f->node) return -9;
        uint32_t got = read_fs(f->node, (uint32_t)f->pos, (uint32_t)len, (u8int*)buf);
        f->pos += got;
        return (int64_t)got;
    }
    if (f->kind == FD_SOCK || f->kind == FD_PIPE) {
        int nonblock = (f->flags & O_NONBLOCK) ? 1 : 0;
        int got = ksock_recv(f->sock, buf, (uint32_t)len, nonblock);
        if (got == KSOCK_EAGAIN) return -11;
        if (got < 0) return got;
        if (got == 0 && !nonblock) {
            uint32_t spins = 0;
            while (spins < 200000) {
                got = ksock_recv(f->sock, buf, (uint32_t)len, 1);
                if (got == KSOCK_EAGAIN) { yield(); spins++; continue; }
                if (got < 0) return got;
                return got;
            }
            return -11;
        }
        return got;
    }
    if (f->kind == FD_INET) {
        int got = ixn_api_recv(f->ixn_fd, (ixn_u8*)buf, (ixn_u32)len, 0);
        if (got == IXN_E_AGAIN) return -11;
        if (got < 0) return got;
        return got;
    }
    if (f->kind == FD_PTY_MASTER) {
        return kpty_master_read(f->pty, buf, (uint32_t)len, (f->flags & O_NONBLOCK) ? 1 : 0);
    }
    if (f->kind == FD_PTY_SLAVE) {
        return kpty_slave_read(f->pty, buf, (uint32_t)len, (f->flags & O_NONBLOCK) ? 1 : 0);
    }
    return -9;
}

static int64_t lsys_writev(int fd, const struct linux_iovec *iov, int iovcnt)
{
    if (!iov || iovcnt <= 0) return -22;
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        int64_t n = lsys_write(fd, (const void*)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total > 0 ? total : n;
        total += n;
        if ((uint64_t)n < iov[i].iov_len) break;
    }
    return total;
}

static int64_t lsys_readv(int fd, const struct linux_iovec *iov, int iovcnt)
{
    if (!iov || iovcnt <= 0) return -22;
    int64_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        int64_t n = lsys_read(fd, (void*)(uintptr_t)iov[i].iov_base, iov[i].iov_len);
        if (n < 0) return total > 0 ? total : n;
        total += n;
        if ((uint64_t)n < iov[i].iov_len) break;
    }
    return total;
}

static int64_t lsys_dup(int fd)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_FREE) return -9;
    int nfd = lsys_alloc_fd();
    if (nfd < 0) return -24;
    linux_fds[nfd] = *f;
    return nfd;
}

static int64_t lsys_open(const char *path, int flags, int mode)
{
    (void)mode;
    if (!path) return -14;

    if (path[0] == '/' && path[1] == 'd' && path[2] == 'e' && path[3] == 'v' && path[4] == '/') {
        if (strcmp(path, "/dev/ptmx") == 0 || strcmp(path, "/dev/ptm") == 0) {
            kpty_pair_t *pty = kpty_alloc();
            if (!pty) return -12;
            int fd = lsys_alloc_fd();
            if (fd < 0) { kpty_close_master(pty); kpty_close_slave(pty); return -24; }
            linux_fds[fd].kind = FD_PTY_MASTER;
            linux_fds[fd].pty = pty;
            linux_fds[fd].flags = (uint32_t)flags;
            return fd;
        }
        if (strcmp(path, "/dev/null") == 0) {
            int fd = lsys_alloc_fd();
            if (fd < 0) return -24;
            linux_fds[fd].kind = FD_FILE;
            linux_fds[fd].node = NULL;
            linux_fds[fd].flags = (uint32_t)flags;
            return fd;
        }
        if (strcmp(path, "/dev/zero") == 0) {
            int fd = lsys_alloc_fd();
            if (fd < 0) return -24;
            linux_fds[fd].kind = FD_FILE;
            linux_fds[fd].node = NULL;
            linux_fds[fd].flags = (uint32_t)flags | 0x80000000u;
            return fd;
        }
        if (strncmp_len(path, "/dev/pts/", 9) == 0) {
            uint32_t idx = 0;
            const char *p = path + 9;
            while (*p >= '0' && *p <= '9') { idx = idx * 10 + (uint32_t)(*p - '0'); p++; }
            kpty_pair_t *pty = kpty_by_index(idx);
            if (!pty) return -2;
            int fd = lsys_alloc_fd();
            if (fd < 0) return -24;
            linux_fds[fd].kind = FD_PTY_SLAVE;
            linux_fds[fd].pty = pty;
            linux_fds[fd].flags = (uint32_t)flags;
            return fd;
        }
        if (strcmp(path, "/dev/tty") == 0) {
            return lsys_dup(0);
        }
    }

    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    if (!node) return -2;
    int fd = lsys_alloc_fd();
    if (fd < 0) return -24;
    linux_fds[fd].kind = FD_FILE;
    linux_fds[fd].node = node;
    linux_fds[fd].pos = (flags & O_APPEND) ? node->length : 0;
    linux_fds[fd].flags = (uint32_t)flags;
    return fd;
}

static int strncmp_len(const char *a, const char *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(a[i] - b[i]);
        if (!a[i]) return 0;
    }
    return 0;
}

static int64_t lsys_close(int fd)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_FREE || f->kind == FD_STD) return -9;
    if ((f->kind == FD_SOCK || f->kind == FD_PIPE) && f->sock)
        ksock_close(f->sock);
    if (f->kind == FD_INET)
        ixn_api_close(f->ixn_fd);
    if (f->kind == FD_PTY_MASTER && f->pty)
        kpty_close_master(f->pty);
    if (f->kind == FD_PTY_SLAVE && f->pty)
        kpty_close_slave(f->pty);
    memset(f, 0, sizeof(*f));
    return 0;
}

static int64_t lsys_lseek(int fd, int64_t off, int whence)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind != FD_FILE) return -29;
    int64_t base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)f->pos; break;
        case SEEK_END: base = (int64_t)f->node->length; break;
        default: return -22;
    }
    int64_t np = base + off;
    if (np < 0) return -22;
    f->pos = (uint64_t)np;
    return np;
}

static void lsys_fill_stat(fs_node_t *node, struct linux_stat *st)
{
    memset(st, 0, sizeof(*st));
    if (!node) return;
    st->st_ino = node->inode;
    st->st_size = (int64_t)node->length;
    st->st_blksize = 4096;
    st->st_blocks = (st->st_size + 511) / 512;
    st->st_nlink = 1;
    uint32_t kind = node->flags & 0x7;
    if (kind == FS_DIRECTORY)      st->st_mode = 0040755;
    else if (kind == FS_CHARDEVICE) st->st_mode = 0020666;
    else                            st->st_mode = 0100644;
}

static int64_t lsys_stat(const char *path, struct linux_stat *st)
{
    if (!path || !st) return -14;
    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    if (!node) return -2;
    lsys_fill_stat(node, st);
    return 0;
}

static int64_t lsys_fstat(int fd, struct linux_stat *st)
{
    if (!st) return -14;
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_STD) {
        memset(st, 0, sizeof(*st));
        st->st_mode = 0020666;
        st->st_blksize = 4096;
        return 0;
    }
    if (f->kind == FD_SOCK || f->kind == FD_PIPE) {
        memset(st, 0, sizeof(*st));
        st->st_mode = 0140777;
        st->st_blksize = 4096;
        return 0;
    }
    if (f->kind != FD_FILE) return -9;
    lsys_fill_stat(f->node, st);
    return 0;
}

static int64_t lsys_brk(uint64_t addr)
{
    vas_t *as = task_current_vas();
    if (!as) return addr;
    if (as->brk_base == 0) {
        as->brk_base = 0x0000000050000000ull;
        as->brk_cur = as->brk_base;
        vmm_vma_alloc(as, as->brk_base, 0x1000, VMA_READ | VMA_WRITE | VMA_USER | VMA_ANON);
    }
    if (addr == 0) return (int64_t)as->brk_cur;
    if (addr < as->brk_base) return (int64_t)as->brk_cur;
    uint64_t max = as->brk_base + 256ull * 1024 * 1024;
    if (addr > max) return (int64_t)as->brk_cur;

    uint64_t old_end = (as->brk_cur + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
    uint64_t new_end = (addr + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);

    if (new_end > old_end) {
        if (pmm_user_oom()) return (int64_t)as->brk_cur;
        vmm_vma_alloc(as, old_end, new_end - old_end, VMA_READ | VMA_WRITE | VMA_USER | VMA_ANON);
    } else if (new_end < old_end) {
        vmm_vma_free_range(as, new_end, old_end - new_end);
    }
    as->brk_cur = addr;
    return (int64_t)as->brk_cur;
}

static uint64_t lsys_mmap(uint64_t addr, uint64_t len, int prot, int flags, int fd, uint64_t off)
{
    (void)fd; (void)off;
    vas_t *as = task_current_vas();
    if (!as || len == 0) return (uint64_t)-22;
    len = (len + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);

    uint32_t vf = VMA_USER | VMA_ANON;
    if (prot & PROT_READ)  vf |= VMA_READ;
    if (prot & PROT_WRITE) vf |= VMA_WRITE;
    if (prot & PROT_EXEC)  vf |= VMA_EXEC;

    uint64_t base;
    if ((flags & MAP_FIXED) && addr) {
        base = addr & ~(VMM_PAGE_SIZE - 1);
    } else {
        base = as->mmap_base;
        as->mmap_base -= len + VMM_PAGE_SIZE;
    }
    if (!vmm_vma_alloc(as, base, len, vf)) return (uint64_t)-12;
    return base;
}

static int64_t lsys_munmap(uint64_t addr, uint64_t len)
{
    vas_t *as = task_current_vas();
    if (!as) return -22;
    if (len == 0) return -22;
    vmm_vma_free_range(as, addr, len);
    return 0;
}

static int64_t lsys_mprotect(uint64_t addr, uint64_t len, int prot)
{
    vas_t *as = task_current_vas();
    if (!as) return -22;
    uint32_t vf = VMA_USER | VMA_ANON;
    if (prot & PROT_READ)  vf |= VMA_READ;
    if (prot & PROT_WRITE) vf |= VMA_WRITE;
    if (prot & PROT_EXEC)  vf |= VMA_EXEC;
    return vmm_vma_protect(as, addr, len, vf);
}

static void lsys_str_copy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static int64_t lsys_uname(struct linux_utsname *u)
{
    if (!u) return -14;
    memset(u, 0, sizeof(*u));
    lsys_str_copy(u->sysname, "ArtyomX", 65);
    lsys_str_copy(u->nodename, "artyomx", 65);
    lsys_str_copy(u->release, "6.9.0-ax", 65);
    lsys_str_copy(u->version, "#1 SMP PREEMPT ArtyomX", 65);
    lsys_str_copy(u->machine, "x86_64", 65);
    lsys_str_copy(u->domainname, "(none)", 65);
    return 0;
}

static int64_t lsys_nanosleep(const struct linux_timespec *req, struct linux_timespec *rem)
{
    (void)rem;
    if (!req) return -14;
    uint64_t ms = (uint64_t)req->tv_sec * 1000ull + (uint64_t)req->tv_nsec / 1000000ull;
    uint32_t freq = timer_get_frequency();
    uint32_t ticks = (uint32_t)((ms * freq) / 1000ull);
    if (ticks == 0) ticks = 1;
    task_sleep_ticks(ticks);
    return 0;
}

static int64_t lsys_getpid(void) { return task_current_pid(); }
static int64_t lsys_getppid(void)
{
    Task *t = task_current();
    return t ? t->ppid : 0;
}

static int64_t lsys_socket(int domain, int type, int protocol)
{
    (void)protocol;
    if (domain == AF_INET) {
        if (type != SOCK_STREAM && type != SOCK_DGRAM) return -94;
        ixn_fd_t ifd = ixn_api_socket(IXN_AF_INET, (ixn_u32)type, 0);
        if (ifd < 0) return ifd;
        int fd = lsys_alloc_fd();
        if (fd < 0) { ixn_api_close(ifd); return -24; }
        linux_fds[fd].kind = FD_INET;
        linux_fds[fd].ixn_fd = ifd;
        return fd;
    }
    if (domain != AF_UNIX && domain != AF_LOCAL) return -97;
    if (type != SOCK_STREAM && type != SOCK_DGRAM) return -94;
    int fd = lsys_alloc_fd();
    if (fd < 0) return -24;
    ksock_t *s = ksock_create();
    if (!s) return -12;
    linux_fds[fd].kind = FD_SOCK;
    linux_fds[fd].sock = s;
    return fd;
}

static int64_t lsys_bind(int fd, const void *addr, uint32_t addrlen)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_INET) {
        if (!addr || addrlen < 8) return -22;
        const ixn_sockaddr_in_t *sa = (const ixn_sockaddr_in_t*)addr;
        return ixn_api_bind(f->ixn_fd, sa);
    }
    if (f->kind != FD_SOCK) return -9;
    if (!addr || addrlen < 3) return -22;
    const uint8_t *a = (const uint8_t*)addr;
    uint16_t family = (uint16_t)(a[0] | (a[1] << 8));
    if (family != AF_UNIX) return -97;
    char path[112];
    uint32_t plen = addrlen - 2;
    if (plen > 107) plen = 107;
    for (uint32_t i = 0; i < plen; i++) path[i] = (char)a[2 + i];
    path[plen] = 0;
    int r = ksock_bind(f->sock, path);
    if (r != 0) return -98;
    for (uint32_t i = 0; i <= plen; i++) f->path[i] = path[i];
    return 0;
}

static int64_t lsys_listen(int fd, int backlog)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_INET)
        return ixn_api_listen(f->ixn_fd, (ixn_u32)backlog);
    if (f->kind != FD_SOCK) return -9;
    return ksock_listen(f->sock, backlog);
}

static int64_t lsys_accept(int fd, void *addr, uint32_t *addrlen)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_INET) {
        ixn_sockaddr_in_t peer;
        ixn_fd_t nifd = ixn_api_accept(f->ixn_fd, addr ? &peer : NULL);
        if (nifd < 0) return nifd;
        int nfd = lsys_alloc_fd();
        if (nfd < 0) { ixn_api_close(nifd); return -24; }
        linux_fds[nfd].kind = FD_INET;
        linux_fds[nfd].ixn_fd = nifd;
        if (addr && addrlen) {
            memcpy(addr, &peer, sizeof(peer) < *addrlen ? sizeof(peer) : *addrlen);
            *addrlen = sizeof(peer);
        }
        return nfd;
    }
    if (f->kind != FD_SOCK) return -9;
    if (addr && addrlen) {
        uint8_t *a = (uint8_t*)addr;
        a[0] = AF_UNIX; a[1] = 0;
        *addrlen = 2;
    }
    ksock_t *client = ksock_accept(f->sock);
    if (!client) {
        if (f->flags & O_NONBLOCK) return -11;
        uint32_t spins = 0;
        while (spins < 100000) {
            client = ksock_accept(f->sock);
            if (client) break;
            yield();
            spins++;
        }
        if (!client) return -11;
    }
    int nfd = lsys_alloc_fd();
    if (nfd < 0) { ksock_close(client); return -24; }
    linux_fds[nfd].kind = FD_SOCK;
    linux_fds[nfd].sock = client;
    return nfd;
}

static int64_t lsys_connect(int fd, const void *addr, uint32_t addrlen)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_INET) {
        if (!addr || addrlen < 8) return -22;
        const ixn_sockaddr_in_t *sa = (const ixn_sockaddr_in_t*)addr;
        return ixn_api_connect(f->ixn_fd, sa);
    }
    if (f->kind != FD_SOCK) return -9;
    if (!addr || addrlen < 3) return -22;
    const uint8_t *a = (const uint8_t*)addr;
    uint16_t family = (uint16_t)(a[0] | (a[1] << 8));
    if (family != AF_UNIX) return -97;
    char path[112];
    uint32_t plen = addrlen - 2;
    if (plen > 107) plen = 107;
    for (uint32_t i = 0; i < plen; i++) path[i] = (char)a[2 + i];
    path[plen] = 0;
    if (path[0] == 0 && plen > 0) {
        for (uint32_t i = 0; i < plen; i++) path[i] = path[i + 1];
    }
    ksock_t *conn = ksock_connect(path);
    if (!conn) return -111;
    ksock_close(f->sock);
    f->sock = conn;
    return 0;
}

static int64_t lsys_shutdown(int fd, int how)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind != FD_SOCK && f->kind != FD_PIPE) return -9;
    return ksock_shutdown(f->sock, how);
}

static int64_t lsys_sendto(int fd, const void *buf, uint64_t len, int flags,
                           const void *dest, uint32_t dlen)
{
    (void)flags; (void)dest; (void)dlen;
    return lsys_write(fd, buf, len);
}

static int64_t lsys_recvfrom(int fd, void *buf, uint64_t len, int flags,
                             void *src, uint32_t *slen)
{
    (void)flags; (void)src; (void)slen;
    return lsys_read(fd, buf, len);
}

static int64_t lsys_setsockopt(int fd, int level, int opt, const void *val, uint32_t vlen)
{
    (void)fd; (void)level; (void)opt; (void)val; (void)vlen;
    return 0;
}

static int64_t lsys_getsockopt(int fd, int level, int opt, void *val, uint32_t *vlen)
{
    (void)fd; (void)level; (void)opt; (void)val; (void)vlen;
    return 0;
}

static int64_t lsys_getsockname(int fd, void *addr, uint32_t *addrlen)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (!addr || !addrlen) return -22;
    uint8_t *a = (uint8_t*)addr;
    a[0] = AF_UNIX; a[1] = 0;
    uint32_t plen = 0;
    while (f->path[plen] && plen < 107) plen++;
    uint32_t cap = *addrlen;
    if (cap < 2 + plen) plen = cap > 2 ? cap - 2 : 0;
    for (uint32_t i = 0; i < plen; i++) a[2 + i] = (uint8_t)f->path[i];
    *addrlen = 2 + plen;
    return 0;
}

static int64_t lsys_getpeername(int fd, void *addr, uint32_t *addrlen)
{
    return lsys_getsockname(fd, addr, addrlen);
}

static int64_t lsys_poll(struct linux_pollfd *fds, uint32_t nfds, int timeout_ms)
{
    if (!fds || nfds == 0) return -22;
    uint32_t waited = 0;
    uint32_t freq = timer_get_frequency();
    uint32_t max_ticks = timeout_ms > 0 ? (uint32_t)((uint64_t)timeout_ms * freq / 1000ull) : 0;

    for (;;) {
        int ready = 0;
        for (uint32_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
            int fd = fds[i].fd;
            if (fd < 0) continue;
            if (fd >= (int)LINUX_FD_MAX) { fds[i].revents = POLLNVAL; ready++; continue; }
            int16_t ev = fds[i].events;
            if ((ev & POLLIN) && lsys_fd_readable(fd)) { fds[i].revents |= POLLIN; }
            if ((ev & POLLOUT) && lsys_fd_writable(fd)) { fds[i].revents |= POLLOUT; }
            if (fds[i].revents) ready++;
        }
        if (ready > 0) return ready;
        if (timeout_ms == 0) return 0;
        if (timeout_ms > 0 && waited >= max_ticks) return 0;
        task_sleep_ticks(1);
        waited++;
    }
}

static int64_t lsys_select(int nfds, uint64_t *readfds, uint64_t *writefds,
                           uint64_t *exceptfds, const struct linux_timespec *timeout)
{
    (void)exceptfds;
    int ready = 0;
    uint32_t waited = 0;
    uint32_t max_ticks = 0;
    if (timeout) {
        uint32_t freq = timer_get_frequency();
        uint64_t ms = (uint64_t)timeout->tv_sec * 1000ull + (uint64_t)timeout->tv_nsec / 1000000ull;
        max_ticks = (uint32_t)(ms * freq / 1000ull);
    }
    for (;;) {
        ready = 0;
        for (int fd = 0; fd < nfds && fd < 64; fd++) {
            if (readfds && (readfds[fd / 64] & (1ull << (fd % 64)))) {
                if (lsys_fd_readable(fd)) ready++;
            }
            if (writefds && (writefds[fd / 64] & (1ull << (fd % 64)))) {
                if (lsys_fd_writable(fd)) ready++;
            }
        }
        if (ready > 0) return ready;
        if (!timeout || waited >= max_ticks) return 0;
        task_sleep_ticks(1);
        waited++;
    }
}

static int64_t lsys_pipe2(int *fds, int flags)
{
    (void)flags;
    if (!fds) return -14;
    ksock_t *a, *b;
    if (ksock_pipe(&a, &b) != 0) return -12;
    int fda = lsys_alloc_fd();
    if (fda < 0) { ksock_close(a); ksock_close(b); return -24; }
    int fdb = lsys_alloc_fd();
    if (fdb < 0) { linux_fds[fda].kind = FD_FREE; ksock_close(a); ksock_close(b); return -24; }
    linux_fds[fda].kind = FD_PIPE;
    linux_fds[fda].sock = a;
    linux_fds[fdb].kind = FD_PIPE;
    linux_fds[fdb].sock = b;
    fds[0] = fda;
    fds[1] = fdb;
    return 0;
}


static int64_t lsys_dup2(int oldfd, int newfd)
{
    if (oldfd < 0 || oldfd >= (int)LINUX_FD_MAX) return -9;
    if (newfd < 0 || newfd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *of = &linux_fds[oldfd];
    if (of->kind == FD_FREE) return -9;
    if (oldfd == newfd) return newfd;
    linux_fd_t *nf = &linux_fds[newfd];
    if (nf->kind == FD_SOCK || nf->kind == FD_PIPE) {
        if (nf->sock) ksock_close(nf->sock);
    }
    linux_fds[newfd] = *of;
    return newfd;
}

static int64_t lsys_fcntl(int fd, int cmd, uint64_t arg)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    switch (cmd) {
        case 1: return 0;
        case 2: return 0;
        case 3: return (int64_t)f->flags;
        case 4:
            f->flags = (uint32_t)arg;
            return 0;
        default: return 0;
    }
}

static int64_t lsys_ioctl(int fd, uint64_t req, uint64_t arg)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind == FD_PTY_MASTER || f->kind == FD_PTY_SLAVE) {
        return kpty_ioctl(f->pty, req, (void*)(uintptr_t)arg, f->kind == FD_PTY_MASTER);
    }
    if (fd == 0 || fd == 1 || fd == 2) {
        if (req == 0x5401 || req == 0x5413) {
            uint16_t *w = (uint16_t*)(uintptr_t)arg;
            if (w) {
                w[0] = 25; w[1] = 80; w[2] = 0; w[3] = 0;
            }
            return 0;
        }
        return 0;
    }
    return -25;
}

#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_MAX_WAITERS 32u

typedef struct {
    uint64_t addr;
    int      tid;
    uint8_t  used;
} futex_waiter_t;

static futex_waiter_t futex_waiters[FUTEX_MAX_WAITERS];

static int64_t lsys_futex(uint32_t *uaddr, int op, uint32_t val,
                          const struct linux_timespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
    (void)uaddr2; (void)val3;
    if (!uaddr) return -14;
    int cmd = op & 0x7F;
    uint64_t addr = (uint64_t)(uintptr_t)uaddr;

    if (cmd == FUTEX_WAIT || cmd == FUTEX_WAIT_BITSET) {
        if (*uaddr != val) return -11;
        uint32_t ticks = 1;
        if (timeout) {
            uint64_t ms = (uint64_t)timeout->tv_sec * 1000ull + (uint64_t)timeout->tv_nsec / 1000000ull;
            uint32_t freq = timer_get_frequency();
            ticks = (uint32_t)(ms * freq / 1000ull);
            if (ticks == 0) ticks = 1;
        }
        for (uint32_t i = 0; i < FUTEX_MAX_WAITERS; i++) {
            if (!futex_waiters[i].used) {
                futex_waiters[i].used = 1;
                futex_waiters[i].addr = addr;
                futex_waiters[i].tid = task_current_pid();
                break;
            }
        }
        task_sleep_ticks(ticks);
        for (uint32_t i = 0; i < FUTEX_MAX_WAITERS; i++) {
            if (futex_waiters[i].used && futex_waiters[i].tid == task_current_pid() && futex_waiters[i].addr == addr) {
                futex_waiters[i].used = 0;
            }
        }
        return 0;
    }
    if (cmd == FUTEX_WAKE || cmd == FUTEX_WAKE_BITSET) {
        int woken = 0;
        for (uint32_t i = 0; i < FUTEX_MAX_WAITERS && woken < (int)val; i++) {
            if (futex_waiters[i].used && futex_waiters[i].addr == addr) {
                futex_waiters[i].used = 0;
                woken++;
            }
        }
        return woken;
    }
    return 0;
}

static int64_t lsys_sched_yield(void)
{
    yield();
    return 0;
}

static int64_t lsys_getuid(void)  { return 0; }
static int64_t lsys_geteuid(void) { return 0; }
static int64_t lsys_getgid(void)  { return 0; }
static int64_t lsys_getegid(void) { return 0; }
static int64_t lsys_gettid(void)  { return task_current_pid(); }

static int64_t lsys_arch_prctl(int code, uint64_t addr)
{
    if (code == 0x1002) {
        task_set_tls(addr);
        return 0;
    }
    if (code == 0x1003) {
        uint64_t *out = (uint64_t*)(uintptr_t)addr;
        if (out) *out = task_get_tls();
        return 0;
    }
    (void)addr;
    return -22;
}

static int64_t lsys_getdents64(int fd, void *dirp, uint32_t count)
{
    if (fd < 0 || fd >= (int)LINUX_FD_MAX) return -9;
    linux_fd_t *f = &linux_fds[fd];
    if (f->kind != FD_FILE || !f->node) return -9;
    if (!dirp) return -14;

    uint8_t *out = (uint8_t*)dirp;
    uint32_t written = 0;
    uint32_t idx = (uint32_t)f->pos;
    for (uint32_t i = 0; i < 64 && written < count; i++) {
        struct dirent *de = readdir_fs(f->node, idx + i);
        if (!de) break;
        uint32_t nlen = 0;
        while (de->name[nlen] && nlen < 255) nlen++;
        uint16_t reclen = (uint16_t)(19 + nlen + 1 + 7);
        reclen &= ~7u;
        if (written + reclen > count) break;
        uint64_t *ino = (uint64_t*)(out + written);
        uint64_t *off = (uint64_t*)(out + written + 8);
        uint16_t *rec = (uint16_t*)(out + written + 16);
        uint8_t *typ = (uint8_t*)(out + written + 18);
        char *nm = (char*)(out + written + 19);
        *ino = de->ino;
        *off = idx + i + 1;
        *rec = reclen;
        *typ = 8;
        for (uint32_t k = 0; k <= nlen; k++) nm[k] = de->name[k];
        written += reclen;
    }
    f->pos += written > 0 ? 1 : 0;
    return (int64_t)written;
}

static int64_t lsys_getcwd(char *buf, uint64_t size)
{
    if (!buf || size < 2) return -22;
    buf[0] = '/';
    buf[1] = 0;
    return 2;
}

static int64_t lsys_access(const char *path, int mode)
{
    (void)mode;
    if (!path) return -14;
    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    return node ? 0 : -2;
}

static int64_t lsys_readlink(const char *path, char *buf, uint64_t bufsiz)
{
    (void)path; (void)buf; (void)bufsiz;
    return -22;
}

static int64_t lsys_chdir(const char *path)
{
    (void)path;
    return 0;
}

static int64_t lsys_gettimeofday(void *tv, void *tz)
{
    (void)tz;
    if (tv) {
        uint64_t *t = (uint64_t*)tv;
        t[0] = (uint64_t)tick * 10ull / 1000ull + 1700000000ull;
        t[1] = ((uint64_t)tick % 100ull) * 10000ull;
    }
    return 0;
}

static int64_t lsys_clock_gettime(int clk, struct linux_timespec *ts)
{
    (void)clk;
    if (!ts) return -14;
    uint32_t freq = timer_get_frequency();
    ts->tv_sec = (int64_t)(tick / freq);
    ts->tv_nsec = (int64_t)((tick % freq) * (1000000000ull / freq));
    return 0;
}

static int64_t lsys_times(void *buf)
{
    if (buf) {
        uint64_t *t = (uint64_t*)buf;
        t[0] = tick; t[1] = 0; t[2] = 0; t[3] = 0;
    }
    return (int64_t)tick;
}

static int64_t lsys_sysinfo(void *buf)
{
    if (!buf) return -14;
    memset(buf, 0, 112);
    int64_t *b = (int64_t*)buf;
    b[0] = (int64_t)(tick / timer_get_frequency());
    uint64_t total = pmm_total_frames() * 4096ull;
    uint64_t freef = pmm_free_frames_count() * 4096ull;
    memcpy((char*)buf + 32, &total, 8);
    memcpy((char*)buf + 40, &freef, 8);
    return 0;
}

static int64_t lsys_sched_getaffinity(int pid, uint32_t cpusetsize, uint64_t *mask)
{
    (void)pid;
    if (!mask || cpusetsize < 8) return -22;
    *mask = 1;
    return 8;
}

static int64_t lsys_sched_setaffinity(int pid, uint32_t cpusetsize, const uint64_t *mask)
{
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
}

static int64_t lsys_sysconf(int name)
{
    if (name == 84) return 1;
    if (name == 30) return 4096;
    if (name == 2) return 4096;
    if (name == 26) return 100;
    if (name == 6) return 4096;
    return -22;
}

static int64_t lsys_getrlimit2(int resource, void *rlim)
{
    if (!rlim) return -14;
    uint64_t *l = (uint64_t*)rlim;
    if (resource == 3) {
        l[0] = 2 * 1024 * 1024;
        l[1] = ~0ull;
    } else if (resource == 7) {
        l[0] = 1024;
        l[1] = 4096;
    } else if (resource == 9) {
        l[0] = 64 * 1024 * 1024;
        l[1] = ~0ull;
    } else {
        l[0] = ~0ull;
        l[1] = ~0ull;
    }
    return 0;
}

static int64_t lsys_set_tid_address(int *tidptr)
{
    (void)tidptr;
    return task_current_pid();
}

static int64_t lsys_set_robust_list(void *head, uint64_t len)
{
    (void)head; (void)len;
    return 0;
}

static int64_t lsys_prlimit64(int pid, int resource, const void *new_lim, void *old_lim)
{
    (void)pid; (void)resource; (void)new_lim;
    if (old_lim) {
        uint64_t *l = (uint64_t*)old_lim;
        l[0] = ~0ull;
        l[1] = ~0ull;
    }
    return 0;
}

static int64_t lsys_getrandom(void *buf, uint64_t len, uint32_t flags)
{
    (void)flags;
    if (!buf) return -14;
    uint8_t *p = (uint8_t*)buf;
    uint32_t seed = tick * 1103515245u + 12345u;
    for (uint64_t i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
    return (int64_t)len;
}

static int64_t lsys_madvise(uint64_t addr, uint64_t len, int advice)
{
    (void)addr; (void)len; (void)advice;
    return 0;
}

static int64_t lsys_mlock(uint64_t addr, uint64_t len)
{
    (void)addr; (void)len;
    return 0;
}

static int64_t lsys_msync(uint64_t addr, uint64_t len, int flags)
{
    (void)addr; (void)len; (void)flags;
    return 0;
}

static int64_t lsys_fsync(int fd)
{
    (void)fd;
    return 0;
}

static int64_t lsys_fdatasync(int fd)
{
    (void)fd;
    return 0;
}

static int64_t lsys_truncate(const char *path, int64_t len)
{
    (void)path; (void)len;
    return 0;
}

static int64_t lsys_ftruncate(int fd, int64_t len)
{
    (void)fd; (void)len;
    return 0;
}

static int64_t lsys_unlink(const char *path)
{
    (void)path;
    return -1;
}

static int64_t lsys_rename(const char *oldp, const char *newp)
{
    (void)oldp; (void)newp;
    return -1;
}

static int64_t lsys_mkdir(const char *path, int mode)
{
    (void)path; (void)mode;
    return -1;
}

static int64_t lsys_rmdir(const char *path)
{
    (void)path;
    return -1;
}

static int64_t lsys_symlink(const char *target, const char *linkpath)
{
    (void)target; (void)linkpath;
    return -1;
}

static int64_t lsys_chmod(const char *path, int mode)
{
    (void)path; (void)mode;
    return 0;
}

static int64_t lsys_fchmod(int fd, int mode)
{
    (void)fd; (void)mode;
    return 0;
}

static int64_t lsys_chown(const char *path, int uid, int gid)
{
    (void)path; (void)uid; (void)gid;
    return 0;
}

static int64_t lsys_execve(const char *path, uint64_t argv, uint64_t envp)
{
    (void)argv; (void)envp;
    if (!path) return -14;
    extern int linux_exec_elf(const char *path);
    int r = linux_exec_elf(path);
    if (r < 0) return -2;
    task_exit_code(0);
    __builtin_unreachable();
}

static int64_t lsys_fork(void)
{
    int pid = task_fork_current();
    if (pid < 0) return pid;
    return pid;
}

static int64_t lsys_vfork(void)
{
    return lsys_fork();
}

#define CLONE_VM             0x00000100ull
#define CLONE_FS             0x00000200ull
#define CLONE_FILES          0x00000400ull
#define CLONE_SIGHAND        0x00000800ull
#define CLONE_THREAD         0x00010000ull
#define CLONE_SETTLS         0x00080000ull
#define CLONE_CHILD_CLEARTID 0x00200000ull
#define CLONE_CHILD_SETTID   0x01000000ull

static int64_t lsys_clone(uint64_t flags, uint64_t stack, void *ptid, void *ctid, uint64_t tls)
{
    if (flags & CLONE_THREAD) {
        if (!stack) return -22;
        int tid = task_clone_thread(flags, stack, tls, ctid);
        if (tid < 0) return tid;
        if (ptid) *(int*)ptid = tid;
        return tid;
    }
    int pid = task_fork_current();
    if (pid < 0) return pid;
    if (flags & CLONE_SETTLS)
        task_set_tls(tls);
    if ((flags & CLONE_CHILD_SETTID) && ctid)
        *(int*)ctid = pid;
    if (ptid)
        *(int*)ptid = pid;
    if (stack) {
        Task *t = task_by_pid(pid);
        if (t) t->regs.rsp = stack;
    }
    return pid;
}

static int64_t lsys_wait4(int pid, int *status, int options, void *rusage)
{
    (void)pid; (void)status; (void)options; (void)rusage;
    return -10;
}

static int64_t lsys_waitid(int idtype, int pid, void *infop, int options, void *rusage)
{
    (void)idtype; (void)pid; (void)infop; (void)options; (void)rusage;
    return -10;
}

static int64_t lsys_kill(int pid, int sig)
{
    (void)sig;
    return task_kill(pid);
}

static int64_t lsys_rt_sigaction(int sig, const void *act, void *oldact, uint64_t sigsetsize)
{
    (void)sigsetsize;
    if (sig < 1 || sig > 64) return -22;
    Task *t = task_current();
    if (!t) return -22;
    if (oldact) {
        struct { void *handler; uint64_t flags; void *restorer; uint64_t mask; } *oa = oldact;
        oa->handler = t->signal_handlers[sig];
        oa->flags = 0;
        oa->restorer = 0;
        oa->mask = t->signal_blocked;
    }
    if (act) {
        const struct { void *handler; uint64_t flags; void *restorer; uint64_t mask; } *a = act;
        t->signal_handlers[sig] = a->handler;
    }
    return 0;
}

static int64_t lsys_rt_sigprocmask(int how, const uint64_t *set, uint64_t *oldset, uint64_t sigsetsize)
{
    (void)sigsetsize;
    Task *t = task_current();
    if (!t) return -22;
    if (oldset) *oldset = t->signal_blocked;
    if (set) {
        if (how == 0) t->signal_blocked |= *set;
        else if (how == 1) t->signal_blocked &= ~*set;
        else if (how == 2) t->signal_blocked = *set;
    }
    return 0;
}

static int64_t lsys_rt_sigreturn(void)
{
    task_signal_return();
    return 0;
}

static int64_t lsys_tgkill(int tgid, int tid, int sig)
{
    (void)tgid;
    return task_signal_send(tid, sig);
}

static int64_t lsys_tkill(int tid, int sig)
{
    return task_signal_send(tid, sig);
}

static int64_t lsys_sigaltstack(const void *ss, void *old_ss)
{
    Task *t = task_current();
    if (!t) return -22;
    if (old_ss) {
        struct { void *sp; int32_t flags; uint64_t size; } *o = old_ss;
        o->sp = (void*)t->sigaltstack_sp;
        o->flags = (int32_t)t->sigaltstack_flags;
        o->size = t->sigaltstack_size;
    }
    if (ss) {
        const struct { void *sp; int32_t flags; uint64_t size; } *s = ss;
        t->sigaltstack_sp = (uint64_t)s->sp;
        t->sigaltstack_flags = (uint32_t)s->flags;
        t->sigaltstack_size = s->size;
    }
    return 0;
}

static int64_t lsys_sigaction(int sig, const void *act, void *oldact)
{
    return lsys_rt_sigaction(sig, act, oldact, 8);
}

static int64_t lsys_sigprocmask(int how, const void *set, void *oldset)
{
    return lsys_rt_sigprocmask(how, set, oldset, 8);
}

static int64_t lsys_sigreturn(void)
{
    return lsys_rt_sigreturn();
}

static int64_t lsys_pause(void)
{
    for (;;) task_sleep_ticks(100);
}

static int64_t lsys_alarm(uint32_t seconds)
{
    (void)seconds;
    return 0;
}
static int64_t lsys_timer_create(int clockid, void *sevp, int *timerid)
{
    (void)clockid; (void)sevp;
    static int next_timer = 1;
    if (timerid) *timerid = next_timer++;
    return 0;
}

static int64_t lsys_timer_settime(int timerid, int flags, const void *newv, void *oldv)
{
    (void)timerid; (void)flags; (void)newv; (void)oldv;
    return 0;
}

static int64_t lsys_timer_delete(int timerid)
{
    (void)timerid;
    return 0;
}

static int64_t lsys_get_mempolicy(int *policy, uint64_t *nmask, uint64_t maxnode, uint64_t addr, uint64_t flags)
{
    (void)addr; (void)flags;
    if (policy) *policy = 0;
    if (nmask) *nmask = 0;
    (void)maxnode;
    return 0;
}

static int64_t lsys_set_mempolicy(int mode, const uint64_t *nmask, uint64_t maxnode)
{
    (void)mode; (void)nmask; (void)maxnode;
    return 0;
}

static int64_t lsys_mbind(uint64_t addr, uint64_t len, int mode, const uint64_t *nmask, uint64_t maxnode, uint64_t flags)
{
    (void)addr; (void)len; (void)mode; (void)nmask; (void)maxnode; (void)flags;
    return 0;
}


static int64_t lsys_setrlimit(int resource, const void *rlim)
{
    (void)resource; (void)rlim;
    return 0;
}

static int64_t lsys_getrusage(int who, void *usage)
{
    (void)who;
    if (usage) memset(usage, 0, 144);
    return 0;
}

static int64_t lsys_umask(int mask)
{
    (void)mask;
    return 0;
}

static int64_t lsys_statfs(const char *path, void *buf)
{
    (void)path;
    if (!buf) return -14;
    memset(buf, 0, 120);
    uint64_t *b = (uint64_t*)buf;
    b[0] = 0x01021997;
    b[1] = 4096;
    return 0;
}

static int64_t lsys_fstatfs(int fd, void *buf)
{
    (void)fd;
    return lsys_statfs("/", buf);
}

static int64_t lsys_getpriority(int which, int who)
{
    (void)which; (void)who;
    return 0;
}

static int64_t lsys_setpriority(int which, int who, int prio)
{
    (void)which; (void)who; (void)prio;
    return 0;
}

static int64_t lsys_getpgid(int pid)
{
    (void)pid;
    return task_current_pid();
}

static int64_t lsys_setpgid(int pid, int pgid)
{
    (void)pid; (void)pgid;
    return 0;
}

static int64_t lsys_getsid(int pid)
{
    (void)pid;
    return task_current_pid();
}

static int64_t lsys_setsid(void)
{
    return task_current_pid();
}

static int64_t lsys_getgroups(int size, int *list)
{
    (void)size; (void)list;
    return 0;
}

static int64_t lsys_setgroups(int size, const int *list)
{
    (void)size; (void)list;
    return 0;
}

static int64_t lsys_personality(uint64_t pers)
{
    (void)pers;
    return 0;
}

static int64_t lsys_prctl(int option, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)option; (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}

static int64_t lsys_syslog(int type, char *buf, int len)
{
    (void)type; (void)buf; (void)len;
    return 0;
}

static int64_t lsys_getcpu(uint32_t *cpu, uint32_t *node, void *tcache)
{
    (void)tcache;
    if (cpu) *cpu = 0;
    if (node) *node = 0;
    return 0;
}

static int64_t lsys_eventfd2(uint32_t initval, int flags)
{
    (void)initval; (void)flags;
    ksock_t *a, *b;
    if (ksock_pipe(&a, &b) != 0) return -12;
    int fd = lsys_alloc_fd();
    if (fd < 0) { ksock_close(a); ksock_close(b); return -24; }
    linux_fds[fd].kind = FD_PIPE;
    linux_fds[fd].sock = a;
    ksock_close(b);
    return fd;
}

static int64_t lsys_signalfd4(int fd, const void *mask, uint64_t sizemask, int flags)
{
    (void)fd; (void)mask; (void)sizemask; (void)flags;
    return -38;
}

static int64_t lsys_timerfd_create(int clockid, int flags)
{
    (void)clockid; (void)flags;
    ksock_t *a, *b;
    if (ksock_pipe(&a, &b) != 0) return -12;
    int fd = lsys_alloc_fd();
    if (fd < 0) { ksock_close(a); ksock_close(b); return -24; }
    linux_fds[fd].kind = FD_PIPE;
    linux_fds[fd].sock = a;
    ksock_close(b);
    return fd;
}

static int64_t lsys_epoll_create1(int flags)
{
    (void)flags;
    int fd = lsys_alloc_fd();
    if (fd < 0) return -24;
    linux_fds[fd].kind = FD_PIPE;
    linux_fds[fd].sock = NULL;
    return fd;
}

static int64_t lsys_epoll_ctl(int epfd, int op, int fd, void *event)
{
    (void)epfd; (void)op; (void)fd; (void)event;
    return 0;
}

static int64_t lsys_epoll_wait(int epfd, void *events, int maxevents, int timeout)
{
    (void)epfd; (void)events; (void)maxevents;
    if (timeout > 0) {
        uint32_t freq = timer_get_frequency();
        task_sleep_ticks((uint32_t)((uint64_t)timeout * freq / 1000ull));
    }
    return 0;
}

static int64_t lsys_pselect6(int nfds, uint64_t *rfds, uint64_t *wfds, uint64_t *efds,
                             const struct linux_timespec *ts, const void *sigmask)
{
    (void)sigmask;
    return lsys_select(nfds, rfds, wfds, efds, ts);
}

static int64_t lsys_ppoll(struct linux_pollfd *fds, uint32_t nfds,
                          const struct linux_timespec *ts, const void *sigmask, uint64_t sigsetsize)
{
    (void)sigmask; (void)sigsetsize;
    int timeout = -1;
    if (ts) timeout = (int)(ts->tv_sec * 1000 + ts->tv_nsec / 1000000);
    return lsys_poll(fds, nfds, timeout);
}

static int64_t lsys_sendmsg(int fd, const void *msg, int flags)
{
    (void)flags;
    if (!msg) return -14;
    const uint64_t *m = (const uint64_t*)msg;
    const struct linux_iovec *iov = (const struct linux_iovec*)(uintptr_t)m[2];
    int iovcnt = (int)m[3];
    return lsys_writev(fd, iov, iovcnt);
}

static int64_t lsys_recvmsg(int fd, void *msg, int flags)
{
    (void)flags;
    if (!msg) return -14;
    uint64_t *m = (uint64_t*)msg;
    struct linux_iovec *iov = (struct linux_iovec*)(uintptr_t)m[2];
    int iovcnt = (int)m[3];
    return lsys_readv(fd, iov, iovcnt);
}

static int64_t lsys_socketpair(int domain, int type, int protocol, int *sv)
{
    (void)domain; (void)type; (void)protocol;
    if (!sv) return -14;
    ksock_t *a, *b;
    if (ksock_pipe(&a, &b) != 0) return -12;
    int fda = lsys_alloc_fd();
    if (fda < 0) { ksock_close(a); ksock_close(b); return -24; }
    int fdb = lsys_alloc_fd();
    if (fdb < 0) { linux_fds[fda].kind = FD_FREE; ksock_close(a); ksock_close(b); return -24; }
    linux_fds[fda].kind = FD_SOCK;
    linux_fds[fda].sock = a;
    linux_fds[fdb].kind = FD_SOCK;
    linux_fds[fdb].sock = b;
    sv[0] = fda;
    sv[1] = fdb;
    return 0;
}

static void lsys_exit(int code)
{
    monitor_write("\n[linux] exit pid=");
    monitor_write_dec(task_current_pid());
    monitor_write(" code=");
    monitor_write_dec((unsigned int)code);
    monitor_write("\n");
    task_exit_code(code);
    __builtin_unreachable();
}

void linux_syscall_dispatch(uint64_t rax, uint64_t rdi, uint64_t rsi, uint64_t rdx,
                            uint64_t r10, uint64_t r8, uint64_t r9,
                            uint64_t *out_rax)
{
    (void)r9;
    uint64_t ret = (uint64_t)-38;

    switch (rax) {
        case 0:   ret = (uint64_t)lsys_read((int)rdi, (void*)rsi, rdx); break;
        case 1:   ret = (uint64_t)lsys_write((int)rdi, (const void*)rsi, rdx); break;
        case 2:   ret = (uint64_t)lsys_open((const char*)rdi, (int)rsi, (int)rdx); break;
        case 3:   ret = (uint64_t)lsys_close((int)rdi); break;
        case 4:   ret = (uint64_t)lsys_stat((const char*)rdi, (struct linux_stat*)rsi); break;
        case 5:   ret = (uint64_t)lsys_fstat((int)rdi, (struct linux_stat*)rsi); break;
        case 6:   ret = (uint64_t)lsys_stat((const char*)rdi, (struct linux_stat*)rsi); break;
        case 7:   ret = (uint64_t)lsys_poll((struct linux_pollfd*)rdi, (uint32_t)rsi, (int)rdx); break;
        case 8:   ret = (uint64_t)lsys_lseek((int)rdi, (int64_t)rsi, (int)rdx); break;
        case 9:   ret = lsys_mmap(rdi, rsi, (int)rdx, (int)r10, (int)r8, r9); break;
        case 10:  ret = (uint64_t)lsys_mprotect(rdi, rsi, (int)rdx); break;
        case 11:  ret = (uint64_t)lsys_munmap(rdi, rsi); break;
        case 12:  ret = (uint64_t)lsys_brk(rdi); break;
        case 13:  ret = (uint64_t)lsys_rt_sigaction((int)rdi, (const void*)rsi, (void*)rdx, r10); break;
        case 30:  ret = (uint64_t)lsys_sysconf((int)rdi); break;
        case 14:  ret = (uint64_t)lsys_rt_sigprocmask((int)rdi, (const uint64_t*)rsi, (uint64_t*)rdx, r10); break;
        case 15:  ret = (uint64_t)lsys_rt_sigreturn(); break;
        case 16:  ret = (uint64_t)lsys_ioctl((int)rdi, rsi, rdx); break;
        case 17:  ret = (uint64_t)lsys_read((int)rdi, (void*)rsi, rdx); break;
        case 18:  ret = (uint64_t)lsys_write((int)rdi, (const void*)rsi, rdx); break;
        case 19:  ret = (uint64_t)lsys_readv((int)rdi, (const struct linux_iovec*)rsi, (int)rdx); break;
        case 20:  ret = (uint64_t)lsys_writev((int)rdi, (const struct linux_iovec*)rsi, (int)rdx); break;
        case 21:  ret = (uint64_t)lsys_access((const char*)rdi, (int)rsi); break;
        case 22:  ret = (uint64_t)lsys_pipe2((int*)rdi, 0); break;
        case 23:  ret = (uint64_t)lsys_select((int)rdi, (uint64_t*)rsi, (uint64_t*)rdx, (uint64_t*)r10, (const struct linux_timespec*)r8); break;
        case 24:  ret = (uint64_t)lsys_sched_yield(); break;
        case 25:  ret = (uint64_t)lsys_mremap_stub(); break;
        case 26:  ret = (uint64_t)lsys_msync(rdi, rsi, (int)rdx); break;
        case 27:  ret = (uint64_t)lsys_madvise(rdi, rsi, (int)rdx); break;
        case 28:  ret = (uint64_t)lsys_madvise(rdi, rsi, (int)rdx); break;
        case 32:  ret = (uint64_t)lsys_dup((int)rdi); break;
        case 33:  ret = (uint64_t)lsys_dup2((int)rdi, (int)rsi); break;
        case 34:  ret = (uint64_t)lsys_pause(); break;
        case 35:  ret = (uint64_t)lsys_nanosleep((const struct linux_timespec*)rdi, (struct linux_timespec*)rsi); break;
        case 36:  ret = (uint64_t)lsys_alarm((uint32_t)rdi); break;
        case 37:  ret = (uint64_t)lsys_alarm((uint32_t)rdi); break;
        case 39:  ret = (uint64_t)lsys_getpid(); break;
        case 41:  ret = (uint64_t)lsys_socket((int)rdi, (int)rsi, (int)rdx); break;
        case 42:  ret = (uint64_t)lsys_connect((int)rdi, (const void*)rsi, (uint32_t)rdx); break;
        case 43:  ret = (uint64_t)lsys_accept((int)rdi, (void*)rsi, (uint32_t*)rdx); break;
        case 44:  ret = (uint64_t)lsys_sendto((int)rdi, (const void*)rsi, rdx, (int)r10, (const void*)r8, (uint32_t)r9); break;
        case 45:  ret = (uint64_t)lsys_recvfrom((int)rdi, (void*)rsi, rdx, (int)r10, (void*)r8, (uint32_t*)r9); break;
        case 46:  ret = (uint64_t)lsys_sendmsg((int)rdi, (const void*)rsi, (int)rdx); break;
        case 47:  ret = (uint64_t)lsys_recvmsg((int)rdi, (void*)rsi, (int)rdx); break;
        case 48:  ret = (uint64_t)lsys_shutdown((int)rdi, (int)rsi); break;
        case 49:  ret = (uint64_t)lsys_bind((int)rdi, (const void*)rsi, (uint32_t)rdx); break;
        case 50:  ret = (uint64_t)lsys_listen((int)rdi, (int)rsi); break;
        case 51:  ret = (uint64_t)lsys_getsockname((int)rdi, (void*)rsi, (uint32_t*)rdx); break;
        case 52:  ret = (uint64_t)lsys_getpeername((int)rdi, (void*)rsi, (uint32_t*)rdx); break;
        case 53:  ret = (uint64_t)lsys_socketpair((int)rdi, (int)rsi, (int)rdx, (int*)r10); break;
        case 54:  ret = (uint64_t)lsys_setsockopt((int)rdi, (int)rsi, (int)rdx, (const void*)r10, (uint32_t)r8); break;
        case 55:  ret = (uint64_t)lsys_getsockopt((int)rdi, (int)rsi, (int)rdx, (void*)r10, (uint32_t*)r8); break;
        case 56:  ret = (uint64_t)lsys_clone(rdi, rsi, (void*)rdx, (void*)r10, r8); break;
        case 57:  ret = (uint64_t)lsys_fork(); break;
        case 58:  ret = (uint64_t)lsys_vfork(); break;
        case 59:  ret = (uint64_t)lsys_execve((const char*)rdi, rsi, rdx); break;
        case 60:  lsys_exit((int)rdi); break;
        case 61:  ret = (uint64_t)lsys_wait4((int)rdi, (int*)rsi, (int)rdx, (void*)r10); break;
        case 62:  ret = (uint64_t)lsys_kill((int)rdi, (int)rsi); break;
        case 63:  ret = (uint64_t)lsys_uname((struct linux_utsname*)rdi); break;
        case 72:  ret = (uint64_t)lsys_fcntl((int)rdi, (int)rsi, rdx); break;
        case 74:  ret = (uint64_t)lsys_fsync((int)rdi); break;
        case 75:  ret = (uint64_t)lsys_fdatasync((int)rdi); break;
        case 76:  ret = (uint64_t)lsys_truncate((const char*)rdi, (int64_t)rsi); break;
        case 77:  ret = (uint64_t)lsys_ftruncate((int)rdi, (int64_t)rsi); break;
        case 79:  ret = (uint64_t)lsys_getcwd((char*)rdi, rsi); break;
        case 80:  ret = (uint64_t)lsys_chdir((const char*)rdi); break;
        case 82:  ret = (uint64_t)lsys_rename((const char*)rdi, (const char*)rsi); break;
        case 83:  ret = (uint64_t)lsys_mkdir((const char*)rdi, (int)rsi); break;
        case 84:  ret = (uint64_t)lsys_rmdir((const char*)rdi); break;
        case 87:  ret = (uint64_t)lsys_unlink((const char*)rdi); break;
        case 88:  ret = (uint64_t)lsys_symlink((const char*)rdi, (const char*)rsi); break;
        case 89:  ret = (uint64_t)lsys_readlink((const char*)rdi, (char*)rsi, rdx); break;
        case 90:  ret = (uint64_t)lsys_chmod((const char*)rdi, (int)rsi); break;
        case 91:  ret = (uint64_t)lsys_fchmod((int)rdi, (int)rsi); break;
        case 92:  ret = (uint64_t)lsys_chown((const char*)rdi, (int)rsi, (int)rdx); break;
        case 95:  ret = (uint64_t)lsys_umask((int)rdi); break;
        case 96:  ret = (uint64_t)lsys_gettimeofday((void*)rdi, (void*)rsi); break;
        case 97:  ret = (uint64_t)lsys_getrlimit2((int)rdi, (void*)rsi); break;
        case 98:  ret = (uint64_t)lsys_getrusage((int)rdi, (void*)rsi); break;
        case 99:  ret = (uint64_t)lsys_sysinfo((void*)rdi); break;
        case 100: ret = (uint64_t)lsys_times((void*)rdi); break;
        case 102: ret = (uint64_t)lsys_getuid(); break;
        case 103: ret = (uint64_t)lsys_syslog((int)rdi, (char*)rsi, (int)rdx); break;
        case 104: ret = (uint64_t)lsys_getgid(); break;
        case 105: ret = (uint64_t)lsys_setuid_stub(); break;
        case 106: ret = (uint64_t)lsys_setgid_stub(); break;
        case 107: ret = (uint64_t)lsys_geteuid(); break;
        case 108: ret = (uint64_t)lsys_getegid(); break;
        case 109: ret = (uint64_t)lsys_setpgid((int)rdi, (int)rsi); break;
        case 110: ret = (uint64_t)lsys_getppid(); break;
        case 111: ret = (uint64_t)lsys_getsid(0); break;
        case 112: ret = (uint64_t)lsys_setsid(); break;
        case 113: ret = (uint64_t)lsys_setreuid_stub(); break;
        case 114: ret = (uint64_t)lsys_setregid_stub(); break;
        case 115: ret = (uint64_t)lsys_getgroups((int)rdi, (int*)rsi); break;
        case 116: ret = (uint64_t)lsys_setgroups((int)rdi, (const int*)rsi); break;
        case 117: ret = (uint64_t)lsys_setresuid_stub(); break;
        case 118: ret = (uint64_t)lsys_setresgid_stub(); break;
        case 119: ret = (uint64_t)lsys_getresuid_stub(); break;
        case 120: ret = (uint64_t)lsys_getresgid_stub(); break;
        case 121: ret = (uint64_t)lsys_getpgid((int)rdi); break;
        case 122: ret = (uint64_t)lsys_setuid_stub(); break;
        case 123: ret = (uint64_t)lsys_setgid_stub(); break;
        case 124: ret = (uint64_t)lsys_getsid((int)rdi); break;
        case 125: ret = (uint64_t)lsys_getsid((int)rdi); break;
        case 126: ret = (uint64_t)lsys_setsid(); break;
        case 127: ret = (uint64_t)lsys_sigpending_stub(); break;
        case 128: ret = (uint64_t)lsys_statfs((const char*)rdi, (void*)rsi); break;
        case 129: ret = (uint64_t)lsys_fstatfs((int)rdi, (void*)rsi); break;
        case 131: ret = (uint64_t)lsys_sigaltstack((const void*)rdi, (void*)rsi); break;
        //case 140: ret = (uint64_t)lsys_getpriority((int)rdi, (int)rsi); break;
        case 132: ret = (uint64_t)lsys_setpriority((int)rdi, (int)rsi, (int)rdx); break;
        case 137: ret = (uint64_t)lsys_statfs((const char*)rdi, (void*)rsi); break;
        case 138: ret = (uint64_t)lsys_fstatfs((int)rdi, (void*)rsi); break;
        case 140: ret = (uint64_t)lsys_getpriority((int)rdi, (int)rsi); break;
        case 141: ret = (uint64_t)lsys_setpriority((int)rdi, (int)rsi, (int)rdx); break;
        case 157: ret = (uint64_t)lsys_prctl((int)rdi, rsi, rdx, r10, r8); break;
        case 158: ret = (uint64_t)lsys_arch_prctl((int)rdi, rsi); break;
        case 160: ret = (uint64_t)lsys_setrlimit((int)rdi, (const void*)rsi); break;
        case 161: ret = (uint64_t)lsys_chroot_stub(); break;
        case 186: ret = (uint64_t)lsys_gettid(); break;
        case 200: ret = (uint64_t)lsys_tkill((int)rdi, (int)rsi); break;
        case 234: ret = (uint64_t)lsys_tgkill((int)rdi, (int)rsi, (int)rdx); break;
        case 199: ret = (uint64_t)lsys_mlock(rdi, rsi); break;
        case 202: ret = (uint64_t)lsys_futex((uint32_t*)rdi, (int)rsi, (uint32_t)rdx, (const struct linux_timespec*)r10, (uint32_t*)r8, (uint32_t)r9); break;
        case 203: ret = (uint64_t)lsys_sched_setaffinity((int)rdi, (uint32_t)rsi, (const uint64_t*)rdx); break;
        case 204: ret = (uint64_t)lsys_sched_getaffinity((int)rdi, (uint32_t)rsi, (uint64_t*)rdx); break;
        case 217: ret = (uint64_t)lsys_getdents64((int)rdi, (void*)rsi, (uint32_t)rdx); break;
        case 222: ret = (uint64_t)lsys_timer_create((int)rdi, (void*)rsi, (int*)rdx); break;
        case 223: ret = (uint64_t)lsys_timer_settime((int)rdi, (int)rsi, (const void*)rdx, (void*)r10); break;
        case 226: ret = (uint64_t)lsys_timer_delete((int)rdi); break;
        case 237: ret = (uint64_t)lsys_mbind(rdi, rsi, (int)rdx, (const uint64_t*)r10, r8, r9); break;
        case 238: ret = (uint64_t)lsys_set_mempolicy((int)rdi, (const uint64_t*)rsi, rdx); break;
        case 239: ret = (uint64_t)lsys_get_mempolicy((int*)rdi, (uint64_t*)rsi, rdx, r10, r8); break;
        case 218: ret = (uint64_t)lsys_set_tid_address((int*)rdi); break;
        case 221: ret = (uint64_t)lsys_fadvise_stub(); break;
        case 228: ret = (uint64_t)lsys_clock_gettime((int)rdi, (struct linux_timespec*)rsi); break;
        case 230: ret = (uint64_t)lsys_nanosleep((const struct linux_timespec*)rdi, (struct linux_timespec*)rsi); break;
        case 231: lsys_exit((int)rdi); break;
        case 232: ret = (uint64_t)lsys_epoll_wait((int)rdi, (void*)rsi, (int)rdx, (int)r10); break;
        case 233: ret = (uint64_t)lsys_epoll_ctl((int)rdi, (int)rsi, (int)rdx, (void*)r10); break;
        case 247: ret = (uint64_t)lsys_waitid((int)rdi, (int)rsi, (void*)rdx, (int)r10, (void*)r8); break;
        case 253: ret = (uint64_t)lsys_inotify_stub(); break;
        case 257: ret = (uint64_t)lsys_open((const char*)rsi, (int)rdx, (int)r10); break;
        case 262: ret = (uint64_t)lsys_fstat((int)rsi, (struct linux_stat*)rdx); break;
        case 263: ret = (uint64_t)lsys_unlink((const char*)rsi); break;
        case 264: ret = (uint64_t)lsys_rename((const char*)rsi, (const char*)rdx); break;
        case 265: ret = (uint64_t)lsys_readlink((const char*)rsi, (char*)rdx, r10); break;
        case 266: ret = (uint64_t)lsys_symlink((const char*)rsi, (const char*)rdx); break;
        case 267: ret = (uint64_t)lsys_readlink((const char*)rsi, (char*)rdx, r10); break;
        case 268: ret = (uint64_t)lsys_fchmod((int)rsi, (int)rdx); break;
        case 269: ret = (uint64_t)lsys_access((const char*)rsi, (int)rdx); break;
        case 270: ret = (uint64_t)lsys_pselect6((int)rdi, (uint64_t*)rsi, (uint64_t*)rdx, (uint64_t*)r10, (const struct linux_timespec*)r8, (const void*)r9); break;
        case 271: ret = (uint64_t)lsys_ppoll((struct linux_pollfd*)rdi, (uint32_t)rsi, (const struct linux_timespec*)rdx, (const void*)r10, r8); break;
        case 273: ret = (uint64_t)lsys_set_robust_list((void*)rdi, rsi); break;
        case 283: ret = (uint64_t)lsys_timerfd_create((int)rdi, (int)rsi); break;
        case 284: ret = (uint64_t)lsys_eventfd2((uint32_t)rdi, (int)rsi); break;
        case 288: ret = (uint64_t)lsys_signalfd4((int)rdi, (const void*)rsi, rdx, (int)r10); break;
        case 289: ret = (uint64_t)lsys_eventfd2((uint32_t)rdi, (int)rsi); break;
        case 290: ret = (uint64_t)lsys_epoll_create1((int)rdi); break;
        case 291: ret = (uint64_t)lsys_epoll_create1((int)rdi); break;
        case 293: ret = (uint64_t)lsys_pipe2((int*)rdi, (int)rsi); break;
        case 302: ret = (uint64_t)lsys_prlimit64((int)rdi, (int)rsi, (const void*)rdx, (void*)r10); break;
        case 308: ret = (uint64_t)lsys_pselect6((int)rdi, (uint64_t*)rsi, (uint64_t*)rdx, (uint64_t*)r10, (const struct linux_timespec*)r8, (const void*)r9); break;
        case 309: ret = (uint64_t)lsys_getcpu((uint32_t*)rdi, (uint32_t*)rsi, (void*)rdx); break;
        case 318: ret = (uint64_t)lsys_getrandom((void*)rdi, rsi, (uint32_t)rdx); break;
        case 334: ret = (uint64_t)lsys_mlock(rdi, rsi); break;
        default:
            lsys_serial_puts("LNX_SC_UNHANDLED:");
            {
                char b[8]; int v = (int)rax; int n = 0;
                if (v == 0) b[n++] = '0';
                while (v) { b[n++] = '0' + (v % 10); v /= 10; }
                while (n) lsys_serial_putc(b[--n]);
            }
            lsys_serial_putc('\n');
            ret = (uint64_t)-38;
            break;
    }
    *out_rax = ret;
}

static int64_t lsys_mremap_stub(void) { return -38; }
static int64_t lsys_setuid_stub(void) { return 0; }
static int64_t lsys_setgid_stub(void) { return 0; }
static int64_t lsys_setreuid_stub(void) { return 0; }
static int64_t lsys_setregid_stub(void) { return 0; }
static int64_t lsys_setresuid_stub(void) { return 0; }
static int64_t lsys_setresgid_stub(void) { return 0; }
static int64_t lsys_getresuid_stub(void) { return 0; }
static int64_t lsys_getresgid_stub(void) { return 0; }
static int64_t lsys_sigpending_stub(void) { return 0; }
static int64_t lsys_chroot_stub(void) { return 0; }
static int64_t lsys_fadvise_stub(void) { return 0; }
static int64_t lsys_inotify_stub(void) { return -38; }

static void linux_int_entry(registers_t *regs)
{
    uint64_t out;
    linux_syscall_dispatch(regs->rax, regs->rdi, regs->rsi, regs->rdx,
                           regs->r10, regs->r8, regs->r9, &out);
    regs->rax = out;
    task_signal_deliver();
}

void linux_sys_init(void)
{
    memset(linux_fds, 0, sizeof(linux_fds));
    linux_fds[0].kind = FD_STD;
    linux_fds[1].kind = FD_STD;
    linux_fds[2].kind = FD_STD;
    register_interrupt_handler(129, linux_int_entry);
}

static uint64_t elf64_load_segment_span(Elf64_Ehdr *hdr, Elf64_Phdr *phdr)
{
    uint64_t max_addr = 0;
    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        uint64_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
        if (end > max_addr) max_addr = end;
    }
    return max_addr;
}

static void *linux_load_elf_into_vas(char *elf_start, uint32_t size, vas_t *as, uint64_t *out_entry)
{
    (void)as;
    Elf64_Ehdr *hdr = (Elf64_Ehdr*)elf_start;
    Elf64_Phdr *phdr = (Elf64_Phdr*)(elf_start + hdr->e_phoff);

    uint64_t span = elf64_load_segment_span(hdr, phdr);
    if (span == 0 || span > 512ull * 1024 * 1024) return NULL;

    uint64_t exec_phys_span = span + 0x10000;
    char *exec = (char*)malloc((uint32_t)exec_phys_span);
    if (!exec) return NULL;
    memset(exec, 0, (uint32_t)exec_phys_span);

    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_filesz > phdr[i].p_memsz) { free(exec); return NULL; }
        char *dst = exec + phdr[i].p_vaddr;
        char *src = elf_start + phdr[i].p_offset;
        if (phdr[i].p_filesz > 0)
            memcpy(dst, src, phdr[i].p_filesz);
        if (phdr[i].p_memsz > phdr[i].p_filesz)
            memset(dst + phdr[i].p_filesz, 0, phdr[i].p_memsz - phdr[i].p_filesz);
    }

    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_DYNAMIC) continue;
        Elf64_Dyn *dyn = (Elf64_Dyn*)(elf_start + phdr[i].p_offset);
        uint64_t rela_off = 0, rela_sz = 0, rela_ent = sizeof(Elf64_Rela);
        const char *strtab = NULL;
        uint64_t symtab_off = 0, symtab_entsz = 0, jmprel_off = 0, jmprel_sz = 0;
        for (int d = 0; dyn[d].d_tag != DT_NULL; d++) {
            switch (dyn[d].d_tag) {
                case DT_RELA:    rela_off = dyn[d].d_val; break;
                case DT_RELASZ:  rela_sz  = dyn[d].d_val; break;
                case DT_RELAENT: rela_ent = dyn[d].d_val; break;
                case DT_STRTAB:  strtab = exec + dyn[d].d_val; break;
                case DT_SYMTAB:  symtab_off = dyn[d].d_val; break;
                case DT_SYMENT:  symtab_entsz = dyn[d].d_val; break;
                case DT_JMPREL:  jmprel_off = dyn[d].d_val; break;
                case DT_PLTRELSZ: jmprel_sz = dyn[d].d_val; break;
            }
        }
        if (rela_off && rela_sz) {
            uint64_t n = rela_sz / rela_ent;
            for (uint64_t r = 0; r < n; r++) {
                Elf64_Rela *rel = (Elf64_Rela*)(exec + rela_off + r * rela_ent);
                uint32_t type = ELF64_R_TYPE(rel->r_info);
                if (type == R_X86_64_RELATIVE) {
                    uint64_t *slot = (uint64_t*)(exec + rel->r_offset);
                    *slot = (uint64_t)(uintptr_t)exec + rel->r_addend;
                } else if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                    if (strtab && symtab_off && symtab_entsz) {
                        uint32_t sym_idx = (uint32_t)(rel->r_info >> 32);
                        Elf64_Sym *sym = (Elf64_Sym*)(exec + symtab_off + sym_idx * symtab_entsz);
                        uint64_t *slot = (uint64_t*)(exec + rel->r_offset);
                        *slot = sym->st_value ? (uint64_t)(uintptr_t)exec + sym->st_value
                                              : (uint64_t)(uintptr_t)exec;
                    }
                }
            }
        }
        if (jmprel_off && jmprel_sz && rela_ent) {
            uint64_t n = jmprel_sz / rela_ent;
            for (uint64_t r = 0; r < n; r++) {
                Elf64_Rela *rel = (Elf64_Rela*)(exec + jmprel_off + r * rela_ent);
                uint32_t type = ELF64_R_TYPE(rel->r_info);
                if (type == R_X86_64_JUMP_SLOT) {
                    if (strtab && symtab_off && symtab_entsz) {
                        uint32_t sym_idx = (uint32_t)(rel->r_info >> 32);
                        Elf64_Sym *sym = (Elf64_Sym*)(exec + symtab_off + sym_idx * symtab_entsz);
                        uint64_t *slot = (uint64_t*)(exec + rel->r_offset);
                        *slot = sym->st_value ? (uint64_t)(uintptr_t)exec + sym->st_value
                                              : (uint64_t)(uintptr_t)exec;
                    }
                }
            }
        }
    }

    *out_entry = (uint64_t)(uintptr_t)exec + (uint64_t)hdr->e_entry;
    return exec;
}

#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_ENTRY  9
#define AT_RANDOM 25
#define AT_SECURE 23
#define AT_EXECFN 31

static int elf64_find_interp(char *elf_buf, uint32_t sz, char *out, uint32_t out_cap)
{
    Elf64_Ehdr *hdr = (Elf64_Ehdr*)elf_buf;
    Elf64_Phdr *phdr = (Elf64_Phdr*)(elf_buf + hdr->e_phoff);
    for (int i = 0; i < hdr->e_phnum; i++) {
        if (phdr[i].p_type == 3) {
            uint32_t off = (uint32_t)phdr[i].p_offset;
            uint32_t len = (uint32_t)phdr[i].p_filesz;
            if (off + len > sz || len >= out_cap) return -1;
            for (uint32_t k = 0; k <= len; k++) out[k] = elf_buf[off + k];
            return 0;
        }
    }
    return -1;
}

int linux_exec_elf(const char *path)
{
    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    if (!node) {
        monitor_write("\n[linux] not found: ");
        monitor_write((char*)path);
        monitor_write("\n");
        return -1;
    }

    static char elf_buf[262144];
    memset(elf_buf, 0, sizeof(elf_buf));
    uint32_t sz = read_fs(node, 0, sizeof(elf_buf) - 1, elf_buf);
    if (sz < 64) return -2;

    Elf64_Ehdr *hdr = (Elf64_Ehdr*)elf_buf;
    if (hdr->e_ident[EI_MAG0] != 0x7F || hdr->e_ident[EI_MAG1] != 'E' ||
        hdr->e_ident[EI_MAG2] != 'L' || hdr->e_ident[EI_MAG3] != 'F') {
        monitor_write("\n[linux] not an ELF\n");
        return -3;
    }
    if (hdr->e_ident[EI_CLASS] != ELFCLASS64) {
        monitor_write("\n[linux] need 64-bit ELF\n");
        return -4;
    }

    vas_t *as = vmm_create_address_space();
    if (!as) {
        monitor_write("\n[linux] OOM creating address space\n");
        return -5;
    }

    uint64_t entry = 0;
    void *img = linux_load_elf_into_vas(elf_buf, sz, as, &entry);
    if (!img) {
        monitor_write("\n[linux] ELF load failed\n");
        vmm_destroy_address_space(as);
        return -6;
    }

    char interp[128];
    uint64_t interp_entry = 0;
    uint64_t interp_base = 0;
    int has_interp = elf64_find_interp(elf_buf, sz, interp, sizeof(interp)) == 0;
    void *interp_img = NULL;

    if (has_interp) {
        fs_node_t *lnode = finddir_fs(fs_root, interp);
        if (!lnode) {
            const char *base = interp;
            for (const char *p = interp; *p; p++) {
                if (*p == '/') base = p + 1;
            }
            lnode = finddir_fs(fs_root, (char*)base);
        }
        if (lnode) {
            static char lbuf[262144];
            memset(lbuf, 0, sizeof(lbuf));
            uint32_t lsz = read_fs(lnode, 0, sizeof(lbuf) - 1, lbuf);
            if (lsz >= 64) {
                interp_img = linux_load_elf_into_vas(lbuf, lsz, as, &interp_entry);
                if (interp_img) {
                    interp_base = (uint64_t)(uintptr_t)interp_img;
                    compat_patch_syscalls(interp_img, lsz + 0x10000, SYSCALL_PATCH_LINUX);
                }
            }
        }
    }

    compat_patch_syscalls(img, sz + 0x10000, SYSCALL_PATCH_LINUX);

    uint64_t ustack_top = as->stack_top;
    uint64_t sp = ustack_top & ~0xFull;

    static uint8_t random_bytes[16];
    uint32_t seed = (uint32_t)tick;
    for (int i = 0; i < 16; i++) {
        seed = seed * 1103515245u + 12345u;
        random_bytes[i] = (uint8_t)(seed >> 16);
    }
    sp -= 16;
    uint64_t random_ptr = sp;
    memcpy((void*)random_ptr, random_bytes, 16);

    const char *execfn = path;
    uint32_t execfn_len = 0;
    while (execfn[execfn_len]) execfn_len++;
    sp -= execfn_len + 1;
    uint64_t execfn_ptr = sp;
    for (uint32_t i = 0; i <= execfn_len; i++)
        ((char*)execfn_ptr)[i] = execfn[i];

    sp &= ~0xFull;

    uint64_t phdr_addr = (uint64_t)(uintptr_t)img + hdr->e_phoff;
    uint64_t phnum = hdr->e_phnum;
    uint64_t phent = hdr->e_phentsize;

    uint64_t auxv[18];
    auxv[0] = AT_PHDR;   auxv[1] = phdr_addr;
    auxv[2] = AT_PHENT;  auxv[3] = phent;
    auxv[4] = AT_PHNUM;  auxv[5] = phnum;
    auxv[6] = AT_PAGESZ; auxv[7] = 4096;
    auxv[8] = AT_ENTRY;  auxv[9] = entry;
    auxv[10] = AT_RANDOM; auxv[11] = random_ptr;
    auxv[12] = AT_SECURE; auxv[13] = 0;
    auxv[14] = AT_EXECFN; auxv[15] = execfn_ptr;
    auxv[16] = AT_BASE;   auxv[17] = interp_base;

    sp -= 18 * 8;
    uint64_t *auxv_sp = (uint64_t*)sp;
    for (int i = 0; i < 18; i++) auxv_sp[i] = auxv[i];

    sp -= 8; *(uint64_t*)sp = 0;
    sp -= 8; *(uint64_t*)sp = 0;
    sp -= 8; *(uint64_t*)sp = 1;
    sp -= 8; *(uint64_t*)sp = execfn_ptr;
    sp -= 8; *(uint64_t*)sp = 1;

    uint64_t final_entry = has_interp && interp_entry ? interp_entry : entry;
    int pid = task_spawn_user(path, final_entry, sp, as);
    if (pid < 0) {
        monitor_write("\n[linux] spawn failed\n");
        vmm_destroy_address_space(as);
        free(img);
        return -7;
    }
    task_set_name(pid, path);
    return pid;
}
