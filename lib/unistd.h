#include "../lib/common.h"

#include <stddef.h>
#include <stdint.h>

#define TCFLSH 21515

// unistd.h - используйте обычный read без yield

// unistd.h - замените read на блокирующую версию

int read(int fd, void *buf, size_t count) {
    char *p = (char*)buf;
    size_t total = 0;
    
    while (total < count) {
        int ret;
        __asm__ volatile(
            "int $0x80"
            : "=a"(ret)
            : "a"(3), "D"((long)fd), "S"((long)(p + total)), "d"((long)(count - total))
            : "memory"
        );
        
        if (ret > 0) {
            total += ret;
            // Если получили новую строку - завершаем чтение
            if (p[total - 1] == '\n') break;
        } else if (ret == 0) {
            // Нет данных - ждем
            yield();  // Передаем управление другим задачам
        } else {
            // Ошибка
            return -1;
        }
    }
    
    // Добавляем null-терминатор
    if (total < count) p[total] = 0;
    return total;
}

// Если хотите блокирующий read, оставьте старую версию
// Но для консоли лучше не блокировать

int write(int fd, void *buf, size_t count) {
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(4), "D"((long)fd), "S"((long)buf), "d"((long)count)
        : "memory"
    );
    return ret;
}
int open(void *path, int flags, int mode){
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(5), "D"((long)path), "S"((long)flags), "d"((long)mode)
        : "memory"
    );
    return ret;
}
int unlink(void *path){
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(10), "D"((long)path)
        : "memory"
    );
    return ret;
}
int exec(const void *path){
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(11), "D"((long)path)
        : "memory"
    );
    return ret;
}

int ioctl(int fd, unsigned long com, char* data){
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(54), "D"((long)fd), "S"((long)com), "d"((long)data)
        : "memory"
    );
    return ret;
}

int getdents(int fd, void *buf, u32int size){
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(141), "D"((long)fd), "S"((long)buf), "d"((long)size)
        : "memory"
    );
    return ret;
}
int ipc_call(int endpoint, void* msg){
     int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(150), "D"((long)endpoint), "S"((long)msg)
        : "memory"
    );
    return ret;
}
void exit(int code){
     int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(1), "D"((long)code)
        : "memory"
    );
}
void yield(void){
     int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(151)
        : "memory"
    );
}

int compat_exec(const void *path){
    int ret;
    __asm__ volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(170), "D"((long)path)
        : "memory"
    );
    return ret;
}
