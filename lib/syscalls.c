#include "syscalls.h"
#include "../drivers/monitor.h"
#include "../drivers/keyboard.h"
#include "../kernel/isr.h"
#include "../fs/fs.h"
#include "../fs/elf.h"
#include "../fs/initrd.h"
#include "../fs/task.h"
#include "../mm/malloc.h"
#include "../lib/common.h"
#include <stddef.h>
#include <stdint.h>

#include "../lib/ipc.h"

typedef struct file {
    fs_node_t *node;
    u32int pos;
    u32int flags;
} file_t;

file_t *files[16384];
int current_fd = 2;

int exit(int code){
    return code;
}

int getdents(int fd, void *buf, u32int size)
{
    file_t *f = files[fd];
    fs_node_t *node = f->node;

    if ((node->flags & 0x7) != FS_DIRECTORY)
        return -2;

    u32int written = 0;

    while (1)
    {
        struct dirent *d = readdir_fs(node, f->pos);
        if (!d)
            break;

        u32int namelen = strlen(d->name);
        u32int reclen =
            sizeof(u32int) +
            sizeof(u16int) +
            1 +
            namelen + 1;

        reclen = (reclen + 3) & ~3;

        if (written + reclen > size)
            break;

        *(u32int *)(buf + written) = d->ino;
        *(u16int *)(buf + written + 4) = reclen;
        *(u8int  *)(buf + written + 6) = 0;
        strcpy(buf + written + 7, d->name);

        written += reclen;
        f->pos++;
    }

    return written;
}

int write(int fd, char* buf, int nbytes) {
    if (fd != 1) return -1;

    int start = 0;
    while (start < nbytes && buf[start] == 0) {
        start++;
    }

    for(int i = start; i < nbytes; i++) {
        if (buf[i] == 0) break;
        monitor_put(buf[i]);
    }

    return nbytes - start;
}

