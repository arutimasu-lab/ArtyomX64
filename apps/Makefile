CC = gcc
LD = ld
CFLAGS = -m64 -nostdlib -ffreestanding -fno-stack-protector  -fPIE -O2 -Wall -Wextra -I../lib \
 -mno-sse -mno-sse2 -mno-mmx -mno-sse3 -mno-ssse3 -fpie
LDFLAGS = -m elf_x86_64 -nostdlib -e _start -static -pie 

# Библиотека
LIBC_SRC = ../lib/libc.c 
LIBC_OBJ = libc.o

# Приложения
APPS =  test.bin term.bin notes.bin prefs.bin calc.bin files.bin xsh.bin hello.bin

all: $(APPS)

# Компиляция libc
libc.o: $(LIBC_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

%.bin: %.o libc.o
	$(LD) $(LDFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

initrd: all
	./mkinitrd \
	 test.bin test term.bin term \
		notes.bin notes prefs.bin prefs \
		files.bin files calc.bin calc  \
		xsh.bin xsh hello.bin hi
	cp initrd.img ../initrd.img

clean:
	rm -f *.o *.bin initrd.img
