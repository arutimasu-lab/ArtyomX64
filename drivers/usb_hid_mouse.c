// usb_hid_mouse.c — простейший драйвер USB HID мыши в Boot Protocol.
//
// Использует только "boot protocol" (класс 3 / подкласс 1 / протокол 2)
// потому что у него фиксированный, документированный формат отчёта
// (байт0=кнопки, байт1=dX, байт2=dY, обе координаты — знаковые), в отличие
// от произвольных HID Report Descriptor'ов, парсинг которых — отдельная
// большая задача. Почти все мыши поддерживают boot protocol (это как раз
// то, что использует BIOS/UEFI до загрузки ОС).
//
// Результат опроса кладётся в те же глобальные переменные, что и PS/2
// драйвер (drivers/mouse.c): mouse_dx, mouse_dy, mouse_buttons — поэтому
// axshell.c менять не нужно, он их уже читает через handle_mouse().

#include <stdint.h>
#include "usb_hid_mouse.h"
#include "usb/usb.h"

extern volatile int mouse_dx;
extern volatile int mouse_dy;
extern volatile int mouse_buttons;

#define HID_REQ_SET_PROTOCOL 0x0B
#define HID_REQ_SET_IDLE     0x0A
#define HID_PROTOCOL_BOOT    0

#define USB_DESC_INTERFACE   0x04
#define USB_DESC_ENDPOINT    0x05

static void dbg_puts(const char *s) { while (*s) outb(0x3F8, (u8int)*s++); }

static int    mouse_attached = 0;
static u8int  mouse_addr = 0;
static u8int  mouse_ep = 0;
static u16int mouse_ep_max_packet = 8;
static int    mouse_toggle = 0;

static u8int cfgbuf[256];

static int fetch_config_descriptor(u8int addr, u16int *out_len)
{
    usb_setup_packet_t setup;

    /* Сначала 9 байт заголовка, чтобы узнать реальный wTotalLength */
    setup.bmRequestType = USB_DIR_IN;
    setup.bRequest      = USB_REQ_GET_DESCRIPTOR;
    setup.wValue        = (u16int)(USB_DESC_CONFIGURATION << 8);
    setup.wIndex        = 0;
    setup.wLength       = 9;
    if (usb_control_transfer(addr, &setup, cfgbuf, 9, 1) != 0) return 0;

    u16int total_len = (u16int)(cfgbuf[2] | (cfgbuf[3] << 8));
    if (total_len < 9) return 0;
    if (total_len > sizeof(cfgbuf)) total_len = sizeof(cfgbuf);

    setup.wLength = total_len;
    if (usb_control_transfer(addr, &setup, cfgbuf, total_len, 1) != 0) return 0;

    *out_len = total_len;
    return 1;
}

int usb_hid_mouse_try_attach(u8int addr)
{
    if (mouse_attached) return 0; /* этот простой драйвер держит только одну мышь */

    u16int total_len;
    if (!fetch_config_descriptor(addr, &total_len)) return 0;

    u8int bConfigurationValue = cfgbuf[5];

    int    in_target_iface = 0;
    u8int  target_iface = 0;
    u8int  found_ep = 0;
    u16int found_ep_max = 0;

    u16int off = 9;
    while ((u32int)off + 2 <= total_len) {
        u8int blen  = cfgbuf[off];
        u8int btype = cfgbuf[off + 1];
        if (blen < 2) break;

        if (btype == USB_DESC_INTERFACE && (u32int)off + 9 <= total_len) {
            u8int iface_num   = cfgbuf[off + 2];
            u8int iface_class = cfgbuf[off + 5];
            u8int iface_sub   = cfgbuf[off + 6];
            u8int iface_proto = cfgbuf[off + 7];

            /* Класс 3 = HID, подкласс 1 = Boot Interface, протокол 2 = Mouse */
            if (iface_class == 0x03 && iface_sub == 0x01 && iface_proto == 0x02) {
                in_target_iface = 1;
                target_iface = iface_num;
            } else {
                in_target_iface = 0;
            }
        } else if (btype == USB_DESC_ENDPOINT && in_target_iface &&
                   !found_ep && (u32int)off + 7 <= total_len) {
            u8int  ep_addr = cfgbuf[off + 2];
            u8int  ep_attr = cfgbuf[off + 3];
            u16int ep_max  = (u16int)(cfgbuf[off + 4] | (cfgbuf[off + 5] << 8));

            /* IN (бит7=1) + Interrupt (тип=3) */
            if ((ep_addr & 0x80) && (ep_attr & 0x03) == 0x03) {
                found_ep = (u8int)(ep_addr & 0x0F);
                found_ep_max = ep_max ? ep_max : 8;
            }
        }

        off = (u16int)(off + blen);
    }

    if (!found_ep) return 0; /* не HID boot mouse (либо составное устройство без такого интерфейса) */

    usb_setup_packet_t setup;

    setup.bmRequestType = USB_DIR_OUT;
    setup.bRequest      = USB_REQ_SET_CONFIGURATION;
    setup.wValue        = bConfigurationValue;
    setup.wIndex        = 0;
    setup.wLength       = 0;
    if (usb_control_transfer(addr, &setup, 0, 0, 0) != 0) {
        dbg_puts("USB-HID: SET_CONFIGURATION failed\n");
        return 0;
    }

    /* Класс-специфичные запросы (bmRequestType=0x21: host->device, class, interface) */
    setup.bmRequestType = 0x21;
    setup.bRequest      = HID_REQ_SET_PROTOCOL;
    setup.wValue        = HID_PROTOCOL_BOOT;
    setup.wIndex        = target_iface;
    setup.wLength       = 0;
    usb_control_transfer(addr, &setup, 0, 0, 0); /* необязательный шаг, ошибку игнорируем */

    setup.bRequest = HID_REQ_SET_IDLE;
    setup.wValue   = 0; /* отчёты только по изменению состояния */
    usb_control_transfer(addr, &setup, 0, 0, 0);

    mouse_addr = addr;
    mouse_ep = found_ep;
    mouse_ep_max_packet = found_ep_max;
    mouse_toggle = 0;
    mouse_attached = 1;

    return 1;
}

void usb_hid_mouse_poll(void)
{
    if (!mouse_attached) return;

    u8int report[8];
    for (int i = 0; i < 8; i++) report[i] = 0;

    u16int len = mouse_ep_max_packet > 8 ? 8 : mouse_ep_max_packet;
    if (len < 3) len = 3; /* минимум для boot-протокола: кнопки+dX+dY */

    int r = usb_transfer(mouse_addr, mouse_ep, 1, report, len, &mouse_toggle, mouse_ep_max_packet);
    if (r != 0) return; /* NAK (нет новых данных) или ошибка — пропускаем этот кадр */

    /* Boot Protocol report: байт0=кнопки(биты0-2), байт1=dX, байт2=dY (знаковые) */
    mouse_buttons = report[0] & 0x07;
    mouse_dx += (int)(int8_t)report[1];
    mouse_dy -= (int)(int8_t)report[2];
}