int read(int fd, char *buf, int nbytes) {

    if (nbytes <= 0) return 0;

     if(fd==0){
             __asm__ volatile("sti");
    int i = 0;
    char c;

    keyboard_clear_buffer();

    while (i < nbytes - 1) {
        if (is_enter_pressed) {
            while (!is_buffer_empty() && i < nbytes - 1) {
                c = keyboard_read();
                if (c == -1) break;

                if (c == '\n') {
                    buf[i] = '\0';
                    is_enter_pressed = 0;
                    return i;
                }
                buf[i] = c;
                i++;
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
      else {

           file_t *f = files[fd];
        if (!f)
            return -1;

        if ((f->node->flags & 0x7) == FS_DIRECTORY)
            return -1;

        int ret = read_fs(f->node, f->pos, nbytes, buf);
        if (ret > 0)
            f->pos += ret;

        return ret;
    }
}

int open(const char* path, int flags, int mode)
{
    fs_node_t *fsnode = finddir_fs(fs_root, path);
    if (!fsnode)
        return -1;

    int fd = current_fd++;

    file_t *f = malloc(sizeof(file_t));
    f->node = fsnode;
    f->pos  = 0;
    f->flags = flags;

    files[fd] = f;
    return fd;
}

int unlink(const char* path){
    return initrd_remove(path) ? 0 : -1;
}

extern void *image_load(char *elf_start, unsigned int size);

extern int call_elf32_thunk(unsigned int entry, unsigned int arg1, unsigned int arg2);
static int gfx_ipc(void* msg) { (void)msg; return 0; }

static int is_elf32_class(void *elf)
{
    uint8_t *hdr = (uint8_t*)elf;
    if (hdr[EI_MAG0] != 0x7F || hdr[EI_MAG1] != 'E' ||
        hdr[EI_MAG2] != 'L' || hdr[EI_MAG3] != 'F')
        return 0;
    return hdr[EI_CLASS] == ELFCLASS32;
}

static int is_linux_dynamic_elf(void *elf)
{
    uint8_t *h = (uint8_t*)elf;
    if (h[EI_MAG0] != 0x7F || h[EI_MAG1] != 'E' || h[EI_MAG2] != 'L' || h[EI_MAG3] != 'F')
        return 0;
    if (h[EI_CLASS] != ELFCLASS64)
        return 0;
    Elf64_Ehdr *eh = (Elf64_Ehdr*)elf;
    if (eh->e_type != 3 && eh->e_type != 2)
        return 0;
    Elf64_Phdr *ph = (Elf64_Phdr*)((char*)elf + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC || ph[i].p_type == 3)
            return 1;
    }
    return 0;
}

extern int linux_exec_elf(const char *path);

int exec(const char* path) {
    fs_node_t *fsnode = finddir_fs(fs_root, (char*)path);
    if (fsnode == 0) {
        monitor_write("\nex: App not found.");
        return -1;
    }

    char buf[14000];
    memset(buf, 0x0, sizeof(buf));
    u32int sz = read_fs(fsnode, 0, sizeof(buf), buf);

    if (is_linux_dynamic_elf(buf)) {
        return linux_exec_elf(path);
    }

    void *ptr = image_load(buf, sz);
    if (ptr == NULL) {
        monitor_write("\nLoading unsuccessful...\n");
        return -2;
    }

    int result = 0;
    if (is_elf32_class(buf)) {
        result = call_elf32_thunk((unsigned int)(uintptr_t)ptr, 0, 0);
    } else {
        int (*entry64)(int, char**, char**) = (int (*)(int, char**, char**))ptr;
        result = entry64(0, NULL, NULL);
    }

    monitor_write("\nProgram exited normally with code: ");
    monitor_write_dec(result);
    monitor_write("\n");

    free(ptr);
    return result;
}

#define GFX_DRIVER 0

#define GFX_SET_PX 0
#define GFX_SET_X 1
#define GFX_SET_Y 2
#define GFX_SET_COL 3
#define GFX_CLEAR 4
#define GFX_INIT 5


int ipc_call(int endpoint, ipc_msg_t *msg) {
    switch (endpoint) {
        case GFX_DRIVER:
            return gfx_ipc(msg);
    }
    return -1;
}

static void syscall_handler(registers_t *regs);

DEFN_SYSCALL1(exit, 1,  int);
DEFN_SYSCALL3(read, 3,  int, const char*, int);
DEFN_SYSCALL3(write, 4, int, const char*, int);
DEFN_SYSCALL3(open, 5, const char*, int, int);
DEFN_SYSCALL1(unlink, 10, const char*);
DEFN_SYSCALL1(exec, 11, const char*);
DEFN_SYSCALL3(getdents, 141,int, void*,u32int);
DEFN_SYSCALL2(ipc_call, 150, int, ipc_msg_t*);

static void *syscalls[256] = {
    0,
    &exit,
    0,
    &read,
    &write,
    &open,
    0,
    0,
    0,
    0,
    &unlink,
    &exec
};

u32int num_syscalls = 256;

extern int64_t ax_syscall_surface(const char *title, int w, int h);
extern int ax_syscall_poll(uint32_t canvas_ptr, void *out);
extern int ax_syscall_time(void *out);
extern int ax_syscall_screen(void *out);
extern int compat_exec(const char *path);

void initialise_syscalls(void) {
    register_interrupt_handler(0x80, &syscall_handler);
    syscalls[141] = (void*)&getdents;
    syscalls[150] = (void*)&ipc_call;
}

static void syscall_handler(registers_t *regs) {
    if (regs->rax >= num_syscalls) {
        regs->rax = -1;
        return;
    }

    switch(regs->rax) {
         case 1:
            regs->rax = exit(regs->rdi);
            break;
        case 3:
            regs->rax = read(regs->rdi, (char*)regs->rsi, regs->rdx);
            break;
        case 4:
            regs->rax = write(regs->rdi, (char*)regs->rsi, regs->rdx);
            break;
        case 5:
            regs->rax = open((const char*)regs->rdi, regs->rsi, regs->rdx);
            break;
        case 10:
            regs->rax = unlink((const char*)regs->rdi);
            break;
         case 11:
            regs->rax = exec((const char*)regs->rdi);
            break;
        case 141:
            regs->rax = getdents(regs->rdi, (void*)regs->rsi, regs->rdx);
            break;

         case 150:
            regs->rax = ipc_call(regs->rdi, (void*)regs->rsi);
            break;
          case 151:
            yield();
            break;
        case 160:
            regs->rax = ax_syscall_surface((const char*)regs->rdi, regs->rsi, regs->rdx);
            break;
        case 161:
            regs->rax = ax_syscall_poll(regs->rdi, (void*)regs->rsi);
            break;
        case 162:
            regs->rax = 0;
            break;
        case 163:
            regs->rax = ax_syscall_time((void*)regs->rdi);
            break;
        case 164:
            regs->rax = ax_syscall_screen((void*)regs->rdi);
            break;
        case 170:
            regs->rax = compat_exec((const char*)regs->rdi);
            break;
        default:
            regs->rax = -1;
    }
}
