#!/bin/bash

KERNEL=kernel.elf
INITRD=initrd.img
ISO_DIR=isodir
ISO=artyomx.iso

mkdir -p $ISO_DIR/boot/grub

cp $KERNEL $INITRD $ISO_DIR/boot/

cat > $ISO_DIR/boot/grub/grub.cfg << EOF
set timeout=5
set default=0

menuentry "ArtyomX OS" {
    set root=(cd)
    multiboot /boot/$KERNEL
    module /boot/$INITRD
}
EOF

grub-mkrescue -o $ISO $ISO_DIR --modules="multiboot"

echo "ISO создан: $ISO"
echo "Тестируем: qemu-system-x86_64 -cdrom $ISO -serial stdio"
