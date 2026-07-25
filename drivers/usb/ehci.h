#ifndef EHCI_H
#define EHCI_H

#include "../../lib/common.h"
#include "usb.h"

/* Найти и запустить первый EHCI-контроллер на шине PCI.
 * Возвращает 1 при успехе, 0 если контроллер не найден/не запустился. */
int ehci_init(void);

/* Общий (не-control) transfer на произвольную конечную точку — для
 * periodic-опроса interrupt endpoint'ов (HID-мышь и т.п.).
 * ep_max_packet — заявленный wMaxPacketSize конечной точки из её
 * дескриптора (используется контроллером для планирования).
 * toggle — как и в ehci_control_transfer, хранится вызывающим между
 * вызовами и обновляется функцией при успехе.
 * Возвращает: 0 = успех, 1 = NAK/нет данных (норма для interrupt EP),
 * -1 = ошибка. */
int ehci_transfer(u8int dev_addr, u8int endpoint, int in_dir,
                   void *buffer, u16int length, int *toggle,
                   u16int ep_max_packet);

/* Выполнить control-transfer на конечной точке 0 High-Speed устройства.
 * in_dir: 1 = устройство -> хост, 0 = хост -> устройство.
 * Возвращает 0 при успехе, -1 при ошибке/таймауте. */
int ehci_control_transfer(u8int dev_addr, usb_setup_packet_t *setup,
                           void *buffer, u16int length, int in_dir);

/* Подключено ли что-то к порту (0..n_ports-1)? */
int ehci_port_connected(int port);

/* Сколько корневых портов у контроллера */
int ehci_port_count(void);

/* Сбросить порт. Возвращает 1, если после сброса устройство осталось
 * на линии И оказалось High-Speed (порт остаётся под управлением EHCI).
 * Возвращает 0, если устройства нет, либо оно Full/Low-Speed — в этом
 * случае порт передаётся компаньон-контроллеру (Port Owner=1), которого
 * этот стек не реализует, так что устройство останется недоступным. */
int ehci_port_reset(int port);

#endif
