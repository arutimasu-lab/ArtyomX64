#ifndef UHCI_H
#define UHCI_H

#include "../../lib/common.h"
#include "usb.h"

/* Найти и запустить первый UHCI-контроллер на шине PCI.
 * Возвращает 1 при успехе, 0 если контроллер не найден/не запустился. */
int uhci_init(void);

/* Общий (не-control) transfer на произвольную конечную точку — для
 * periodic-опроса interrupt endpoint'ов (например HID-мышь), годится и
 * для bulk. Строит один пакет данных без SETUP/STATUS стадий.
 * toggle — указатель на бит переключения данных для этой конечной точки;
 * вызывающий обязан хранить его между вызовами, функция сама его обновит
 * при успешной передаче.
 * Возвращает: 0 = успех (buffer заполнен/отправлен), 1 = устройство
 * ответило NAK или не успело ответить (это нормально для interrupt
 * endpoint — просто нет новых данных), -1 = настоящая ошибка. */
int uhci_transfer(u8int dev_addr, u8int endpoint, int in_dir,
                   void *buffer, u16int length, int *toggle);

/* Выполнить control-transfer на конечной точке 0.
 * in_dir: 1 = устройство -> хост (IN-стадия данных), 0 = хост -> устройство.
 * buffer/length можно передать 0/0 для запросов без стадии данных
 * (например SET_ADDRESS). Возвращает 0 при успехе, -1 при ошибке/таймауте. */
int uhci_control_transfer(u8int dev_addr, usb_setup_packet_t *setup,
                           void *buffer, u16int length, int in_dir);

/* Подключено ли устройство к порту (0 или 1)? */
int uhci_port_connected(int port);

/* Сбросить порт (USB reset) и включить его.
 * Возвращает 1, если после сброса устройство осталось на линии. */
int uhci_port_reset(int port);

#endif
