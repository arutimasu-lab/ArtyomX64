# Makefile для руководств по сборке ядра - автор James Molloy.
# Правила обработки C и C++ указаны как используемые по умолчанию.
# Единственное, что нужно изменить, это указать правила обработки 
# ассемблера, поскольку мы используем nasm вместо GNU as.
include ../include.mk
.POSIX:
.PHONY: clean 

all: mon.o kb.o vga.o mouse.o tm.o  fb.o fnt.o pci.o #uhm.o
#video.o
#vesafb.o

pci.o: pci.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
#uhm.o: usb_hid_mouse.c
#	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

mon.o: monitor.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	#-m32 -std=gnu99 -ffreestanding -fno-builtin -Os -o '$@' -Wall -Wextra '$<'
tm.o: timer.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'	
kb.o: keyboard.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
mouse.o: mouse.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
#vga.o: vga-driver-main/vga.c
	#$(CC) -c $(CFLAGS) -c '$<' -o '$@'
vga.o: vga.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
fb.o: framebuffer.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
fnt.o: font.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
#io.o: vga-driver-main/io.c
	#$(CC) -c $(CFLAGS) -c '$<' -o '$@'

#video.o: video.c
	#$(CC) -c $(CFLAGS) -c '$<' -o '$@'
#video.o: video.c
	#$(CC) -c $(CFLAGS) -c '$<' -o '$@'

#intel_hd.o: intel_hd.c
	#$(CC) -c $(CFLAGS) -c '$<' -o '$@'
#vesafb.o: graphics/vesafb.c
	#$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	#-m32 -std=gnu99 -ffreestanding -fno-builtin -Os -o '$@' -Wall -Wextra '$<'
	
clean:
	rm -f  *.o 

