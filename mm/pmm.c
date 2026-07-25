/*
 * Physical page-frame allocator (bitmap based).
 *
 * It reads the memory map that limine_boot_init() already converted into
 * multiboot1-style entries and stored at itdo_mmap_storage (see
 * limine_boot.c: bridge_memmap()). Rather than depending on the exact C
 * layout of `struct multiboot` (defined in a header this file doesn't
 * include, to avoid any risk of a mismatched/duplicate definition), the
 * few fields we need are read directly at the byte offsets that itdo.asm
 * uses to lay out the `mbi` label. If you ever change the field order in
 * itdo.asm's `mbi:` block, update the offsets below to match.
 */

#include "pmm.h"
#include "../lib/common.h"
#include <stdint.h>

/* --- raw access to the `mbi` structure defined in itdo.asm ------------ */
extern u8int mbi[1];

#define MBI_FLAGS        (*(u32int *)(mbi + 0))
#define MBI_MMAP_LENGTH  (*(u32int *)(mbi + 48))
#define MBI_MMAP_ADDR    (*(u64int *)(mbi + 52))
#define MULTIBOOT_FLAG_MMAP (1u << 6)

/* Layout written by bridge_memmap() in limine_boot.c (mb1_mmap_entry). */
struct mb_mmap_entry {
    u32int size;
    u32int base_lo;
    u32int base_hi;
    u32int len_lo;
    u32int len_hi;
    u32int type;
} __attribute__((packed));

#define MB_MMAP_USABLE 1

/* Track up to 16 GiB of physical memory: 1 bit / 4 KiB page
 * => 16GiB / 4096 / 8 = 512 KiB bitmap, kept in kernel .bss. */
#define PMM_MAX_MEMORY  (16ULL * 1024 * 1024 * 1024)
#define PMM_MAX_PAGES   (PMM_MAX_MEMORY / PMM_PAGE_SIZE)
#define PMM_BITMAP_SIZE (PMM_MAX_PAGES / 8)

static u8int  pmm_bitmap[PMM_BITMAP_SIZE];
static u64int pmm_highest_page = 0;   /* number of pages actually tracked */
static u64int pmm_free_count   = 0;
static u64int pmm_last_hint    = 0;

static inline void bitmap_set(u64int page)   { pmm_bitmap[page >> 3] |=  (u8int)(1u << (page & 7)); }
static inline void bitmap_clear(u64int page) { pmm_bitmap[page >> 3] &= (u8int)~(1u << (page & 7)); }
static inline int  bitmap_test(u64int page)  { return (pmm_bitmap[page >> 3] >> (page & 7)) & 1; }

static void mark_region(u64int base, u64int length, int used)
{
    if (length == 0) return;

    u64int start_page = base / PMM_PAGE_SIZE;
    u64int end_page    = (base + length + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (end_page > PMM_MAX_PAGES) end_page = PMM_MAX_PAGES;

    for (u64int p = start_page; p < end_page; p++) {
        int was_used = bitmap_test(p);
        if (used) {
            if (!was_used) { bitmap_set(p); pmm_free_count--; }
        } else {
            if (was_used) { bitmap_clear(p); pmm_free_count++; }
        }
    }
}

void pmm_init(void)
{
    /* Start fully reserved; usable regions get punched free below. */
    for (u64int i = 0; i < PMM_BITMAP_SIZE; i++) pmm_bitmap[i] = 0xFF;
    pmm_free_count   = 0;
    pmm_highest_page = 0;
    pmm_last_hint    = 0;

    if (!(MBI_FLAGS & MULTIBOOT_FLAG_MMAP) || MBI_MMAP_ADDR == 0) {
        /* No memory map available -- nothing we can safely hand out. */
        return;
    }

    u8int  *base = (u8int *)(uintptr_t)MBI_MMAP_ADDR;
    u32int  off  = 0;
    u32int  len  = MBI_MMAP_LENGTH;

    /* Defensive cap: even if the memory map turned out to be malformed,
       never loop more than this many entries. */
    u32int guard = 4096;

    while (off + sizeof(struct mb_mmap_entry) <= len && guard--) {
        struct mb_mmap_entry *e = (struct mb_mmap_entry *)(base + off);
        if (e->size == 0) break; /* malformed entry, stop instead of looping */

        u64int entry_base = ((u64int)e->base_hi << 32) | e->base_lo;
        u64int entry_len  = ((u64int)e->len_hi  << 32) | e->len_lo;

        if (e->type == MB_MMAP_USABLE) {
            mark_region(entry_base, entry_len, 0 /* free */);
            u64int top = (entry_base + entry_len) / PMM_PAGE_SIZE;
            if (top > pmm_highest_page) pmm_highest_page = top;
        }

        off += e->size + (u32int)sizeof(e->size);
    }

    if (pmm_highest_page > PMM_MAX_PAGES) pmm_highest_page = PMM_MAX_PAGES;

    /* Never hand out physical page 0 (NULL guard). */
    mark_region(0, PMM_PAGE_SIZE, 1 /* used */);
}

u64int pmm_alloc_pages(u64int count)
{
    if (count == 0 || pmm_highest_page == 0) return 0;

    u64int run = 0, run_start = 0;

    for (u64int pass = 0; pass < 2; pass++) {
        u64int start = (pass == 0) ? pmm_last_hint : 0;
        u64int end   = (pass == 0) ? pmm_highest_page : pmm_last_hint;
        run = 0;

        for (u64int p = start; p < end; p++) {
            if (!bitmap_test(p)) {
                if (run == 0) run_start = p;
                run++;
                if (run == count) {
                    for (u64int i = 0; i < count; i++) {
                        bitmap_set(run_start + i);
                        pmm_free_count--;
                    }
                    pmm_last_hint = run_start + count;
                    return run_start * PMM_PAGE_SIZE;
                }
            } else {
                run = 0;
            }
        }
        if (pmm_last_hint == 0) break; /* pass 0 already covered everything */
    }

    return 0; /* out of memory */
}

u64int pmm_alloc_page(void)
{
    return pmm_alloc_pages(1);
}

void pmm_free_pages(u64int phys_addr, u64int count)
{
    u64int start = phys_addr / PMM_PAGE_SIZE;
    for (u64int i = 0; i < count; i++) {
        u64int p = start + i;
        if (p >= pmm_highest_page) break;
        if (bitmap_test(p)) { bitmap_clear(p); pmm_free_count++; }
    }
    if (start < pmm_last_hint) pmm_last_hint = start;
}

void pmm_free_page(u64int phys_addr)
{
    pmm_free_pages(phys_addr, 1);
}

u64int pmm_free_page_count(void)  { return pmm_free_count; }
u64int pmm_total_page_count(void) { return pmm_highest_page; }
u64int pmm_used_page_count(void)  { return pmm_highest_page - pmm_free_count; }
