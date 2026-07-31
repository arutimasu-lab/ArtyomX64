# Makefile для руководств по сборке ядра - автор James Molloy.
# Правила обработки C и C++ указаны как используемые по умолчанию.
# Единственное, что нужно изменить, это указать правила обработки 
# ассемблера, поскольку мы используем nasm вместо GNU as.
include ../include.mk
.POSIX:
.PHONY: clean 

all: malloc.o pmm.o vmm.o


malloc.o: malloc.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

pmm.o: pmm.c pmm.h
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

vmm.o: vmm.c vmm.h pmm.h
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	
clean:
	rm -f  *.o 

