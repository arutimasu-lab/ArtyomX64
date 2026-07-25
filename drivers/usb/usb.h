#ifndef USB_H
#define USB_H

#include "../../lib/common.h"

#pragma pack(push, 1)

typedef struct {
    u8int  bmRequestType;
    u8int  bRequest;
    u16int wValue;
    u16int wIndex;
    u16int wLength;
} usb_setup_packet_t;

typedef struct {
    u8int  bLength;
    u8int  bDescriptorType;
    u16int bcdUSB;
    u8int  bDeviceClass;
    u8int  bDeviceSubClass;
    u8int  bDeviceProtocol;
    u8int  bMaxPacketSize0;
    u16int idVendor;
    u16int idProduct;
    u16int bcdDevice;
    u8int  iManufacturer;
    u8int  iProduct;
    u8int  iSerialNumber;
    u8int  bNumConfigurations;
} usb_device_descriptor_t;

#pragma pack(pop)

#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_SET_CONFIGURATION 0x09

#define USB_DESC_DEVICE        0x01
#define USB_DESC_CONFIGURATION 0x02

#define USB_DIR_IN  0x80
#define USB_DIR_OUT 0x00

#define USB_MAX_DEVICES 16

typedef struct {
    int    present;
    int    address;
    u8int  port;
    usb_device_descriptor_t desc;
} usb_device_t;

/* Инициализирует контроллер (UHCI, при отсутствии — EHCI) и перечисляет
 * устройства на его портах. Ничего не делает, если контроллер не найден. */
void usb_init(void);

/* Общие (не зависящие от того, UHCI под капотом или EHCI) обёртки —
 * ими пользуются драйверы классов устройств (HID и т.п.). */
int usb_control_transfer(u8int dev_addr, usb_setup_packet_t *setup,
                          void *buffer, u16int length, int in_dir);
int usb_transfer(u8int dev_addr, u8int endpoint, int in_dir,
                  void *buffer, u16int length, int *toggle,
                  u16int ep_max_packet);

extern usb_device_t usb_devices[USB_MAX_DEVICES];

#endif
