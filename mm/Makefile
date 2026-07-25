# Makefile для руководств по сборке ядра - автор James Molloy.
# Правила обработки C и C++ указаны как используемые по умолчанию.
# Единственное, что нужно изменить, это указать правила обработки 
# ассемблера, поскольку мы используем nasm вместо GNU as.
include ../include.mk
.POSIX:
.PHONY: clean 

all: pmm.o kheap.o


#malloc.o: malloc.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	#-m32 -std=gnu99 -ffreestanding -fno-builtin -Os -o '$@' -Wall -Wextra '$<'
pmm.o: pmm.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
kheap.o: kheap.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

clean:
	rm -f  *.o 

