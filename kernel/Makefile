include ../include.mk
.POSIX:
.PHONY: clean 

all: kern.o dt.o isr.o gdt.o int.o ksh.o vgafnt.o thunk.o axsh.o compat.o compat_asm.o


kern.o: main.c
	$(CC) $(CFLAGS) -c '$<' -o '$@'

dt.o: descriptor_tables.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	
isr.o: isr.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
ksh.o: ksh.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
vgafnt.o: vgafnt.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

axsh.o: axshell.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

gdt.o: asm/gdt.asm
	nasm -f elf64 '$<' -o '$@'
	
int.o: asm/interrupt.asm
	nasm -f elf64 '$<' -o '$@'

thunk.o: asm/thunk.asm
	nasm -f elf64 '$<' -o '$@'

compat.o: compat.c compat.h macho.h
	$(CC) $(CFLAGS) -c '$<' -o '$@'

compat_asm.o: asm/compat.asm
	nasm -f elf64 '$<' -o '$@'

clean:
	rm -f  *.o
