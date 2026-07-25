// ehci.c — простейший драйвер контроллера EHCI (USB 2.0 High-Speed)
// для itdo/ArtyomX OS.
//
// Реализует:
//   - обнаружение контроллера через PCI (класс 0x0C, подкласс 0x03, prog-if 0x20);
//   - legacy BIOS->OS handoff через Extended Capability в PCI-конфиге;
//   - разбор Capability-регистров, инициализацию Operational-регистров;
//   - одну статическую asynchronous QH (control/bulk) и пул qTD;
//   - сброс порта с определением High-Speed / не-High-Speed устройства;
//   - синхронные (poll) control-transfer'ы.
//
// Осознанные ограничения:
//   - только control-transfer'ы, только конечная точка 0;
//   - только High-Speed устройства. Full/Low-Speed устройства EHCI сам
//     не обслуживает: после сброса порта, если PE (Port Enable) не
//     установился аппаратно, устройство full/low-speed, и порт отдаётся
//     компаньон-контроллеру (Port Owner=1) — читайте: устройство работать
//     не будет, если вы не реализуете ещё и UHCI/OHCI-компаньон;
//   - periodic list (interrupt/isochronous endpoints) не используется —
//     для клавиатуры/мыши по HID это тоже потребуется добавить отдельно;
//   - все структуры данных (async QH, qTD) держим в нижних 4 ГиБ и работаем
//     как с identity-mapped памятью (тот же стиль, что framebuffer_addr в
//     gfxlib.c) — CTRLDSSEGMENT всегда 0, 64-битная адресация не используется;
//   - BAR0 контроллера предполагается 32-битным MMIO. 64-битный BAR0
//     (редкость для EHCI) не поддержан — драйвер откажется работать и
//     выведет предупреждение.

#include "ehci.h"
#include "../pci.h"

/* Уже настроено в limine_boot.c: виртуальный HHDM-адрес = физический + это
 * смещение. Limine гарантирует, что HHDM покрывает как минимум первые
 * 4 ГиБ физического адресного пространства — этого достаточно, чтобы
 * достать до PCI BAR MMIO-регионов (обычно где-то в районе 0xFEB00000)
 * без написания собственного менеджера виртуальной памяти. */
extern u64int limine_hhdm_offset;

/* ---- смещения Capability-регистров от cap_base ---- */
#define EHCI_CAP_CAPLENGTH   0x00
#define EHCI_CAP_HCIVERSION  0x02
#define EHCI_CAP_HCSPARAMS   0x04
#define EHCI_CAP_HCCPARAMS   0x08

/* ---- смещения Operational-регистров от op_base ---- */
#define EHCI_OP_USBCMD        0x00
#define EHCI_OP_USBSTS        0x04
#define EHCI_OP_USBINTR       0x08
#define EHCI_OP_FRINDEX       0x0C
#define EHCI_OP_CTRLDSSEGMENT 0x10
#define EHCI_OP_PERIODICBASE  0x14
#define EHCI_OP_ASYNCLISTADDR 0x18
#define EHCI_OP_CONFIGFLAG    0x40
#define EHCI_OP_PORTSC(n)     (0x44 + (n) * 4)

#define EHCI_CMD_RS          0x00000001u
#define EHCI_CMD_HCRESET     0x00000002u
#define EHCI_CMD_ASYNC_EN    0x00000020u
#define EHCI_CMD_ITC_8       0x00080000u  /* interrupt threshold: 8 микрокадров */

#define EHCI_STS_HALTED      0x00001000u

#define EHCI_PORT_CCS        0x00000001u
#define EHCI_PORT_CSC        0x00000002u
#define EHCI_PORT_PE         0x00000004u
#define EHCI_PORT_PEC        0x00000008u
#define EHCI_PORT_RESET      0x00000100u
#define EHCI_PORT_OWNER      0x00002000u

#define EHCI_QTD_ACTIVE      0x00000080u
#define EHCI_QTD_ERROR_MASK  0x0000007Cu  /* halted|buf-err|babble|xact-err: биты 3..6 */

