#include "../lib/unistd.h"

int _start() {
    char name[20];
    int n = 0;
    int total = 0;
    
    write(1, "Hello, world!\n", 14);
    write(1, "What's your name?: ", 19);
    
    // Блокирующий read с yield
    while (total < sizeof(name) - 1) {
        n = read(0, name + total, 1);
        if (n > 0) {
            if (name[total] == '\n') break;
            total++;
        } else {
            yield();  // Ждем ввода
        }
    }
    
    if (total > 0) {
        name[total] = 0;
        write(1, "Your name is '", 14);
        write(1, name, total);
        write(1, "'\n", 2);
    }
    
    return 0;
}