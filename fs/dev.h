// dev.h -- Defines the interface for devices
#ifndef DEV_H
#define DEV_H

#include "../fs/fs.h"
#include "../lib/common.h"

// Типы устройств
#define DEV_TYPE_CHAR    0x01
#define DEV_TYPE_BLOCK   0x02
#define DEV_TYPE_NULL    0x03
#define DEV_TYPE_ZERO    0x04
#define DEV_TYPE_RANDOM  0x05

// Структура устройства
typedef struct device {
    char name[64];
    u32int type;
    u32int major;
    u32int minor;
    fs_node_t *node;
    void *priv_data;
    
    // Callbacks для операций с устройством
    u32int (*read)(struct device *dev, u32int offset, u32int size, u8int *buffer);
    u32int (*write)(struct device *dev, u32int offset, u32int size, u8int *buffer);
    void (*open)(struct device *dev);
    void (*close)(struct device *dev);
    int (*ioctl)(struct device *dev, u32int cmd, void *arg);
} device_t;

// Регистрация устройства
int dev_register(device_t *dev);
int dev_unregister(device_t *dev);
device_t *dev_find(const char *name);
device_t *dev_find_by_major_minor(u32int major, u32int minor);

// Инициализация
void initialise_devfs(void);

#endif