#define EHCI_PID_OUT   0x00
#define EHCI_PID_IN    0x01
#define EHCI_PID_SETUP 0x02

typedef struct __attribute__((packed, aligned(32))) {
    volatile u32int next_qtd;
    volatile u32int alt_next_qtd;
    volatile u32int token;
    volatile u32int buffer[5];
} ehci_qtd_t;

typedef struct __attribute__((packed, aligned(32))) {
    volatile u32int horizontal_link;
    volatile u32int ep_char;
    volatile u32int ep_cap;
    volatile u32int current_qtd;
    /* область-оверлей: HC копирует сюда активный qTD во время выполнения */
    volatile u32int next_qtd;
    volatile u32int alt_next_qtd;
    volatile u32int token;
    volatile u32int buffer[5];
} ehci_qh_t;

#define EHCI_MAX_QTD 8

static ehci_qh_t  __attribute__((aligned(32))) async_qh;
static ehci_qtd_t __attribute__((aligned(32))) qtd_pool[EHCI_MAX_QTD];

static volatile u8int *cap_base = 0;
static volatile u8int *op_base  = 0;
static int   n_ports = 0;
static int   controller_ok = 0;
static u8int pci_bus, pci_slot, pci_func;

/* ---- отладочный вывод в COM1 ---- */
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
    if (v >> 32) dbg_puts("EHCI: WARNING structure above 4GiB!\n");
    return (u32int)v;
}

static void short_delay(u32int loops) { while (loops--) __asm__ volatile("pause"); }

static inline u32int cap_read32(u32int off) { return *(volatile u32int *)(cap_base + off); }
static inline u16int cap_read16(u32int off) { return *(volatile u16int *)(cap_base + off); }
static inline u8int  cap_read8 (u32int off) { return *(cap_base + off); }

static inline u32int op_read32(u32int off)  { return *(volatile u32int *)(op_base + off); }
static inline void   op_write32(u32int off, u32int val) { *(volatile u32int *)(op_base + off) = val; }

/* ---- Legacy BIOS -> OS handoff (Extended Capability в PCI-конфиге) ---- */
static void ehci_bios_handoff(void)
{
    u32int hccparams = cap_read32(EHCI_CAP_HCCPARAMS);
    u8int eecp = (u8int)((hccparams >> 8) & 0xFF);
    if (eecp < 0x40) return; /* нет расширенных возможностей */

    u32int legsup = pci_read32(pci_bus, pci_slot, pci_func, eecp);
    if ((legsup & 0xFF) != 0x01) return; /* не USB Legacy Support Capability */

    if (!(legsup & (1u << 16))) {
        /* BIOS ничем не владеет — просто помечаем себя владельцем */
        pci_write32(pci_bus, pci_slot, pci_func, eecp, legsup | (1u << 24));
    } else {
        pci_write32(pci_bus, pci_slot, pci_func, eecp, legsup | (1u << 24));
        int tries = 100000;
        while ((pci_read32(pci_bus, pci_slot, pci_func, eecp) & (1u << 16)) && tries--)
            short_delay(1000);
        if (!tries) dbg_puts("EHCI: BIOS handoff timed out, continuing anyway\n");
    }

    /* Отключаем SMI на USBLEGCTLSTS (eecp+4), чтобы BIOS не мешал дальше */
    pci_write32(pci_bus, pci_slot, pci_func, (u8int)(eecp + 4), 0);
}

static ehci_qtd_t *alloc_qtd(void)
{
    for (int i = 0; i < EHCI_MAX_QTD; i++) {
        ehci_qtd_t *q = &qtd_pool[i];
        if (q->next_qtd == 0 && q->token == 0) return q;
    }
    return 0;
}

static void free_qtd(ehci_qtd_t *q)
{
    q->next_qtd = 0; q->alt_next_qtd = 0; q->token = 0;
    for (int i = 0; i < 5; i++) q->buffer[i] = 0;
}

