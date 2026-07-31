#include "../lib/common.h"
#include "isr.h"
#include "../drivers/monitor.h"
#include "../fs/task.h"
#include "../mm/vmm.h"

isr_t interrupt_handlers[256];
extern void init_shell(void);

int in_program = 0;

static void serial_pf_putc(char c){ outb(0x3F8, c); }
static void serial_pf_puts(const char *s){ while(*s) serial_pf_putc(*s++); }

static void page_fault_handler(registers_t *regs)
{
    uint64_t cr2;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    uint64_t err = regs->err_code;
    bool from_user = (regs->cs & 3) == 3;

    serial_pf_puts("PF: addr=");
    {
        char buf[17]; int i;
        for (i = 15; i >= 0; i--) {
            int nib = (cr2 >> (i * 4)) & 0xF;
            buf[15 - i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        }
        buf[16] = 0;
        serial_pf_puts(buf);
    }
    serial_pf_puts(" err=");
    {
        char buf[17]; int i;
        for (i = 15; i >= 0; i--) {
            int nib = (err >> (i * 4)) & 0xF;
            buf[15 - i] = nib < 10 ? '0' + nib : 'a' + nib - 10;
        }
        buf[16] = 0;
        serial_pf_puts(buf);
    }
    serial_pf_puts(" user=");
    serial_pf_putc(from_user ? '1' : '0');
    serial_pf_putc('\n');

    vas_t *as = task_current_vas();
    if (as && vmm_handle_fault(as, cr2, err))
        return;

    if (from_user) {
        monitor_write("\n[segv] pid=");
        monitor_write_dec(task_current_pid());
        monitor_write(" addr=0x");
        monitor_write_hex((u32int)(cr2 & 0xFFFFFFFF));
        monitor_write(" killed\n");
        task_signal_send_fault(task_current_pid(), 11, cr2, (uint32_t)err);
        task_signal_deliver();
        task_exit_code(139);
        __builtin_unreachable();
    }

    monitor_write("\n[kernel page fault] addr=0x");
    monitor_write_hex((u32int)(cr2 & 0xFFFFFFFF));
    monitor_write(" err=0x");
    monitor_write_hex((u32int)(err & 0xFFFFFFFF));
    monitor_write("\n");
    for (;;) __asm__ volatile("cli; hlt");
}

void register_interrupt_handler(u8int n, isr_t handler)
{
    interrupt_handlers[n] = handler;
}

void isr_handler(registers_t *regs)
{
    u8int int_no = (u8int)(regs->int_no & 0xFF);

    if (int_no == 14) {
        page_fault_handler(regs);
        return;
    }

    if (int_no <= 31) {
        bool from_user = (regs->cs & 3) == 3;
        monitor_write("\nException ");
        monitor_write_dec(int_no);
        monitor_write(" occurred. Returning to shell.\n");
        if (from_user) {
            int sig = 0;
            if (int_no == 0) sig = 8;
            else if (int_no == 6) sig = 4;
            else if (int_no == 8) sig = 11;
            else if (int_no == 13) sig = 11;
            if (sig) {
                task_signal_send(task_current_pid(), sig);
                task_signal_deliver();
                return;
            }
        }
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
