// uhci.c — простейший драйвер контроллера UHCI (USB 1.1) для itdo/ArtyomX OS.
//
// Реализует:
//   - обнаружение контроллера через PCI (класс 0x0C, подкласс 0x03, prog-if 0x00);
//   - глобальный/полный сброс, запуск списка кадров;
//   - сброс и включение порта;
//   - синхронные (poll) control-transfer'ы на конечной точке 0.
//
// Осознанные ограничения "простейшего" стека:
//   - только control-transfer'ы (для Get Descriptor / Set Address / Set Configuration).
//     Bulk/Interrupt/Isochronous не реализованы — их легко добавить по образцу
//     uhci_control_transfer(), заведя отдельные QH в списке кадров;
//   - нет прерываний хост-контроллера, всё определяется опросом регистров —
//     то есть USBINTR выключен, IOC в TD не используется;
//   - структуры контроллера (frame list, QH, TD) размещены в статических
//     массивах в .bss и используются как есть, в предположении, что
//     виртуальный адрес совпадает с физическим (тот же стиль, что и
//     framebuffer_addr в gfxlib.c). Если в вашем ядре виртуальные и
//     физические адреса ядра различаются — поправьте функцию v2p();
//   - UHCI умеет адресовать только нижние 4 ГиБ физической памяти:
//     v2p() при переполнении печатает предупреждение в serial.

#include "uhci.h"
#include "../pci.h"

/* ---- Регистры UHCI (I/O-порты, смещение от io_base) ---- */
#define UHCI_REG_USBCMD    0x00
#define UHCI_REG_USBSTS    0x02
#define UHCI_REG_USBINTR   0x04
#define UHCI_REG_FRNUM     0x06
#define UHCI_REG_FRBASEADD 0x08
#define UHCI_REG_SOFMOD    0x0C
#define UHCI_REG_PORTSC1   0x10
#define UHCI_REG_PORTSC2   0x12

#define UHCI_CMD_RS       0x0001
#define UHCI_CMD_HCRESET  0x0002
#define UHCI_CMD_GRESET   0x0004
#define UHCI_CMD_CF       0x0040
#define UHCI_CMD_MAXP     0x0080

#define UHCI_STS_HALTED   0x0020

#define UHCI_PORT_CCS     0x0001
#define UHCI_PORT_CSC     0x0002
#define UHCI_PORT_PE      0x0004
#define UHCI_PORT_PEC     0x0008
#define UHCI_PORT_RESET   0x0200

#define UHCI_PID_IN     0x69
#define UHCI_PID_OUT    0xE1
#define UHCI_PID_SETUP  0x2D

#define UHCI_LINK_TERM  0x00000001u   /* бит T: конец цепочки           */
#define UHCI_LINK_QH    0x00000002u   /* бит Q: указатель ведёт на QH   */
#define UHCI_LINK_VF    0x00000004u   /* бит Vf: обработать в этом кадре*/

#define UHCI_TD_ACTIVE      (1u << 23)
#define UHCI_TD_ERROR_MASK  (0x3Fu << 17)  /* биты 17..22: все статус-ошибки */
#define UHCI_TD_NAK         (1u << 19)
/* "настоящие" ошибки без NAK — NAK для periodic-опроса это норма, а не сбой */
#define UHCI_TD_HARD_ERROR_MASK (UHCI_TD_ERROR_MASK & ~UHCI_TD_NAK)

typedef struct __attribute__((packed, aligned(16))) {
    volatile u32int link;
    volatile u32int cs;
    volatile u32int token;
    volatile u32int buffer;
} uhci_td_t;

typedef struct __attribute__((packed, aligned(16))) {
    volatile u32int head_link;
    volatile u32int element_link;
} uhci_qh_t;

#define UHCI_FRAMELIST_LEN 1024
#define UHCI_MAX_TD        16

static u32int    __attribute__((aligned(4096))) frame_list[UHCI_FRAMELIST_LEN];
static uhci_qh_t __attribute__((aligned(16)))    control_qh;
static uhci_td_t __attribute__((aligned(16)))    td_pool[UHCI_MAX_TD];

static u16int io_base = 0;
static int    controller_ok = 0;

/* ---- отладочный вывод в COM1, чтобы не тянуть зависимость на monitor.h ---- */
static void dbg_putc(char c) { outb(0x3F8, (u8int)c); }
static void dbg_puts(const char *s) { while (*s) dbg_putc(*s++); }
static void dbg_hex32(u32int v)
{
    const char *hexd = "0123456789ABCDEF";
    dbg_puts("0x");
    for (int i = 28; i >= 0; i -= 4) dbg_putc(hexd[(v >> i) & 0xF]);
}

static inline u32int v2p(void *p)
{
    u64int v = (u64int)(uintptr_t)p;
    if (v >> 32) {
        dbg_puts("UHCI: WARNING structure above 4GiB, UHCI cannot address it!\n");
    }
    return (u32int)v;
}

