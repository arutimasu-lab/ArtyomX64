#include "../lib/common.h"
#include "isr.h"
#include "../drivers/monitor.h"

isr_t interrupt_handlers[256];
extern void init_shell(void);

int in_program = 0;

void register_interrupt_handler(u8int n, isr_t handler)
{
    interrupt_handlers[n] = handler;
}

void isr_handler(registers_t *regs)
{
    u8int int_no = (u8int)(regs->int_no & 0xFF);

    if (int_no <= 31) {
        monitor_write("\nException ");
        monitor_write_dec(int_no);
        monitor_write(" occurred. Returning to shell.\n");
    }

    if (int_no < 256 && interrupt_handlers[int_no] != 0)
    {
        isr_t handler = interrupt_handlers[int_no];
        handler(regs);
    }
}

void irq_handler(registers_t *regs)
{
    if (regs->int_no >= 40)
    {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);

    if (interrupt_handlers[regs->int_no] != 0)
    {
        isr_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
}
