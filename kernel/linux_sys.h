#ifndef LINUX_SYS_H
#define LINUX_SYS_H

#include <stdint.h>

void linux_sys_init(void);
int  linux_exec_elf(const char *path);

#endif