static inline u16int uhci_in16(u16int reg)          { return inw((u16int)(io_base + reg)); }
static inline void   uhci_out16(u16int reg, u16int v){ outw((u16int)(io_base + reg), v); }
static inline void   uhci_out32(u16int reg, u32int v){ outl((u16int)(io_base + reg), v); }

static void short_delay(u32int loops)
{
    while (loops--) __asm__ volatile("pause");
}

static uhci_td_t *alloc_td(void)
{
    for (int i = 0; i < UHCI_MAX_TD; i++) {
        uhci_td_t *td = &td_pool[i];
        if (td->link == 0 && td->cs == 0 && td->token == 0 && td->buffer == 0)
            return td;
    }
    return 0;
}

static void free_td(uhci_td_t *td)
{
    td->link = 0; td->cs = 0; td->token = 0; td->buffer = 0;
}

int uhci_init(void)
{
    u8int bus, slot, func;

    if (!pci_find_class(0x0C, 0x03, 0x00, &bus, &slot, &func)) {
        dbg_puts("UHCI: controller not found on PCI bus\n");
        return 0;
    }

    /* Включаем I/O space (бит0) и bus mastering (бит2) в PCI Command */
    u16int cmd = pci_read16(bus, slot, func, 0x04);
    cmd |= 0x0001 | 0x0004;
    pci_write16(bus, slot, func, 0x04, cmd);

    u32int bar4 = pci_read32(bus, slot, func, 0x20);
    if (!(bar4 & 0x1)) {
        dbg_puts("UHCI: BAR4 is not an I/O BAR\n");
        return 0;
    }
    io_base = (u16int)(bar4 & 0xFFFC);
    dbg_puts("UHCI: io_base="); dbg_hex32(io_base); dbg_puts("\n");

    /* Глобальный сброс шины USB */
    uhci_out16(UHCI_REG_USBCMD, UHCI_CMD_GRESET);
    short_delay(2000000);
    uhci_out16(UHCI_REG_USBCMD, 0);

    /* Полный сброс хост-контроллера */
    uhci_out16(UHCI_REG_USBCMD, UHCI_CMD_HCRESET);
    int tries = 1000;
    while ((uhci_in16(UHCI_REG_USBCMD) & UHCI_CMD_HCRESET) && tries--)
        short_delay(1000);

    uhci_out16(UHCI_REG_USBINTR, 0);   /* прерывания HC не используем, работаем поллингом */
    uhci_out16(UHCI_REG_FRNUM, 0);

    control_qh.head_link    = UHCI_LINK_TERM;
    control_qh.element_link = UHCI_LINK_TERM;

    /* Все 1024 записи списка кадров указывают на одну control QH —
       для простейшего стека этого достаточно, отдельные isochronous/
       interrupt QH не нужны. */
    u32int qh_phys = v2p(&control_qh) | UHCI_LINK_QH;
    for (int i = 0; i < UHCI_FRAMELIST_LEN; i++)
        frame_list[i] = qh_phys;

    uhci_out32(UHCI_REG_FRBASEADD, v2p(frame_list));
    outb((u16int)(io_base + UHCI_REG_SOFMOD), 0x40);

    uhci_out16(UHCI_REG_USBCMD, UHCI_CMD_RS | UHCI_CMD_CF | UHCI_CMD_MAXP);
    short_delay(100000);

    if (uhci_in16(UHCI_REG_USBSTS) & UHCI_STS_HALTED) {
        dbg_puts("UHCI: controller halted right after start\n");
        return 0;
    }

    controller_ok = 1;
    dbg_puts("UHCI: controller started\n");
    return 1;
}

int uhci_port_connected(int port)
{
    u16int reg = (port == 0) ? UHCI_REG_PORTSC1 : UHCI_REG_PORTSC2;
    return (uhci_in16(reg) & UHCI_PORT_CCS) != 0;
}

int uhci_port_reset(int port)
{
    u16int reg = (port == 0) ? UHCI_REG_PORTSC1 : UHCI_REG_PORTSC2;

    u16int v = uhci_in16(reg);
    uhci_out16(reg, v | UHCI_PORT_RESET);
    short_delay(5000000);                 /* ~50мс на реальном железе */
    v = uhci_in16(reg);
    uhci_out16(reg, (u16int)(v & ~UHCI_PORT_RESET));
    short_delay(200000);

    v = uhci_in16(reg);
    if (!(v & UHCI_PORT_CCS)) return 0;   /* устройство отвалилось во время сброса */

    uhci_out16(reg, v | UHCI_PORT_PE);
    short_delay(200000);

    v = uhci_in16(reg);
    uhci_out16(reg, v | UHCI_PORT_CSC | UHCI_PORT_PEC); /* сброс битов изменения (W1C) */

    return 1;
}

