.POSIX:
.PHONY: all clean run run-img run-limine limine-iso grub-iso

LIMINE_BIN := limine-binary
LIMINE_TOOL := $(LIMINE_BIN)/limine-tool-windows-x86/limine.exe
LIMINE_ISO_DIR := limine-iso-root

all:
	$(MAKE) -C kernel
	$(MAKE) -C drivers
	#$(MAKE) -C drivers/gpu
	#$(MAKE) -C drivers/usb
	$(MAKE) -C fs
	$(MAKE) -C lib
	$(MAKE) -C mm
	$(MAKE) main.elf

main.iso: limine-iso

main.elf: kasm.o boot/limine_boot.o lib/com.o lib/sc.o lib/libc.o lib/gfxlib.o drivers/fb.o drivers/fnt.o drivers/mon.o drivers/tm.o drivers/kb.o drivers/vga.o drivers/mouse.o drivers/pci.o fs/fs.o fs/ird.o fs/ldr.o fs/tsk.o fs/switch.o mm/kheap.o mm/pmm.o dev/tty.o kernel/kern.o kernel/dt.o kernel/gdt.o kernel/isr.o kernel/int.o kernel/ksh.o kernel/vgafnt.o kernel/thunk.o kernel/axsh.o kernel/compat.o kernel/compat_asm.o #drivers/usb/usb_core.o drivers/usb/uhci.o drivers/usb/ehci.o drivers/uhm.o
	#drivers/gpu/ixg_driver.o drivers/gpu/ixg_shader_vm.o drivers/gpu/ixg_isa.o drivers/gpu/ixg_isa_encode.o drivers/gpu/ixg_sass.o drivers/gpu/ixg_sass_encode.o drivers/gpu/ixg_mutants.o drivers/gpu/ixg_compute.o drivers/gpu/ixg_backend_amd.o drivers/gpu/ixg_backend_intel.o drivers/gpu/ixg_backend_nvidia.o drivers/gpu/ixg_pman.o
	ld -m elf_x86_64 -nostdlib -T link.ld $^ -o '$@'

kasm.o: boot/itdo.asm
	nasm -f elf64 -w-label-redef-late -w-implicit-abs-deprecated '$<' -o '$@'

boot/limine_boot.o: boot/limine_boot.c boot/limine.h boot/multiboot.h
	gcc -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -std=c99 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -O2 -Wall -Wextra -Wno-incompatible-pointer-types -c '$<' -o '$@'

#dev/con.o: dev/console.c
dev/tty.o: dev/tty.c
	gcc -m64 -mcmodel=kernel -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -std=c99 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -O2 -Wall -Wextra -Wno-incompatible-pointer-types -c '$<' -o '$@'
limine-iso: main.elf
	rm -rf $(LIMINE_ISO_DIR)
	mkdir -p $(LIMINE_ISO_DIR)/boot/limine
	cp main.elf $(LIMINE_ISO_DIR)/boot/
	cp apps/initrd.img $(LIMINE_ISO_DIR)/boot/
	cp boot/limine/limine.conf $(LIMINE_ISO_DIR)/boot/limine/
	cp $(LIMINE_BIN)/limine-bios.sys $(LIMINE_ISO_DIR)/boot/limine/
	cp $(LIMINE_BIN)/limine-bios-cd.bin $(LIMINE_ISO_DIR)/boot/limine/
	cp $(LIMINE_BIN)/limine-uefi-cd.bin $(LIMINE_ISO_DIR)/boot/limine/
	mkdir -p $(LIMINE_ISO_DIR)/EFI/BOOT
	cp $(LIMINE_BIN)/BOOTX64.EFI $(LIMINE_ISO_DIR)/EFI/BOOT/
	cp $(LIMINE_BIN)/BOOTIA32.EFI $(LIMINE_ISO_DIR)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		$(LIMINE_ISO_DIR) -o main.iso
	$(LIMINE_TOOL) bios-install main.iso

grub-iso: main.elf
	cp '$<' iso/boot
	grub-mkrescue -o main-grub.iso iso

clean:
	rm -f *.elf *.o iso/boot/*.elf iso/boot/*.img *.img main-grub.iso
	find ./kernel -name "*.o" -type f -delete
	find ./fs -name "*.o" -type f -delete
	find ./drivers -name "*.o" -type f -delete
	find ./lib -name "*.o" -type f -delete
	find ./mm -name "*.o" -type f -delete
	find ./boot -name "*.o" -type f -delete
	$(MAKE) -C drivers/gpu clean
	rm -rf $(LIMINE_ISO_DIR)

run: main.elf
	qemu-system-x86_64 -kernel '$<' -initrd apps/initrd.img

run-img: main.iso
	qemu-system-x86_64 -cdrom '$<' \
  -m 256M \
  -vga std

run-limine: main.iso
	qemu-system-x86_64 -device usb-ehci,id=ehci \
	-cdrom '$<' \
  -m 512M \
  -serial stdio
#-device usb-ehci,id=ehci \