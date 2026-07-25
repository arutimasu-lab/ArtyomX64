#include "../lib/common.h"
int write(int fd, void *buf, unsigned long count);


int memcmp(const void *ptr1, const void *ptr2, size_t num)
{
    const unsigned char *a = (const unsigned char *)ptr1;
    const unsigned char *b = (const unsigned char *)ptr2;

    for (size_t i = 0; i < num; i++)
    {
        if (a[i] != b[i])
            return (int)a[i] - (int)b[i];
    }
    return 0;
}

// Copy len bytes from src to dest.
void memcpy(u8int *dest, const u8int *src, u32int len)
{
    const u8int *sp = (const u8int *)src;
    u8int *dp = (u8int *)dest;
    for(; len != 0; len--) *dp++ = *sp++;
}

// Write len copies of val into dest.
void memset(u8int *dest, u8int val, u32int len)
{
    u8int *temp = (u8int *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

void *memmove(void *dest, const void *src, u32int n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0) {
        return dest; // ничего не нужно делать
    }

    // Если область назначения перекрывает область источника, сначала копируем в буфер
    if (d < s || d >= s + n) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        // Если область назначения перекрывает область источника, копируем в обратном порядке
        d += n;
        s += n;
        while (n--) {
            *(--d) = *(--s);
        }
    }

    return dest;
}

// Compare two strings. Should return -1 if 
// str1 < str2, 0 if they are equal or 1 otherwise.
int strcmp(char *str1, char *str2)
{
      int i = 0;
      int failed = 0;
      while(str1[i] != '\0' && str2[i] != '\0')
      {
          if(str1[i] != str2[i])
          {
              failed = 1;
              break;
          }
          i++;
      }
      // why did the loop exit?
      if( (str1[i] == '\0' && str2[i] != '\0') || (str1[i] != '\0' && str2[i] == '\0') )
          failed = 1;
  
      return failed;
}

// Copy the NULL-terminated string src into dest, and
// return dest.
char *strcpy(char *dest, const char *src)
{
    do
    {
      *dest++ = *src++;
    }
    while (*src != 0);
}

char* strncpy(char *dest, const char *src, size_t n)
{
    size_t i;

   for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for ( ; i < n; i++)
        dest[i] = '\0';

   return dest;
}

// Concatenate the NULL-terminated string src onto
// the end of dest, and return dest.
char *strcat(char *dest, const char *src)
{
    while (*dest != 0)
    {
        *dest = *dest++;
    }

    do
    {
        *dest++ = *src++;
    }
    while (*src != 0);
    return dest;
}

int strlen(char *src)
{
    int i = 0;
    while (*src++)
        i++;
    return i;
}

unsigned int is_delim(char c, char *delim)
{
    while(*delim != '\0')
    {
        if(c == *delim)
            return 1;
        delim++;
    }
    return 0;
}
char *strtok(char *srcString, char *delim)
{
    static char *backup_string; // start of the next search
    if(!srcString)
    {
        srcString = backup_string;
    }
    if(!srcString)
    {
        // user is bad user
        return 0;
    }
    // handle beginning of the string containing delims
    while(1)
    {
        if(is_delim(*srcString, delim))
        {
            srcString++;
            continue;
        }
        if(*srcString == '\0')
        {
            // we've reached the end of the string
            return 0; 
        }
        break;
    }
    char *ret = srcString;
    while(1)
    {
        if(*srcString == '\0')
        {
            /*end of the input string and
            next exec will return NULL*/
            backup_string = srcString;
            return ret;
        }
        if(is_delim(*srcString, delim))
        {
            *srcString = '\0';
            backup_string = srcString + 1;
            return ret;
        }
        srcString++;
    }
}

// Простые функции ввода-вывода
void putchar(char c) {
    write(1, &c, 1);
}

void puts(const char *s) {
    write(1, s, strlen(s));
    putchar('\n');
}

int atoi(char *p) {
    int k = 0;
    while (*p) {
        k = (k << 3) + (k << 1) + (*p) - '0';
        p++;
     }
     return k;
}



// Добавь в common.c или в отдельный printf.c

#include "../lib/common.h"
#include <stdarg.h>  // если есть, иначе реализуем вручную

// Если stdarg.h нет — вот минимальная реализация для x86_64
#ifndef _STDARG_H
#define _STDARG_H
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,l)   __builtin_va_arg(v,l)
#endif

// Вспомогательные функции
static void print_char(char c) {
    write(1, &c, 1);
}

