// dev.c -- Implementation of the device system
#include "dev.h"
#include "../mm/kheap.h"
#include "../lib/common.h"

#define MAX_DEVICES 64

static device_t *devices[MAX_DEVICES];
static int num_devices = 0;
static fs_node_t *devfs_root;

// Стандартные устройства
static device_t *null_device;
static device_t *zero_device;
static device_t *random_device;

// ========== VFS Callbacks для устройств ==========

static u32int devfs_read(fs_node_t *node, u32int offset, u32int size, u8int *buffer)
{
    device_t *dev = (device_t*)node->impl;
    if (dev && dev->read)
        return dev->read(dev, offset, size, buffer);
    return 0;
}

static u32int devfs_write(fs_node_t *node, u32int offset, u32int size, u8int *buffer)
{
    device_t *dev = (device_t*)node->impl;
    if (dev && dev->write)
        return dev->write(dev, offset, size, buffer);
    return 0;
}

static void devfs_open(fs_node_t *node)
{
    device_t *dev = (device_t*)node->impl;
    if (dev && dev->open)
        dev->open(dev);
}

static void devfs_close(fs_node_t *node)
{
    device_t *dev = (device_t*)node->impl;
    if (dev && dev->close)
        dev->close(dev);
}

static struct dirent *devfs_readdir(fs_node_t *node, u32int index)
{
    static struct dirent dirent;
    if (index >= num_devices)
        return NULL;
    
    strcpy(dirent.name, devices[index]->name);
    dirent.name[strlen(devices[index]->name)] = 0;
    dirent.ino = index + 1;
    return &dirent;
}

static fs_node_t *devfs_finddir(fs_node_t *node, char *name)
{
    for (int i = 0; i < num_devices; i++) {
        if (!strcmp(name, devices[i]->name))
            return devices[i]->node;
    }
    return NULL;
}

// ========== Устройства по умолчанию ==========

// /dev/null - отбрасывает все данные, чтение возвращает EOF
static u32int null_read(device_t *dev, u32int offset, u32int size, u8int *buffer)
{
    return 0; // EOF
}

static u32int null_write(device_t *dev, u32int offset, u32int size, u8int *buffer)
{
    return size; // "Успешно" записано
}

// /dev/zero - возвращает нули при чтении, отбрасывает запись
static u32int zero_read(device_t *dev, u32int offset, u32int size, u8int *buffer)
{
    memset(buffer, 0, size);
    return size;
}

static u32int zero_write(device_t *dev, u32int offset, u32int size, u8int *buffer)
{
    return size;
}

// Простой генератор случайных чисел (LFSR)
static u32int random_seed = 0x12345678;

static u32int random_next(void)
{
    random_seed ^= (random_seed << 13) & 0xFFFFFFFF;
    random_seed ^= random_seed >> 17;
    random_seed ^= (random_seed << 5) & 0xFFFFFFFF;
    return random_seed;
}

static u32int random_read(device_t *dev, u32int offset, u32int size, u8int *buffer)
{
    u32int bytes = 0;
    while (bytes < size) {
        u32int rnd = random_next();
        u32int copy = (size - bytes > 4) ? 4 : (size - bytes);
        memcpy(buffer + bytes, &rnd, copy);
        bytes += copy;
    }
    return bytes;
}

static u32int random_write(device_t *dev, u32int offset, u32int size, u8int *buffer)
{
    return size; // "Записано", хотя данные игнорируются
}

// ========== Управление устройствами ==========

