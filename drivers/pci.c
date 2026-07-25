// pci.c — минимальный доступ к конфигурационному пространству PCI
// через порты 0xCF8/0xCFC (механизм #1, работает почти на всём x86).
#include "pci.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static u32int pci_make_addr(u8int bus, u8int slot, u8int func, u8int offset)
{
    return (u32int)(
        (1u << 31) |
        ((u32int)bus  << 16) |
        ((u32int)slot << 11) |
        ((u32int)func << 8)  |
        (offset & 0xFC));
}

u32int pci_read32(u8int bus, u8int slot, u8int func, u8int offset)
{
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(u8int bus, u8int slot, u8int func, u8int offset, u32int value)
{
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

u16int pci_read16(u8int bus, u8int slot, u8int func, u8int offset)
{
    u32int v = pci_read32(bus, slot, func, offset & 0xFC);
    return (u16int)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

u8int pci_read8(u8int bus, u8int slot, u8int func, u8int offset)
{
    u32int v = pci_read32(bus, slot, func, offset & 0xFC);
    return (u8int)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write16(u8int bus, u8int slot, u8int func, u8int offset, u16int value)
{
    u32int v = pci_read32(bus, slot, func, offset & 0xFC);
    u32int shift = (offset & 2) * 8;
    v = (v & ~(0xFFFFu << shift)) | ((u32int)value << shift);
    pci_write32(bus, slot, func, offset & 0xFC, v);
}

int pci_find_class(u8int class_code, u8int subclass, u8int prog_if,
                    u8int *out_bus, u8int *out_slot, u8int *out_func)
{
    for (u16int bus = 0; bus < 256; bus++) {
        for (u8int slot = 0; slot < 32; slot++) {
            u16int vendor = pci_read16((u8int)bus, slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;

            u8int header_type = pci_read8((u8int)bus, slot, 0, 0x0E);
            u8int func_count = (header_type & 0x80) ? 8 : 1;

            for (u8int func = 0; func < func_count; func++) {
                u16int v2 = pci_read16((u8int)bus, slot, func, 0x00);
                if (v2 == 0xFFFF) continue;

                u32int classreg = pci_read32((u8int)bus, slot, func, 0x08);
                u8int cc  = (u8int)((classreg >> 24) & 0xFF);
                u8int sc  = (u8int)((classreg >> 16) & 0xFF);
                u8int pif = (u8int)((classreg >> 8)  & 0xFF);

                if (cc == class_code && sc == subclass &&
                    (prog_if == 0xFF || pif == prog_if)) {
                    *out_bus  = (u8int)bus;
                    *out_slot = slot;
                    *out_func = func;
                    return 1;
                }
            }
        }
    }
    return 0;
}
