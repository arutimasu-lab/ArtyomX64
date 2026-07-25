#ifndef PMM_H
#define PMM_H

#include "../lib/common.h"
#include <stddef.h>

#define PMM_PAGE_SIZE 4096ULL

/* Must be called once, after limine_boot_init() has populated `mbi`
 * (i.e. after the framebuffer/mmap bridging in limine_boot.c has run)
 * and before anything calls pmm_alloc_page()/kmalloc(). */
void   pmm_init(void);

u64int pmm_alloc_page(void);
u64int pmm_alloc_pages(u64int count);   /* returns base phys addr of a
                                            physically-contiguous run,
                                            or 0 on failure */
void   pmm_free_page(u64int phys_addr);
void   pmm_free_pages(u64int phys_addr, u64int count);

u64int pmm_free_page_count(void);
u64int pmm_used_page_count(void);
u64int pmm_total_page_count(void);

#endif
