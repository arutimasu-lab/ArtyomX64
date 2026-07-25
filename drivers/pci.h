#ifndef PCI_H
#define PCI_H

#include "../lib/common.h"

u32int pci_read32(u8int bus, u8int slot, u8int func, u8int offset);
u16int pci_read16(u8int bus, u8int slot, u8int func, u8int offset);
u8int  pci_read8 (u8int bus, u8int slot, u8int func, u8int offset);
void   pci_write32(u8int bus, u8int slot, u8int func, u8int offset, u32int value);
void   pci_write16(u8int bus, u8int slot, u8int func, u8int offset, u16int value);

/* prog_if = 0xFF означает "любой". Возвращает 1 и заполняет bus/slot/func
 * при первом найденном совпадении класс/подкласс/prog-if, иначе 0. */
int pci_find_class(u8int class_code, u8int subclass, u8int prog_if,
                    u8int *out_bus, u8int *out_slot, u8int *out_func);

#endif