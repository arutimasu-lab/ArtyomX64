/*
 * Kernel heap allocator (kmalloc/kfree).
 *
 * Grows on demand by requesting physically-contiguous page runs from the
 * PMM and accessing them through Limine's HHDM (higher-half direct map),
 * so no page-table code is needed here: phys X is always visible at
 * virtual (limine_hhdm_offset + X).
 *
 * Each growth request ("arena") is a first-fit free-list heap of its own;
 * kmalloc() searches all arenas and, if nothing fits, allocates a new one.
 */
#define malloc kmalloc 
#define free kfree
#include "kheap.h"
#include "pmm.h"
#include "../lib/common.h"
#include <stddef.h>
#include <stdint.h>

extern u64int limine_hhdm_offset;

#define ALIGN16(x)       (((x) + 15) & ~(size_t)15)
#define ARENA_MIN_PAGES  16   /* 64 KiB minimum growth chunk */

typedef struct block_header {
    size_t size;                 /* payload size, not including header */
    int    free;
    struct block_header *next;   /* next block in this arena, by address */
    struct block_header *prev;
} block_header_t;

typedef struct arena {
    struct arena *next;
    size_t total_size;           /* bytes available for blocks in this arena */
} arena_t;

static arena_t *arena_list  = 0;
static int       kheap_ready = 0;

static inline void *phys_to_virt(u64int phys)
{
    return (void *)(uintptr_t)(phys + limine_hhdm_offset);
}

static inline block_header_t *arena_first_block(arena_t *a)
{
    return (block_header_t *)((u8int *)a + sizeof(arena_t));
}

static arena_t *new_arena(size_t min_payload)
{
    size_t needed = sizeof(arena_t) + sizeof(block_header_t) + min_payload;
    u64int pages  = (needed + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (pages < ARENA_MIN_PAGES) pages = ARENA_MIN_PAGES;

    u64int phys = pmm_alloc_pages(pages);
    if (phys == 0) return 0; /* out of physical memory */

    arena_t *a = (arena_t *)phys_to_virt(phys);
    a->total_size = pages * PMM_PAGE_SIZE - sizeof(arena_t);

    block_header_t *b = arena_first_block(a);
    b->size = a->total_size - sizeof(block_header_t);
    b->free = 1;
    b->next = 0;
    b->prev = 0;

    a->next = arena_list;
    arena_list = a;
    return a;
}

void kheap_init(void)
{
    kheap_ready = 1;
}

static void split_block(block_header_t *b, size_t size)
{
    if (b->size < size) return;
    size_t remaining = b->size - size;
    if (remaining <= sizeof(block_header_t) + 16) return; /* not worth it */

    block_header_t *nb = (block_header_t *)((u8int *)(b + 1) + size);
    nb->size = remaining - sizeof(block_header_t);
    nb->free = 1;
    nb->next = b->next;
    nb->prev = b;
    if (b->next) b->next->prev = nb;
    b->next = nb;
    b->size = size;
}

void *kmalloc(size_t size)
{
    if (!kheap_ready) kheap_init();
    if (size == 0) return 0;
    size = ALIGN16(size);

    for (arena_t *a = arena_list; a; a = a->next) {
        for (block_header_t *b = arena_first_block(a); b; b = b->next) {
            if (b->free && b->size >= size) {
                split_block(b, size);
                b->free = 0;
                return (void *)(b + 1);
            }
        }
    }

    arena_t *a = new_arena(size);
    if (!a) return 0;

    block_header_t *b = arena_first_block(a);
    split_block(b, size);
    b->free = 0;
    return (void *)(b + 1);
}

void kfree(void *ptr)
{
    if (!ptr) return;

    block_header_t *b = (block_header_t *)ptr - 1;
    b->free = 1;

    /* Coalesce with the next block if it's physically adjacent and free. */
    if (b->next && b->next->free &&
        (u8int *)(b + 1) + b->size == (u8int *)b->next) {
        block_header_t *n = b->next;
        b->size += sizeof(block_header_t) + n->size;
        b->next = n->next;
        if (n->next) n->next->prev = b;
    }
    /* Coalesce with the previous block if it's physically adjacent and free. */
    if (b->prev && b->prev->free &&
        (u8int *)(b->prev + 1) + b->prev->size == (u8int *)b) {
        block_header_t *p = b->prev;
        p->size += sizeof(block_header_t) + b->size;
        p->next = b->next;
        if (b->next) b->next->prev = p;
        b = p;
    }
    (void)b;
}

void *kcalloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    void *p = kmalloc(total);
    if (p) memset((u8int *)p, 0, (u32int)total);
    return p;
}

void *krealloc(void *ptr, size_t size)
{
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return 0; }

    block_header_t *b = (block_header_t *)ptr - 1;
    if (b->size >= size) return ptr;

    void *np = kmalloc(size);
    if (!np) return 0;
    memcpy((u8int *)np, (u8int *)ptr, (u32int)b->size);
    kfree(ptr);
    return np;
}