static void build_td(uhci_td_t *td, u32int next_phys, int is_last,
                      u8int pid, u8int dev_addr, u8int endpoint, int toggle,
                      void *buffer, u16int length)
{
    td->link = is_last ? UHCI_LINK_TERM : (next_phys | UHCI_LINK_VF);

    u32int cs = UHCI_TD_ACTIVE;
    cs |= (3u << 27);   /* C_ERR = 3 попытки прежде чем считать ошибкой */
    td->cs = cs;

    u16int maxlen_field = (length == 0) ? 0x7FF : (u16int)(length - 1);
    u32int token = pid;
    token |= ((u32int)dev_addr  & 0x7Fu) << 8;
    token |= ((u32int)endpoint  & 0xFu)  << 15;
    token |= ((u32int)(toggle & 1))      << 19;
    token |= ((u32int)maxlen_field)      << 21;
    td->token = token;

    td->buffer = length ? v2p(buffer) : 0;
}

int uhci_control_transfer(u8int dev_addr, usb_setup_packet_t *setup,
                           void *buffer, u16int length, int in_dir)
{
    if (!controller_ok) return -1;

    uhci_td_t *setup_td  = alloc_td();
    uhci_td_t *data_td    = length ? alloc_td() : 0;
    uhci_td_t *status_td  = alloc_td();
    if (!setup_td || !status_td || (length && !data_td)) {
        dbg_puts("UHCI: out of TDs\n");
        if (setup_td) free_td(setup_td);
        if (data_td)  free_td(data_td);
        if (status_td) free_td(status_td);
        return -1;
    }

    u8int data_pid    = in_dir ? UHCI_PID_IN  : UHCI_PID_OUT;
    u8int status_pid  = in_dir ? UHCI_PID_OUT : UHCI_PID_IN;

    if (data_td) {
        build_td(setup_td,  v2p(data_td),   0, UHCI_PID_SETUP, dev_addr, 0, 0, setup, 8);
        build_td(data_td,   v2p(status_td), 0, data_pid,       dev_addr, 0, 1, buffer, length);
        build_td(status_td, 0,              1, status_pid,     dev_addr, 0, 1, 0, 0);
    } else {
        build_td(setup_td,  v2p(status_td), 0, UHCI_PID_SETUP, dev_addr, 0, 0, setup, 8);
        build_td(status_td, 0,              1, status_pid,     dev_addr, 0, 1, 0, 0);
    }

    control_qh.element_link = v2p(setup_td);

    int timeout = 200000;
    while (timeout--) {
        if (!(status_td->cs & UHCI_TD_ACTIVE)) break;
        short_delay(50);
    }

    int had_error = 0;
    if (setup_td->cs & UHCI_TD_ERROR_MASK) had_error = 1;
    if (data_td && (data_td->cs & UHCI_TD_ERROR_MASK)) had_error = 1;
    if (status_td->cs & UHCI_TD_ERROR_MASK) had_error = 1;

    int ok = (timeout > 0) && !had_error;
    if (!ok) {
        dbg_puts("UHCI: control transfer failed (timeout=");
        dbg_hex32((u32int)timeout);
        dbg_puts(" setup.cs="); dbg_hex32(setup_td->cs);
        if (data_td) { dbg_puts(" data.cs="); dbg_hex32(data_td->cs); }
        dbg_puts(" status.cs="); dbg_hex32(status_td->cs);
        dbg_puts(")\n");
    }

    control_qh.element_link = UHCI_LINK_TERM;
    free_td(setup_td);
    if (data_td) free_td(data_td);
    free_td(status_td);

    return ok ? 0 : -1;
}

int uhci_transfer(u8int dev_addr, u8int endpoint, int in_dir,
                   void *buffer, u16int length, int *toggle)
{
    if (!controller_ok) return -1;

    uhci_td_t *td = alloc_td();
    if (!td) return -1;

    u8int pid = in_dir ? UHCI_PID_IN : UHCI_PID_OUT;
    build_td(td, 0, 1, pid, dev_addr, endpoint, *toggle, buffer, length);

    control_qh.element_link = v2p(td);

    /* Короткий таймаут: для interrupt endpoint NAK (нет новых данных) —
       обычное дело, ждать полный control-timeout тут не нужно и вредно
       для отзывчивости композитора. */
    int timeout = 3000;
    while (timeout--) {
        if (!(td->cs & UHCI_TD_ACTIVE)) break;
        short_delay(20);
    }

    int result;
    if (timeout <= 0) {
        result = 1; /* HC не успел отработать / устройство держит NAK */
    } else if (td->cs & UHCI_TD_HARD_ERROR_MASK) {
        result = -1;
    } else if (td->cs & UHCI_TD_NAK) {
        result = 1;
    } else {
        *toggle ^= 1;
        result = 0;
    }

    control_qh.element_link = UHCI_LINK_TERM;
    free_td(td);
    return result;
}
