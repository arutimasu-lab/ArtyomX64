// syscalls.c
#include "syscalls.h"
#include "../drivers/monitor.h"
#include "../drivers/keyboard.h"
#include "../kernel/isr.h"
#include "../fs/fs.h"
#include "../fs/elf.h"
#include "../fs/initrd.h"
#include "../fs/task.h"
//#include "../mm/malloc.h"
#include "../mm/kheap.h"
#include "../lib/common.h"
#include "../lib/axipc.h"
#include "../dev/console.h"
#include <stddef.h>
#include <stdint.h>

#define TCFLSH 21515
#define malloc kmalloc
#define free kfree
typedef struct file {
    fs_node_t *node;
    u32int pos;
    u32int flags;
} file_t;

file_t *files[16384];
int current_fd = 2;

//extern void console_flush_input(void);
// Внешние функции из axshell
extern int64_t ax_syscall_surface(const char *title, int w, int h);
extern int ax_syscall_poll(uint32_t canvas_ptr, ax_event *out);
extern int ax_syscall_time(ax_time_t *out);
extern int ax_syscall_screen(ax_screen_t *out);
extern int ax_syscall_commit(uint32_t canvas_ptr);
extern void* image_load(char *elf_start, unsigned int size);
//extern int compat_exec(const char *path);


// Отладочный вывод
static void debug_puts(const char *s) {
    while(*s) {
        outb(0x3F8, *s++);
    }
}

static void debug_putc(char c) {
    outb(0x3F8, c);
}

void debug_putnum(uint64_t n) {
    char buf[32];
    int i = 0;
    if (n == 0) {
        debug_putc('0');
        return;
    }
    while (n) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i) {
        debug_putc(buf[--i]);
    }
}


int exit(int code) { 
    task_exit(code);
    debug_puts("EXIT: ");
    debug_putnum(code);
    debug_putc('\n');
    
    return code; 
}