static void print_string(const char *s) {
    if (!s) {
        write(1, "(null)", 6);
        return;
    }
    while (*s) {
        print_char(*s++);
    }
}

// Печать целого числа (знакового)
static void print_int(int num, int base) {
    char buf[32];
    int i = 0;
    int negative = 0;
    
    if (num == 0) {
        print_char('0');
        return;
    }
    
    if (num < 0 && base == 10) {
        negative = 1;
        num = -num;
    }
    
    while (num > 0) {
        int digit = num % base;
        buf[i++] = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        num /= base;
    }
    
    if (negative) {
        print_char('-');
    }
    
    while (i > 0) {
        print_char(buf[--i]);
    }
}

// Печать беззнакового целого
static void print_uint(unsigned int num, int base) {
    char buf[32];
    int i = 0;
    
    if (num == 0) {
        print_char('0');
        return;
    }
    
    while (num > 0) {
        int digit = num % base;
        buf[i++] = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        num /= base;
    }
    
    while (i > 0) {
        print_char(buf[--i]);
    }
}

// Печать 64-битного беззнакового (для указателей)
static void print_uint64(uint64_t num, int base) {
    char buf[32];
    int i = 0;
    
    if (num == 0) {
        print_char('0');
        return;
    }
    
    while (num > 0) {
        int digit = num % base;
        buf[i++] = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        num /= base;
    }
    
    while (i > 0) {
        print_char(buf[--i]);
    }
}

// Главная функция форматирования
static void vprintf(const char *fmt, va_list args) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            print_char(*p);
            continue;
        }
        
        p++; // пропускаем '%'
        
        switch (*p) {
            case '%':
                print_char('%');
                break;
                
            case 'd':  // десятичное знаковое
            case 'i':
                print_int(va_arg(args, int), 10);
                break;
                
            case 'u':  // десятичное беззнаковое
                print_uint(va_arg(args, unsigned int), 10);
                break;
                
            case 'x':  // шестнадцатеричное (строчные)
                print_uint(va_arg(args, unsigned int), 16);
                break;
                
            case 'X':  // шестнадцатеричное (заглавные)
                print_uint(va_arg(args, unsigned int), 16); // можно допилить регистр
                break;
                
            case 'o':  // восьмеричное
                print_uint(va_arg(args, unsigned int), 8);
                break;
                
            case 's':  // строка
                print_string(va_arg(args, const char *));
                break;
                
            case 'c':  // символ
                print_char((char)va_arg(args, int));
                break;
                
            case 'p':  // указатель
                void *ptr = va_arg(args, void *);
                if (ptr == NULL) {
                    print_string("(nil)");
                } else {
                    print_string("0x");
                    print_uint64((uint64_t)ptr, 16);
                }
                break;
                
            case 'l':  // long (для 32-бит = 32 бита, для 64-бит = 64 бита)
                p++; // смотрим следующий символ
                if (*p == 'd' || *p == 'i') {
                    print_int(va_arg(args, long), 10);
                } else if (*p == 'u') {
                    print_uint(va_arg(args, unsigned long), 10);
                } else if (*p == 'x') {
                    print_uint(va_arg(args, unsigned long), 16);
                } else if (*p == 'l') { // ll
                    p++;
                    if (*p == 'd' || *p == 'i') {
                        print_int(va_arg(args, long long), 10);
                    } else if (*p == 'u') {
                        print_uint(va_arg(args, unsigned long long), 10);
                    } else if (*p == 'x') {
                        print_uint(va_arg(args, unsigned long long), 16);
                    }
                }
                break;
                
            case 'z':  // size_t
                p++;
                if (*p == 'u') {
                    print_uint(va_arg(args, size_t), 10);
                } else if (*p == 'x') {
                    print_uint(va_arg(args, size_t), 16);
                }
                break;
                
            default:
                // неизвестный спецификатор — печатаем как есть
                print_char('%');
                print_char(*p);
                break;
        }
    }
}

// Публичный API
int printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    return 0; // можно вернуть количество напечатанных символов
}

// sprintf — форматирование в строку
int sprintf(char *buf, const char *fmt, ...) {
    // Простая реализация: перенаправляем write во временный буфер
    // Для этого нужно модифицировать vprintf, но пока так:
    va_list args;
    va_start(args, fmt);
    
    char temp[4096]; // временный буфер
    // Сохраняем старый обработчик write и подменяем
    // Но это сложно, лучше сделать отдельную функцию vsprintf
    
    va_end(args);
    
    // Заглушка — TODO: сделать нормальную реализацию
    buf[0] = '\0';
    return 0;
}