int dev_register(device_t *dev)
{
    if (num_devices >= MAX_DEVICES)
        return -1;
    
    // Проверка на дубликат имени
    for (int i = 0; i < num_devices; i++) {
        if (!strcmp(devices[i]->name, dev->name))
            return -2;
    }
    
    // Создаём VFS узел для устройства
    fs_node_t *node = (fs_node_t*)malloc(sizeof(fs_node_t));
    if (!node)
        return -3;
    
    strcpy(node->name, dev->name);
    node->mask = 0666;
    node->uid = node->gid = 0;
    node->flags = FS_CHARDEVICE;
    node->inode = num_devices + 1;
    node->length = 0;
    node->impl = (u32int)(uintptr_t)dev;
    node->read = &devfs_read;
    node->write = &devfs_write;
    node->open = &devfs_open;
    node->close = &devfs_close;
    node->readdir = NULL;
    node->finddir = NULL;
    node->ptr = NULL;
    
    dev->node = node;
    devices[num_devices++] = dev;
    
    return 0;
}

int dev_unregister(device_t *dev)
{
    for (int i = 0; i < num_devices; i++) {
        if (devices[i] == dev) {
            // Удаляем устройство
            if (devices[i]->node) {
                free(devices[i]->node);
            }
            // Сдвигаем оставшиеся
            for (int j = i; j < num_devices - 1; j++) {
                devices[j] = devices[j + 1];
            }
            num_devices--;
            return 0;
        }
    }
    return -1;
}

device_t *dev_find(const char *name)
{
    for (int i = 0; i < num_devices; i++) {
        if (!strcmp(devices[i]->name, name))
            return devices[i];
    }
    return NULL;
}

device_t *dev_find_by_major_minor(u32int major, u32int minor)
{
    for (int i = 0; i < num_devices; i++) {
        if (devices[i]->major == major && devices[i]->minor == minor)
            return devices[i];
    }
    return NULL;
}

// ========== Инициализация ==========

void initialise_devfs(void)
{
    // Создаём корень /dev
    devfs_root = (fs_node_t*)malloc(sizeof(fs_node_t));
    strcpy(devfs_root->name, "dev");
    devfs_root->mask = devfs_root->uid = devfs_root->gid = 0;
    devfs_root->flags = FS_DIRECTORY;
    devfs_root->inode = 0;
    devfs_root->length = 0;
    devfs_root->read = 0;
    devfs_root->write = 0;
    devfs_root->open = 0;
    devfs_root->close = 0;
    devfs_root->readdir = &devfs_readdir;
    devfs_root->finddir = &devfs_finddir;
    devfs_root->impl = 0;
    devfs_root->ptr = 0;
    
    // Регистрируем /dev/null
    null_device = (device_t*)malloc(sizeof(device_t));
    strcpy(null_device->name, "null");
    null_device->type = DEV_TYPE_NULL;
    null_device->major = 1;
    null_device->minor = 3;
    null_device->read = null_read;
    null_device->write = null_write;
    null_device->open = NULL;
    null_device->close = NULL;
    null_device->ioctl = NULL;
    null_device->priv_data = NULL;
    dev_register(null_device);
    
    // Регистрируем /dev/zero
    zero_device = (device_t*)malloc(sizeof(device_t));
    strcpy(zero_device->name, "zero");
    zero_device->type = DEV_TYPE_ZERO;
    zero_device->major = 1;
    zero_device->minor = 5;
    zero_device->read = zero_read;
    zero_device->write = zero_write;
    zero_device->open = NULL;
    zero_device->close = NULL;
    zero_device->ioctl = NULL;
    zero_device->priv_data = NULL;
    dev_register(zero_device);
    
    // Регистрируем /dev/random
    random_device = (device_t*)malloc(sizeof(device_t));
    strcpy(random_device->name, "random");
    random_device->type = DEV_TYPE_RANDOM;
    random_device->major = 1;
    random_device->minor = 8;
    random_device->read = random_read;
    random_device->write = random_write;
    random_device->open = NULL;
    random_device->close = NULL;
    random_device->ioctl = NULL;
    random_device->priv_data = NULL;
    dev_register(random_device);
    
    // Если есть существующий корень FS, монтируем /dev
    if (fs_root) {
        // Здесь можно добавить код для монтирования devfs в существующую FS
        // Например, создать узел /dev в корневой FS, указывающий на devfs_root
    }
}