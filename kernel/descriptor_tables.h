#ifndef DESCRIPTOR_TABLES_H
#define DESCRIPTOR_TABLES_H

#include "../lib/common.h"

void init_descriptor_tables(void);

struct gdt_entry_struct
{
    u16int limit_low;
    u16int base_low;
    u8int  base_middle;
    u8int  access;
    u8int  granularity;
    u8int  base_high;
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_ptr_struct
{
    u16int limit;
    u64int base;
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

struct idt_entry_struct
{
    u16int base_low;
    u16int sel;
    u8int  ist;
    u8int  flags;
    u16int base_mid;
    u32int base_high;
    u32int zero;
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct
{
    u16int limit;
    u64int base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

#define GDT_NULL      0x00
#define GDT_KERNEL_CS 0x08
#define GDT_KERNEL_DS 0x10
#define GDT_USER_CS   0x18
#define GDT_USER_DS   0x20
#define GDT_COMPAT32_CS 0x28
#define GDT_COMPAT32_DS 0x30
#define GDT_COMPAT16_CS 0x38
#define GDT_COMPAT16_DS 0x40

extern void isr0 (void);
extern void isr1 (void);
extern void isr2 (void);
extern void isr3 (void);
extern void isr4 (void);
extern void isr5 (void);
extern void isr6 (void);
extern void isr7 (void);
extern void isr8 (void);
extern void isr9 (void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);
extern void irq0 (void);
extern void irq1 (void);
extern void irq2 (void);
extern void irq3 (void);
extern void irq4 (void);
extern void irq5 (void);
extern void irq6 (void);
extern void irq7 (void);
extern void irq8 (void);
extern void irq9 (void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);
extern void isr128(void);
extern void isr129(void);
extern void isr130(void);

#endif
