#include "keyboard.h"
#include "monitor.h"
#include "keyboard_map.h"
#include "../kernel/isr.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64
#define ENTER_KEY_CODE 0x1C
#define UP_KEY_CODE 0x48
#define LEFT_KEY_CODE 0x4B
#define RIGHT_KEY_CODE 0x4D
#define DOWN_KEY_CODE 0x50

#define BUFFER_SIZE 256

// Статические переменные буфера
static char keyboard_buffer[BUFFER_SIZE];
static int buffer_head = 0;
static int buffer_tail = 0;
static int buffer_count = 0;
int is_enter_pressed = 0;
char current_key;
// Проверка пустоты буфера
int is_buffer_empty() {
    return buffer_count == 0;
}

int is_buffer_full() {
    return buffer_count >= BUFFER_SIZE - 1;
}

// Проверка доступности данных (для syscall)
int keyboard_buffer_available() {
    return buffer_count > 0;
}

// Очистка буфера
void keyboard_clear_buffer() {
    buffer_head = 0;
    buffer_tail = 0;
    buffer_count = 0;
    is_enter_pressed = 0;
}

// Основная функция чтения (используется в syscall)
char keyboard_read() {
    if (is_buffer_empty()) {
        return -1;
    }

    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
    buffer_count--;
    return c;
}

// Обработчик прерывания клавиатуры
static void keyboard_callback(registers_t regs) {
    unsigned char status;
    char keycode;

    status = inb(KEYBOARD_STATUS_PORT);
    
    if (status & 0x01) {  // Данные доступны
        keycode = inb(KEYBOARD_DATA_PORT);
        
        // Игнорируем отпускание клавиши (старший бит установлен)
        if (keycode & 0x80) {
            goto end;
        }

        current_key = keycode;
        
        // Обработка Enter
        if (keycode == ENTER_KEY_CODE) {
            if (!is_buffer_full()) {
                keyboard_buffer[buffer_head] = '\n';
                buffer_head = (buffer_head + 1) % BUFFER_SIZE;
                buffer_count++;
            }
            is_enter_pressed = 1;
            goto end;
        }
        
        // Backspace
        if (keycode == 0x0E) {
            if (buffer_count > 0) {
                buffer_head = (buffer_head - 1 + BUFFER_SIZE) % BUFFER_SIZE;
                buffer_count--;
                // Выводим backspace на экран
                monitor_put('\b');
                monitor_put(' ');
                monitor_put('\b');
            }
            goto end;
        }
        
        // Получаем символ из карты
        char c = keyboard_map[(unsigned int)keycode];
        if (c == 0) {
            goto end;  // Игнорируем специальные клавиши
        }
        
        // Добавляем в буфер, если не полный
        if (!is_buffer_full()) {
            keyboard_buffer[buffer_head] = c;
            buffer_head = (buffer_head + 1) % BUFFER_SIZE;
            buffer_count++;
            
            // Эхо на экран
            monitor_put(c);
        }
        


    }
    
end:
    // Отправляем EOI (End Of Interrupt)
    outb(0x20, 0x20);
}

// Инициализация клавиатуры
void init_keyboard() {
    keyboard_clear_buffer();
    register_interrupt_handler(IRQ1, &keyboard_callback);
   // outb(0x21, 0xFD);  // Разрешаем только IRQ1 (клавиатура)
}