int ehci_init(void)
{
    if (!pci_find_class(0x0C, 0x03, 0x20, &pci_bus, &pci_slot, &pci_func)) {
        dbg_puts("EHCI: controller not found on PCI bus\n");
        return 0;
    }

    /* Включаем memory space (бит1) и bus mastering (бит2) в PCI Command */
    u16int cmd = pci_read16(pci_bus, pci_slot, pci_func, 0x04);
    cmd |= 0x0002 | 0x0004;
    pci_write16(pci_bus, pci_slot, pci_func, 0x04, cmd);

    u32int bar0 = pci_read32(pci_bus, pci_slot, pci_func, 0x10);
    if (bar0 & 0x1) { dbg_puts("EHCI: BAR0 is I/O space, expected MMIO\n"); return 0; }
    if (((bar0 >> 1) & 0x3) == 0x2) {
        dbg_puts("EHCI: 64-bit BAR0 not supported by this driver\n");
        return 0;
    }

    u64int bar0_phys = (u64int)(bar0 & 0xFFFFFFF0u);
    cap_base = (volatile u8int *)(uintptr_t)(bar0_phys + limine_hhdm_offset);

    ehci_bios_handoff();

    u8int caplen = cap_read8(EHCI_CAP_CAPLENGTH);
    op_base = cap_base + caplen;

    u32int hcsparams = cap_read32(EHCI_CAP_HCSPARAMS);
    n_ports = (int)(hcsparams & 0xF);
    if (n_ports <= 0) n_ports = 1;

    dbg_puts("EHCI: cap_base="); dbg_hex32((u32int)(uintptr_t)cap_base);
    dbg_puts(" n_ports="); dbg_hex32((u32int)n_ports); dbg_puts("\n");

    /* Останавливаем и сбрасываем контроллер перед настройкой */
    op_write32(EHCI_OP_USBCMD, op_read32(EHCI_OP_USBCMD) & ~EHCI_CMD_RS);
    int tries = 100000;
    while (!(op_read32(EHCI_OP_USBSTS) & EHCI_STS_HALTED) && tries--) short_delay(100);

    op_write32(EHCI_OP_USBCMD, EHCI_CMD_HCRESET);
    tries = 100000;
    while ((op_read32(EHCI_OP_USBCMD) & EHCI_CMD_HCRESET) && tries--) short_delay(100);

    op_write32(EHCI_OP_USBINTR, 0);           /* прерывания HC не используем */
    op_write32(EHCI_OP_CTRLDSSEGMENT, 0);     /* все структуры ниже 4 ГиБ   */
    op_write32(EHCI_OP_PERIODICBASE, 0);      /* periodic list не используем */

    /* Инициализация единственной async QH — головы reclamation list,
       замкнутой самой на себя (пока в ней нет активных qTD). */
    async_qh.horizontal_link = v2p(&async_qh) | 0x2 /* Typ=QH */;
    async_qh.ep_char = (1u << 15); /* H = 1 (голова reclamation list), остальное заполнится перед каждым transfer'ом */
    async_qh.ep_cap  = (1u << 30); /* Mult = 1 */
    async_qh.current_qtd = 0;
    async_qh.next_qtd = 1;      /* terminate: пока нечего выполнять */
    async_qh.alt_next_qtd = 1;
    async_qh.token = 0;
    for (int i = 0; i < 5; i++) async_qh.buffer[i] = 0;
    for (int i = 0; i < EHCI_MAX_QTD; i++) free_qtd(&qtd_pool[i]);

    op_write32(EHCI_OP_ASYNCLISTADDR, v2p(&async_qh));

    op_write32(EHCI_OP_USBCMD, EHCI_CMD_RS | EHCI_CMD_ASYNC_EN | EHCI_CMD_ITC_8);
    short_delay(100000);

    if (op_read32(EHCI_OP_USBSTS) & EHCI_STS_HALTED) {
        dbg_puts("EHCI: controller halted right after start\n");
        return 0;
    }

    /* Routing всех портов на EHCI (а не на компаньон-контроллеры) */
    op_write32(EHCI_OP_CONFIGFLAG, 1);
    short_delay(50000);

    controller_ok = 1;
    dbg_puts("EHCI: controller started\n");
    return 1;
}

