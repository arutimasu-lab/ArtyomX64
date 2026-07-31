#include "compat.h"
#include "linux_sys.h"
#include "macho.h"
#include "../fs/elf.h"
#include "../fs/fs.h"
#include "../fs/initrd.h"
#include "../mm/malloc.h"
#include "../drivers/monitor.h"
#include "../lib/common.h"
#include "../lib/ipc.h"
#include "../kernel/isr.h"
#include <stddef.h>

extern void *image_load(char *elf_start, unsigned int size);
extern int call_elf32_thunk(unsigned int entry, unsigned int arg1, unsigned int arg2);
extern void yield(void);
extern void linux_syscall_dispatch(uint64_t rax, uint64_t rdi, uint64_t rsi, uint64_t rdx,
                                   uint64_t r10, uint64_t r8, uint64_t r9,
                                   uint64_t *out_rax);

typedef struct file {
    fs_node_t *node;
    u32int pos;
    u32int flags;
} file_t;

extern file_t *files[];
extern int current_fd;

static int compat_initialized = 0;

void compat_init(void)
{
    compat_initialized = 1;
}

static int is_macho64(const void *buf)
{
    const uint32_t *m = (const uint32_t*)buf;
    return m[0] == MH_MAGIC_64 || m[0] == MH_CIGAM_64;
}

static int is_macho32(const void *buf)
{
    const uint32_t *m = (const uint32_t*)buf;
    return m[0] == MH_MAGIC || m[0] == MH_CIGAM;
}

static int is_elf_any(const void *buf)
{
    const uint8_t *h = (const uint8_t*)buf;
    return h[0] == 0x7F && h[1] == 'E' && h[2] == 'L' && h[3] == 'F';
}

int compat_detect_format(const void *buf, uint32_t size)
{
    if (size < 16) return COMPAT_NATIVE;
    if (is_macho64(buf)) return COMPAT_DARWIN;
    if (is_macho32(buf)) return COMPAT_DARWIN;
    if (is_elf_any(buf)) {
        const uint8_t *h = (const uint8_t*)buf;
        if (h[EI_CLASS] == ELFCLASS64) return COMPAT_LINUX64;
        if (h[EI_CLASS] == ELFCLASS32) return COMPAT_LINUX32;
    }
    return COMPAT_NATIVE;
}

static void *macho_load64(void *buf, uint32_t size)
{
    macho_header_64 *hdr = (macho_header_64*)buf;
    if (hdr->magic != MH_MAGIC_64 && hdr->magic != MH_CIGAM_64) return NULL;

    uint32_t alloc_size = size * 2 + 0x10000;
    char *exec = (char*)malloc(alloc_size);
    if (!exec) return NULL;
    memset((u8int*)exec, 0, alloc_size);

    uint64_t entry_off = 0;
    uint64_t base_addr = 0;
    int found_seg = 0;

    char *lc_ptr = (char*)(hdr + 1);
    for (uint32_t i = 0; i < hdr->ncmds; i++) {
        macho_load_command *lc = (macho_load_command*)lc_ptr;
        if (lc->cmdsize == 0) break;

        if (lc->cmd == LC_SEGMENT_64) {
            macho_segment_command_64 *seg = (macho_segment_command_64*)lc_ptr;
            if (seg->filesize > 0 && seg->fileoff + seg->filesize <= size) {
                if (!found_seg) {
                    base_addr = seg->vmaddr;
                    found_seg = 1;
                }
                uint64_t dst_off = seg->vmaddr - base_addr;
                if (dst_off + seg->filesize > alloc_size) {
                    alloc_size = (uint32_t)(dst_off + seg->filesize + 0x1000);
                    char *new_exec = (char*)malloc(alloc_size);
                    if (!new_exec) { free(exec); return NULL; }
                    memset((u8int*)new_exec, 0, alloc_size);
                    memcpy((u8int*)new_exec, (u8int*)exec, dst_off);
                    free(exec);
                    exec = new_exec;
                }
                memcpy((u8int*)(exec + dst_off),
                       (u8int*)(buf + seg->fileoff),
                       seg->filesize);
            }
        } else if (lc->cmd == LC_MAIN) {
            macho_entry_point_command *ep = (macho_entry_point_command*)lc_ptr;
            entry_off = ep->entryoff;
        } else if (lc->cmd == LC_UNIXTHREAD) {
            uint32_t *flavor_ptr = (uint32_t*)(lc_ptr + 8);
            if (*flavor_ptr == 4) {
                macho_thread_state_64 *ts = (macho_thread_state_64*)(flavor_ptr + 2);
                entry_off = ts->rip - base_addr;
            }
        }

        lc_ptr += lc->cmdsize;
    }

    return (void*)(exec + entry_off);
}

