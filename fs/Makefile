include ../include.mk
.POSIX:
.PHONY: clean 

all: fs.o ird.o ldr.o tsk.o switch.o


fs.o: fs.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
	
ird.o: initrd.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'

tsk.o: task.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
ldr.o: loader.c
	$(CC) -c $(CFLAGS) -c '$<' -o '$@'
switch.o: switch.S
	$(CC) -c -m64 -no-pie '$<' -o '$@'
	
clean:
	rm -f  *.o 
