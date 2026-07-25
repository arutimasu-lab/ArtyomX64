# Makefile для руководств по сборке ядра - автор James Molloy.
# Правила обработки C и C++ указаны как используемые по умолчанию.
# Единственное, что нужно изменить, это указать правила обработки 
# ассемблера, поскольку мы используем nasm вместо GNU as.
include ../../include.mk
.POSIX:
.PHONY: clean 

all: usb_core.o uhci.o ehci.o

usb_core.o: usb_core.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'


uhci.o: uhci.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'	
ehci.o: ehci.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'	

clean:
	rm -f  *.o 

