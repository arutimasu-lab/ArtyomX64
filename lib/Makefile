# Makefile для руководств по сборке ядра - автор James Molloy.
# Правила обработки C и C++ указаны как используемые по умолчанию.
# Единственное, что нужно изменить, это указать правила обработки 
# ассемблера, поскольку мы используем nasm вместо GNU as.
include ../include.mk

.POSIX:
.PHONY: clean 

all: com.o sc.o libc.o gfxlib.o


com.o: common.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	#-m32 -std=gnu99 -ffreestanding -fno-builtin -Os -o '$@' -Wall -Wextra '$<'
	
sc.o: syscalls.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	#-m32 -std=gnu99 -ffreestanding -fno-builtin -Os -o '$@' -Wall -Wextra '$<'
libc.o: libc.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
gfxlib.o: gfxlib.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	#-m32 -std=gnu99 -ffreestanding -fno-builtin -Os -o '$@' -Wall -Wextra '$<'

	
clean:
	rm -f  *.o 

