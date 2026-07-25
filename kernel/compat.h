#ifndef COMPAT_H
#define COMPAT_H

#include <stdint.h>
#include "../lib/common.h"

#define COMPAT_NATIVE  0
#define COMPAT_LINUX32 1
#define COMPAT_LINUX64 2
#define COMPAT_DARWIN  3

#define SYSCALL_PATCH_LINUX  129
#define SYSCALL_PATCH_DARWIN 130

int compat_detect_format(const void *buf, uint32_t size);
void *compat_load(void *buf, uint32_t size, int format);
void compat_patch_syscalls(void *code, uint32_t size, int patch_int);
void compat_init(void);
void compat_register_handlers(void);

int compat_exec(const char *path);
int compat_exec_buf(void *buf, uint32_t size);

void linux_syscall_handler(u64int rax, u64int rdi, u64int rsi, u64int rdx,
                           u64int r10, u64int r8, u64int r9,
                           u64int *out_rax);

void darwin_syscall_handler(u64int rax, u64int rdi, u64int rsi, u64int rdx,
                            u64int r10, u64int r8, u64int r9,
                            u64int *out_rax);

int call_compat64_thunk(uint64_t entry, int format);

#endif