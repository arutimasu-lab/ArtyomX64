#include <stdint.h>
#include <stddef.h>
#include "../drivers/monitor.h"
#include "../drivers/keyboard.h"
#include "descriptor_tables.h"
#include "../boot/multiboot.h"
#include "../fs/fs.h"
#include "../fs/initrd.h"
#include "../fs/task.h"
#include "../lib/syscalls.h"
#include "../drivers/mouse.h"
#include "../drivers/timer.h"
#include "../drivers/usb.h"
#include "../mm/malloc.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "compat.h"
#include "linux_sys.h"
#include "ksock.h"
#include "kpty.h"
#include "../drivers/net/ixn_net.h"
#include "../drivers/net/ixn_socket_api.h"

static void serial_putc(char c)
{
    outb(0x3F8, c);
}

static void serial_puts(const char *s)
{
    while (*s) serial_putc(*s++);
}

extern char _heap_start[];
extern char _heap_end[];

struct multiboot;

#include "../lib/common.h"

extern uint64_t framebuffer_addr;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;
extern uint32_t framebuffer_bpp;
extern void axshell_main(void);

void fb_fill(uint32_t color)
{
    uint8_t *fb = (uint8_t*)(uintptr_t)framebuffer_addr;

    for (uint32_t y = 0; y < framebuffer_height; y++) {
        uint32_t *row = (uint32_t*)(fb + y * framebuffer_pitch);
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            row[x] = color;
        }
    }
}

void init_shell(void){
    init_descriptor_tables();
    __asm__ volatile("sti");
    init_keyboard();
    initialise_syscalls();
    compat_register_handlers();
}

int main(struct multiboot *mboot_ptr)
{
    serial_puts("MAIN\n");
    __asm__ volatile("cli");
    serial_puts("DT\n");
    init_descriptor_tables();
    serial_puts("DT_OK\n");

    ASSERT(mboot_ptr->mods_count > 0);
    serial_puts("MODS_OK\n");
    u64int initrd_location = *((u64int*)(uintptr_t)mboot_ptr->mods_addr);

    serial_puts("MALLOC\n");
    malloc_init((void*)_heap_start, (size_t)(_heap_end - _heap_start));
    serial_puts("MALLOC_OK\n");

    serial_puts("PMM\n");
    pmm_init(0x01000000ull, 0x10000000ull);
    serial_puts("PMM_OK\n");

    serial_puts("VMM\n");
    vmm_init(0xffff800000000000ull);
    serial_puts("VMM_OK\n");

    serial_puts("KSOCK\n");
    ksock_init();
    serial_puts("KSOCK_OK\n");

    serial_puts("KPTY\n");
    kpty_init();
    serial_puts("KPTY_OK\n");

    serial_puts("NET\n");
    /*ixn_net_init();
    ixn_socket_api_init();
    ixn_net_auto_configure();*/
    serial_puts("NET_OK\n");

    serial_puts("LINUX_SYS\n");
    linux_sys_init();
    serial_puts("LINUX_SYS_OK\n");

    serial_puts("INITRD\n");
    fs_root = initialise_initrd(initrd_location);
    serial_puts("INITRD_OK\n");

    monitor_write("List the contents of ramdisk:\n\n");
    serial_puts("LIST\n");
    int i = 0;
    struct dirent *node = 0;
    while ( (node = readdir_fs(fs_root, i)) != 0)
    {
        serial_puts("F:");
        serial_puts(node->name);
        serial_putc('\n');
        monitor_write("Found file ");
        monitor_write(node->name);
        fs_node_t *fsnode = finddir_fs(fs_root, node->name);
        if ((fsnode->flags&0x7) == FS_DIRECTORY)
        {
            monitor_write("\n\t(directory)\n");
        }
        else
        {
            monitor_write("\n\t contents: \"");
            char buf[256];
            u32int sz = read_fs(fsnode, 0, 256, buf);
            int j;
            for (j = 0; j < sz; j++)
                monitor_put(buf[j]);

            monitor_write("\"\n");
        }
        i++;
    }
    serial_puts("LIST_DONE\n");

    serial_puts("TIMER\n");
    init_timer(100u);
    serial_puts("TIMER_OK\n");

    serial_puts("USB\n");
    usb_init();
    serial_puts("USB_OK\n");

    serial_puts("PIC\n");
    outb(0x21, 0xF8);
    outb(0xA1, 0xEF);
    serial_puts("PIC_OK\n");

    serial_puts("KBD\n");
    init_keyboard();
    serial_puts("KBD_OK\n");

    serial_puts("MOUSE\n");
    mouse_install();
    serial_puts("MOUSE_OK\n");

    __asm__ volatile("sti");

    serial_puts("TASK\n");
    initTasking();
    serial_puts("TASK_OK\n");

    serial_puts("AXSHELL\n");
    axshell_main();

    return 0;
}