int getdents(int fd, void *buf, u32int size) {
    file_t *f = files[fd];
    fs_node_t *node = f->node;

    if ((node->flags & 0x7) != FS_DIRECTORY)
        return -2;

    u32int written = 0;

    while (1) {
        struct dirent *d = readdir_fs(node, f->pos);
        if (!d)
            break;

        u32int namelen = strlen(d->name);
        u32int reclen = sizeof(u32int) + sizeof(u16int) + 1 + namelen + 1;
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

/*int write(int fd, char* buf, int nbytes) {
    if (fd != 1 && fd != 2) return -1;

    int start = 0;
    while (start < nbytes && buf[start] == 0) {
        start++;
    }

    for(int i = start; i < nbytes; i++) {
        if (buf[i] == 0) break;
        debug_putc(buf[i]);
    }

    return nbytes - start;
}*/
int write(int fd, char* buf, int nbytes) {
    file_t *f = files[fd];
    if (!f) return -1;

    int start = 0;
    while (start < nbytes && buf[start] == 0) start++;

    int ret = write_fs(f->node, f->pos, nbytes - start, (u8int*)(buf + start));
    if (ret > 0) f->pos += ret;
    return ret;
}

// syscalls.c - read syscall

int read(int fd, char *buf, int nbytes) {
    if (nbytes <= 0) return 0;

    file_t *f = files[fd];
    if (!f) return -1;

    if ((f->node->flags & 0x7) == FS_DIRECTORY)
        return -1;

    // Если данных нет, возвращаем 0 (не блокируем)
    // Блокировка реализована в пользовательском слое (unistd.h)
    int ret = read_fs(f->node, f->pos, nbytes, (u8int*)buf);
    if (ret > 0) f->pos += ret;
    
    return ret;
}
int open(const char* path, int flags, int mode) {
    fs_node_t *fsnode = finddir_fs(fs_root, (char*)path);
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

int unlink(const char* path) {
    return initrd_remove((char*)path) ? 0 : -1;
}

int ioctl(int fd, unsigned long com, char* data){
    (void)fd; // пока единственное устройство — console; при желании можно сверять files[fd]->node
    //if(com==TCFLSH && data==0)
        //console_flush_input();
    return 0;
 }

extern void *elf_load(const void *elf_data, size_t size, void *load_addr);
extern int call_elf32_thunk(unsigned int entry, unsigned int arg1, unsigned int arg2);

static int is_elf32_class(void *elf) {
    uint8_t *hdr = (uint8_t*)elf;
    if (hdr[EI_MAG0] != 0x7F || hdr[EI_MAG1] != 'E' ||
        hdr[EI_MAG2] != 'L' || hdr[EI_MAG3] != 'F')
        return 0;
    return hdr[EI_CLASS] == ELFCLASS32;
}

// syscalls.c - exec
int exec(const char* path) {
    debug_puts("EXEC: ");
    debug_puts(path);
    debug_putc('\n');
    
    fs_node_t *fsnode = finddir_fs(fs_root, (char*)path);
    if (fsnode == 0) {
        debug_puts("EXEC: file not found\n");
        return -1;
    }

    char buf[14000];
    memset(buf, 0x0, sizeof(buf));
    u32int sz = read_fs(fsnode, 0, sizeof(buf), buf);
    debug_puts("EXEC: read ");
    debug_putnum(sz);
    debug_putc('\n');

    void *ptr = image_load(buf, sz);
    if (ptr == NULL) {
        debug_puts("EXEC: load failed\n");
        return -2;
    }
 // === НАЧАЛО ПРАВИЛЬНОЙ НАСТРОЙКИ ПОТОКОВ ===
    
    // 1. Очищаем таблицу файлов НОВОГО процесса 
    // (чтобы дочерний процесс не наследовал файлы родителя "как есть")
    for (int i = 0; i < 16384; i++) {
        files[i] = NULL;
    }
    current_fd = 2; // Сбрасываем счетчик выделяемых FD

    // 2. Открываем нужные узлы ФС ВНУТРИ ЯДРА
    // Мы используем finddir_fs напрямую, минуя пользовательский open()
    fs_node_t *stdin_node = finddir_fs(fs_root, (char*)"pts"); // Или ваш pts узел
    fs_node_t *stdout_node = stdin_node; // Обычно они совпадают для TTY

    if (stdin_node) {
        // 3. Создаем записи file_t вручную
        int fd0 = current_fd++;
        files[fd0] = malloc(sizeof(file_t));
        files[fd0]->node = stdin_node;
        files[fd0]->pos = 0;
        files[fd0]->flags = 0;//O_RDONLY;

        int fd1 = current_fd++;
        files[fd1] = malloc(sizeof(file_t));
        files[fd1]->node = stdout_node;
        files[fd1]->pos = 0;
        files[fd1]->flags = 0;//O_WRONLY;
        
        int fd2 = current_fd++;
        files[fd2] = malloc(sizeof(file_t));
        files[fd2]->node = stdout_node;
        files[fd2]->pos = 0;
        files[fd2]->flags = 0;//O_WRONLY;
    }
    // Создаем задачу
    int pid = task_spawn((void(*)(void*))ptr, NULL, path);
    if (pid < 0) {
        debug_puts("EXEC: spawn failed\n");
        return -1;
    }
    
    debug_puts("EXEC: spawned PID=");
    debug_putnum(pid);
    debug_putc('\n');

    return pid;
}
int ipc_call(int endpoint, void* msg) { return -1; }

void syscall_handler(registers_t *regs) {
    /* debug_puts("SYSCALL_ENTER\n");
       uint64_t rsp;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    debug_puts("SYSCALL_RSP: ");
    debug_putnum(rsp);
    debug_putc('\n');
    debug_puts("SYSCALL: ");
    debug_putnum(regs->rax);
    debug_putc('\n');
    */
    switch(regs->rax) {
        case 1:  // exit
            regs->rax = exit(regs->rdi);
            break;
        case 3:  // read
            regs->rax = read(regs->rdi, (char*)regs->rsi, regs->rdx);
            break;
        case 4:  // write
            regs->rax = write(regs->rdi, (char*)regs->rsi, regs->rdx);
            break;
        case 5:  // open
            regs->rax = open((const char*)regs->rdi, regs->rsi, regs->rdx);
            break;
        case 10: // unlink
            regs->rax = unlink((const char*)regs->rdi);
            break;
        case 54:
            regs->rax = ioctl(regs->rdi, regs->rsi, regs->rdx);
            break;
        case 11: // exec
            regs->rax = exec((const char*)regs->rdi);
            break;
        case 141: // getdents
            regs->rax = getdents(regs->rdi, (void*)regs->rsi, regs->rdx);
            break;
        case 150: // ipc_call
            regs->rax = ipc_call(regs->rdi, (void*)regs->rsi);
            break;
        case 151: // yield
            yield();
             //debug_puts("YIELD\n");
            regs->rax = 0;
            break;
        case 160: // AX_SYS_SURFACE
            debug_puts("SYS_SURFACE\n");
            regs->rax = ax_syscall_surface((const char*)regs->rdi, regs->rsi, regs->rdx);
            break;
        case 161: // AX_SYS_POLL
            //debug_puts("SYS_POLL\n");
            regs->rax = ax_syscall_poll(regs->rdi, (void*)regs->rsi);
            break;
        case 162: // AX_SYS_COMMIT
            debug_puts("SYS_COMMIT\n");
             regs->rax = ax_syscall_commit(regs->rdi);
            //regs->rax = 0;
            break;
        case 163: // AX_SYS_TIME
            debug_puts("SYS_TIME\n");
            regs->rax = ax_syscall_time((void*)regs->rdi);
            break;
        case 164: // AX_SYS_SCREEN
            debug_puts("SYS_SCREEN\n");
            regs->rax = ax_syscall_screen((void*)regs->rdi);
            break;
        case 170: // compat_exec
            //regs->rax = compat_exec((const char*)regs->rdi);
            break;
        case 0xFFFFFFFFFFFFFFFF:  // -1
    debug_puts("SYSCALL_RAX_WAS_MINUS_ONE\n");
    regs->rax = -1;  // Просто возвращаем ошибку
    break;
        default:
            debug_puts("UNKNOWN_SYSCALL: ");
            debug_putnum(regs->rax);
            debug_putc('\n');
            regs->rax = -1;
    }
     //debug_puts("SYSCALL_RET: rax=");
    //debug_putnum(regs->rax);
    //debug_putc('\n');
}
void initialise_syscalls(void) {
    register_interrupt_handler(0x80, &syscall_handler);
}