void *compat_load(void *buf, uint32_t size, int format)
{
    switch (format) {
        case COMPAT_DARWIN:
            return macho_load64(buf, size);
        case COMPAT_LINUX64:
        case COMPAT_LINUX32:
        case COMPAT_NATIVE:
        default:
            return image_load((char*)buf, size);
    }
}

void compat_patch_syscalls(void *code, uint32_t size, int patch_int)
{
    uint8_t *p = (uint8_t*)code;
    uint8_t int_no = (uint8_t)patch_int;

    for (uint32_t i = 0; i + 1 < size; i++) {
        if (p[i] == 0x0F && p[i+1] == 0x05) {
            p[i]   = 0xCD;
            p[i+1] = int_no;
        }
    }
}

static int sys_write_compat(int fd, const char *buf, int len)
{
    if (fd != 1 && fd != 2) return -1;
    int n = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == 0) break;
        monitor_put(buf[i]);
        n++;
    }
    return n;
}

static int sys_read_compat(int fd, char *buf, int len)
{
    if (fd != 0) return -1;
    if (len <= 0) return 0;

    extern void keyboard_clear_buffer(void);
    extern int is_enter_pressed;
    extern int is_buffer_empty(void);
    extern char keyboard_read(void);

    __asm__ volatile("sti");
    keyboard_clear_buffer();

    int i = 0;
    while (i < len - 1) {
        if (is_enter_pressed) {
            while (!is_buffer_empty() && i < len - 1) {
                char c = keyboard_read();
                if (c == (char)-1) break;
                if (c == '\n') {
                    buf[i] = '\0';
                    is_enter_pressed = 0;
                    return i;
                }
                buf[i++] = c;
            }
            if (is_enter_pressed) {
                buf[i] = '\0';
                is_enter_pressed = 0;
                return i;
            }
        }
        __asm__ volatile("pause");
    }
    buf[i] = '\0';
    return i;
}

static int sys_open_compat(const char *path, int flags, int mode)
{
    (void)flags; (void)mode;
    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    if (!node) return -1;

    int fd = current_fd++;
    file_t *f = (file_t*)malloc(sizeof(file_t));
    f->node = node;
    f->pos = 0;
    f->flags = flags;
    files[fd] = f;
    return fd;
}

static int sys_close_compat(int fd)
{
    if (fd < 0 || fd >= 16384) return -1;
    if (files[fd]) {
        free(files[fd]);
        files[fd] = NULL;
        return 0;
    }
    return -1;
}

static void *sys_brk_compat(void *addr)
{
    static u8int *cur_brk = NULL;
    if (!cur_brk) cur_brk = (u8int*)malloc(0x100000);
    if (addr) {
        uintptr_t target = (uintptr_t)addr;
        uintptr_t cur = (uintptr_t)cur_brk;
        if (target > cur && target < cur + 0x100000) {
            cur_brk = (u8int*)target;
        }
    }
    return cur_brk;
}

static void *sys_mmap_compat(void *addr, uint64_t len, int prot,
                             int flags, int fd, uint64_t off)
{
    (void)addr; (void)prot; (void)flags; (void)fd; (void)off;
    void *p = malloc((uint32_t)len);
    if (p) memset((u8int*)p, 0, (uint32_t)len);
    return p;
}

static int sys_munmap_compat(void *addr, uint64_t len)
{
    (void)len;
    if (addr) { free(addr); return 0; }
    return -1;
}

void linux_syscall_handler(u64int rax, u64int rdi, u64int rsi, u64int rdx,
                           u64int r10, u64int r8, u64int r9,
                           u64int *out_rax)
{
    linux_syscall_dispatch(rax, rdi, rsi, rdx, r10, r8, r9, out_rax);
}