int ehci_port_count(void) { return n_ports; }

int ehci_port_connected(int port)
{
    return (op_read32(EHCI_OP_PORTSC(port)) & EHCI_PORT_CCS) != 0;
}

int ehci_port_reset(int port)
{
    u32int off = EHCI_OP_PORTSC(port);
    u32int v = op_read32(off);

    /* сбрасываем сохранённые change-биты, включаем Reset, отключаем Enable */
    v &= ~(EHCI_PORT_PE | EHCI_PORT_CSC | EHCI_PORT_PEC);
    op_write32(off, v | EHCI_PORT_RESET);
    short_delay(5000000); /* >= 50мс на реальном железе */

    v = op_read32(off);
    op_write32(off, v & ~EHCI_PORT_RESET);
    short_delay(200000);

    v = op_read32(off);
    if (!(v & EHCI_PORT_CCS)) return 0; /* устройство отвалилось */

    if (v & EHCI_PORT_PE) {
        /* HC сам включил порт => устройство High-Speed, остаётся на EHCI */
        op_write32(off, v | EHCI_PORT_CSC | EHCI_PORT_PEC);
        return 1;
    }

    /* Не High-Speed: отдаём компаньон-контроллеру. Этот стек его не
       реализует, так что устройство просто не заработает. */
    dbg_puts("EHCI: non-high-speed device on port, handing off to companion (unsupported)\n");
    op_write32(off, v | EHCI_PORT_OWNER);
    return 0;
}

static void build_qtd(ehci_qtd_t *qtd, u32int next_phys, int is_last,
                       u8int pid, int toggle, void *buffer, u16int length)
{
    qtd->next_qtd = is_last ? 1u : next_phys;
    qtd->alt_next_qtd = 1u; /* terminate, alt-путь не используется */

    u32int token = EHCI_QTD_ACTIVE;
    token |= ((u32int)pid) << 8;
    token |= (3u << 10);                 /* CERR = 3 попытки */
    token |= ((u32int)length) << 16;
    token |= ((u32int)(toggle & 1)) << 31;
    qtd->token = token;

    for (int i = 0; i < 5; i++) qtd->buffer[i] = 0;
    if (length && buffer) {
        /* Буфер небольшой (дескрипторы <= 64 байт), пересечение 4К-страницы
           не обрабатываем — этого достаточно для GET_DESCRIPTOR/SET_ADDRESS. */
        qtd->buffer[0] = v2p(buffer);
    }
}

