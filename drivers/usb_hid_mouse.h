#ifndef USB_HID_MOUSE_H
#define USB_HID_MOUSE_H

#include "../lib/common.h"

/* Проверяет устройство addr на наличие HID boot-protocol интерфейса мыши
 * среди его конфигурации; если находит — делает SET_CONFIGURATION,
 * SET_PROTOCOL(Boot) и SET_IDLE(0), запоминает endpoint и адрес как
 * активную мышь. Поддерживается одна мышь одновременно (для двух и
 * более — расширьте mouse_addr/mouse_ep в массив).
 * Возвращает 1, если устройство подключено как мышь, иначе 0. */
int usb_hid_mouse_try_attach(u8int addr);

/* Опрашивает подключённую (если есть) USB HID мышь по её interrupt IN
 * endpoint'у и добавляет смещение/кнопки в те же глобальные переменные,
 * которые использует PS/2-драйвер мыши: mouse_dx, mouse_dy, mouse_buttons
 * (см. drivers/mouse.c). Если мыши нет — ничего не делает. Дешёвая функция,
 * рассчитана на вызов каждый кадр композитора (см. handle_mouse()). */
void usb_hid_mouse_poll(void);

#endif
