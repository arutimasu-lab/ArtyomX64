CC = gcc
LD = ld
CFLAGS = -m64 -nostdlib -ffreestanding -fno-stack-protector -fPIC -fPIE -O2 -Wall -Wextra -I../lib
LDFLAGS = -m elf_x86_64 -nostdlib -pie -e _start

APPS = term.bin test.bin calc.bin notes.bin prefs.bin xterm.bin #craft.bin

# Библиотека
#LIBC_SRC = ../lib/axgl.c ../mm/malloc.c
#LIBC_OBJ = ../lib/axgl.o ../mm/malloc.o

all: $(APPS)

# Компиляция libc
#$(LIBC_OBJ): $(LIBC_SRC)
#	$(CC) $(CFLAGS) -c $< -o $@

# Компиляция приложений и линковка с libc
%.o: %.c #$(LIBC_OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

%.bin: %.o #$(LIBC_OBJ)
	$(LD) $(LDFLAGS) $^ -o $@

initrd: all
	./mkinitrd \
		term.bin term test.bin test calc.bin calc \
		notes.bin notes prefs.bin prefs xterm.bin xterm #craft.bin craft
	cp initrd.img ../initrd.img

clean:
	rm -f *.o *.bin initrd.img
