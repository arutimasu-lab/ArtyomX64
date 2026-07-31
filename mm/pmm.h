#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define PMM_PAGE_SIZE 4096ull
#define PMM_REGION_SIZE 0x10000000ull

void     pmm_init(uint64_t managed_base, uint64_t managed_size);
uint64_t pmm_alloc_frame(void);
uint64_t pmm_alloc_frames(uint32_t count);
void     pmm_free_frame(uint64_t phys);
void     pmm_free_frames(uint64_t phys, uint32_t count);

uint64_t pmm_total_frames(void);
uint64_t pmm_free_frames_count(void);
uint64_t pmm_used_frames(void);

bool     pmm_user_oom(void);
void     pmm_dump_stats(void);

#endif