int ehci_control_transfer(u8int dev_addr, usb_setup_packet_t *setup,
                           void *buffer, u16int length, int in_dir)
{
    if (!controller_ok) return -1;

    ehci_qtd_t *setup_qtd  = alloc_qtd();
    ehci_qtd_t *data_qtd   = length ? alloc_qtd() : 0;
    ehci_qtd_t *status_qtd = alloc_qtd();
    if (!setup_qtd || !status_qtd || (length && !data_qtd)) {
        dbg_puts("EHCI: out of qTDs\n");
        if (setup_qtd) free_qtd(setup_qtd);
        if (data_qtd) free_qtd(data_qtd);
        if (status_qtd) free_qtd(status_qtd);
        return -1;
    }

    u8int data_pid   = in_dir ? EHCI_PID_IN  : EHCI_PID_OUT;
    u8int status_pid = in_dir ? EHCI_PID_OUT : EHCI_PID_IN;

    if (data_qtd) {
        build_qtd(setup_qtd,  v2p(data_qtd),   0, EHCI_PID_SETUP, 0, setup, 8);
        build_qtd(data_qtd,   v2p(status_qtd), 0, data_pid,       1, buffer, length);
        build_qtd(status_qtd, 0,               1, status_pid,     1, 0, 0);
    } else {
        build_qtd(setup_qtd,  v2p(status_qtd), 0, EHCI_PID_SETUP, 0, setup, 8);
        build_qtd(status_qtd, 0,               1, status_pid,     1, 0, 0);
    }

    /* Настраиваем единственную async QH под текущее устройство/EP0.
       Конкурентных transfer'ов нет (всё синхронно), так что переиспользовать
       одну QH безопасно. */
    u32int ep_char = ((u32int)dev_addr & 0x7Fu);
    ep_char |= (0u << 8);      /* Endpoint Number = 0 */
    ep_char |= (2u << 12);     /* Endpoint Speed = High Speed */
    ep_char |= (1u << 14);     /* DTC = 1: toggle берём из qTD */
    ep_char |= (1u << 15);     /* H = 1 */
    ep_char |= (64u << 16);    /* MaxPacketLen = 64 (обязателен для HS EP0) */
    async_qh.ep_char = ep_char;
    async_qh.ep_cap  = (1u << 30);

    /* "Прайминг" QH новым transfer'ом: пишем overlay так, будто QH только
       что стала неактивна — HC подхватит next_qtd на следующем проходе
       async schedule. */
    async_qh.token = 0;
    async_qh.next_qtd = v2p(setup_qtd);
    async_qh.alt_next_qtd = 1;

    int timeout = 400000;
    while (timeout--) {
        if (!(status_qtd->token & EHCI_QTD_ACTIVE)) break;
        short_delay(50);
    }

    int had_error = 0;
    if (setup_qtd->token & EHCI_QTD_ERROR_MASK) had_error = 1;
    if (data_qtd && (data_qtd->token & EHCI_QTD_ERROR_MASK)) had_error = 1;
    if (status_qtd->token & EHCI_QTD_ERROR_MASK) had_error = 1;

    int ok = (timeout > 0) && !had_error;
    if (!ok) {
        dbg_puts("EHCI: control transfer failed setup.tok=");
        dbg_hex32(setup_qtd->token);
        if (data_qtd) { dbg_puts(" data.tok="); dbg_hex32(data_qtd->token); }
        dbg_puts(" status.tok="); dbg_hex32(status_qtd->token);
        dbg_puts("\n");
    }

    /* останавливаем QH перед очисткой пула, чтобы HC не трогал освобождённые qTD */
    async_qh.next_qtd = 1;
    async_qh.token = 0;

    free_qtd(setup_qtd);
    if (data_qtd) free_qtd(data_qtd);
    free_qtd(status_qtd);

    return ok ? 0 : -1;
}

int ehci_transfer(u8int dev_addr, u8int endpoint, int in_dir,
                   void *buffer, u16int length, int *toggle,
                   u16int ep_max_packet)
{
    if (!controller_ok) return -1;

    ehci_qtd_t *qtd = alloc_qtd();
    if (!qtd) return -1;

    u8int pid = in_dir ? EHCI_PID_IN : EHCI_PID_OUT;
    build_qtd(qtd, 0, 1, pid, *toggle, buffer, length);

    u32int ep_char = ((u32int)dev_addr & 0x7Fu);
    ep_char |= ((u32int)endpoint & 0xFu) << 8;
    ep_char |= (2u << 12);                         /* High Speed */
    ep_char |= (1u << 14);                         /* DTC = 1 */
    ep_char |= (1u << 15);                         /* H = 1 */
    ep_char |= ((u32int)(ep_max_packet ? ep_max_packet : 8u)) << 16;
    async_qh.ep_char = ep_char;
    async_qh.ep_cap  = (1u << 30);

    async_qh.token = 0;
    async_qh.next_qtd = v2p(qtd);
    async_qh.alt_next_qtd = 1;

    /* Короткий таймаут: NAK на interrupt endpoint — обычное дело. */
    int timeout = 6000;
    while (timeout--) {
        if (!(qtd->token & EHCI_QTD_ACTIVE)) break;
        short_delay(20);
    }

    int result;
    if (timeout <= 0) {
        result = 1;
    } else if (qtd->token & EHCI_QTD_ERROR_MASK) {
        result = -1;
    } else {
        *toggle ^= 1;
        result = 0;
    }

    async_qh.next_qtd = 1;
    async_qh.token = 0;
    free_qtd(qtd);
    return result;
}