void darwin_syscall_handler(u64int rax, u64int rdi, u64int rsi, u64int rdx,
                            u64int r10, u64int r8, u64int r9,
                            u64int *out_rax)
{
    (void)r10; (void)r8; (void)r9;
    u64int ret = (u64int)-1;

    u64int subsystem = (rax >> 24) & 0xFF;
    u64int sc = rax & 0xFFFFFF;

    if (subsystem == 0 || subsystem == 0x02) {
        switch (sc) {
            case 1:
                monitor_write("\n[Darwin] exit: ");
                monitor_write_dec((int)rdi);
                monitor_write("\n");
                for (;;) yield();
                break;
            case 4:
                ret = (u64int)sys_write_compat((int)rdi, (const char*)rsi, (int)rdx);
                break;
            case 3:
                ret = (u64int)sys_read_compat((int)rdi, (char*)rsi, (int)rdx);
                break;
            case 5:
                ret = (u64int)sys_open_compat((const char*)rdi, (int)rsi, (int)rdx);
                break;
            case 6:
                ret = (u64int)sys_close_compat((int)rdi);
                break;
            case 0x20:
                ret = 0;
                break;
            case 0x36:
                ret = 0;
                break;
            case 0x49:
                ret = (u64int)(uintptr_t)sys_brk_compat((void*)rdi);
                break;
            case 0xc5:
                ret = (u64int)(uintptr_t)sys_mmap_compat((void*)rdi, rsi, (int)rdx, (int)r10, (int)r8, r9);
                break;
            default:
                monitor_write("\n[Darwin] unhandled syscall: 0x");
                monitor_write_hex((u32int)sc);
                monitor_write("\n");
                ret = (u64int)-90;
                break;
        }
    } else {
        monitor_write("\n[Darwin] unhandled subsystem: 0x");
        monitor_write_hex((u32int)subsystem);
        monitor_write("\n");
        ret = (u64int)-90;
    }

    *out_rax = ret;
}

static void linux_int_handler(registers_t *regs)
{
    u64int out;
    linux_syscall_handler(regs->rax, regs->rdi, regs->rsi, regs->rdx,
                          regs->r10, regs->r8, regs->r9, &out);
    regs->rax = out;
}

static void darwin_int_handler(registers_t *regs)
{
    u64int out;
    darwin_syscall_handler(regs->rax, regs->rdi, regs->rsi, regs->rdx,
                           regs->r10, regs->r8, regs->r9, &out);
    regs->rax = out;
}

void compat_register_handlers(void)
{
    register_interrupt_handler(129, linux_int_handler);
    register_interrupt_handler(130, darwin_int_handler);
}

int compat_exec_buf(void *buf, uint32_t size)
{
    int fmt = compat_detect_format(buf, size);
    if (fmt == COMPAT_NATIVE) return -1;

    void *code = compat_load(buf, size, fmt);
    if (!code) return -2;

    int patch_int;
    if (fmt == COMPAT_DARWIN) {
        patch_int = SYSCALL_PATCH_DARWIN;
    } else {
        patch_int = SYSCALL_PATCH_LINUX;
    }

    compat_patch_syscalls(code, size * 2, patch_int);

    int result = call_compat64_thunk((uint64_t)(uintptr_t)code, fmt);

    free(code);
    return result;
}

int compat_exec(const char *path)
{
    fs_node_t *node = finddir_fs(fs_root, (char*)path);
    if (!node) {
        monitor_write("\ncompat: file not found: ");
        monitor_write((char*)path);
        monitor_write("\n");
        return -1;
    }

    static char buf[65536];
    memset((u8int*)buf, 0, sizeof(buf));
    u32int sz = read_fs(node, 0, sizeof(buf) - 1, buf);

    int fmt = compat_detect_format(buf, sz);
    if (fmt == COMPAT_NATIVE) {
        monitor_write("\ncompat: native ELF, use exec\n");
        return -1;
    }

    monitor_write("\ncompat: detected format: ");
    switch (fmt) {
        case COMPAT_DARWIN:   monitor_write("Darwin/Mach-O"); break;
        case COMPAT_LINUX64:  monitor_write("Linux x86_64"); break;
        case COMPAT_LINUX32:  monitor_write("Linux i386"); break;
        default:              monitor_write("unknown"); break;
    }
    monitor_write("\n");

    return compat_exec_buf(buf, sz);
}