// hello.c - простое приложение для вашей ОС
#include "../lib/common.h"
#include "../lib/unistd.h"
#include "../drivers/vga.h"
#include "../lib/ipc.h"
#include "../lib/gfxlib.h"
#include <stdbool.h>
// Точка входа должна быть _start, а не main
void _start() {
   /* puts("Hello from user program!");
    puts("This is my custom OS!");
    
    // Чтение с клавиатуры
    char buffer[64];
    puts("Enter your name: ");
    read(0, buffer, sizeof(buffer));
    
    puts("Hello, ");
    write(1, buffer, strlen(buffer));
    */

struct Window {
    int id;
    int x;
    int y;
    bool closed;

    bool dragging;
    int drag_off_x;
    int drag_off_y;
    //#define CLOSE_X (x + 5)
    //#define CLOSE_Y (y + 5)
};

struct Window* active_window = NULL;

struct Window window   = {0, 55, 55, true, false, 0, 0};
struct Window window_1 = {1, 100, 100, true, false, 0, 0};
//ipc_call(GFX_DRIVER, &m);
//ipc_call(GFX_DRIVER, &m1);
char buffer = 0;
while(buffer!='c'){
    //ipc_call(GFX_DRIVER, &m3);
    draw_rectangle(window.x, window.y, 110, 100,VGA_COLOR_LIGHT_GRAY);
    read(0,&buffer,1);
}

exit(0);
    // Выход из программы
    // Пока что просто возврат, позже добавим exit()
    /*asm volatile(
        "mov $1, %eax\n\t"  // Номер syscall для exit (нужно добавить)
        "int $0x80"
    );*/
}