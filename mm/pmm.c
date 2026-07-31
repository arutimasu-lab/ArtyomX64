#include "pmm.h"
#include "../lib/common.h"

#define PMM_MAX_FRAMES   65536u
#define PMM_STACK_CAP    PMM_MAX_FRAMES
#define PMM_KERNEL_RESERVE 1024u

static uint64_t pmm_free_stack[PMM_STACK_CAP];
static uint32_t pmm_stack_top;
static uint64_t pmm_frames_total;
static uint64_t pmm_frames_used;
static uint64_t pmm_region_base;
static uint64_t pmm_region_size;
static int      pmm_ready;
static volatile uint32_t pmm_lock;

static void pmm_spin_lock(void)
{
    while (__sync_lock_test_and_set(&pmm_lock, 1u))
        __asm__ volatile("pause" ::: "memory");
}

static void pmm_spin_unlock(void)
{
    __sync_lock_release(&pmm_lock);
}

void pmm_init(uint64_t managed_base, uint64_t managed_size)
{
    uint64_t base;
    uint64_t size;
    uint64_t pages;
    uint64_t i;

    pmm_spin_lock();
    if (pmm_ready) {
        pmm_spin_unlock();
        return;
    }

    base = (managed_base + (PMM_PAGE_SIZE - 1)) & ~(PMM_PAGE_SIZE - 1);
    size = managed_size & ~(PMM_PAGE_SIZE - 1);
    if (base >= managed_base + managed_size)
        size = 0;
    else if (size > (managed_base + managed_size) - base)
        size = (managed_base + managed_size) - base;

    pages = size / PMM_PAGE_SIZE;
    if (pages > PMM_MAX_FRAMES)
        pages = PMM_MAX_FRAMES;

    pmm_stack_top = 0;
    for (i = 0; i < pages; i++) {
        pmm_free_stack[pmm_stack_top++] = base + i * PMM_PAGE_SIZE;
    }

    pmm_region_base = base;
    pmm_region_size = pages * PMM_PAGE_SIZE;
    pmm_frames_total = pages;
    pmm_frames_used = 0;
    pmm_ready = 1;
    pmm_spin_unlock();
}

static uint64_t pmm_pop_frame(void)
{
    if (pmm_stack_top == 0)
        return 0;
    pmm_stack_top--;
    pmm_frames_used++;
    return pmm_free_stack[pmm_stack_top];
}

uint64_t pmm_alloc_frame(void)
{
    uint64_t frame;
    pmm_spin_lock();
    frame = pmm_pop_frame();
    pmm_spin_unlock();
    return frame;
}

uint64_t pmm_alloc_frames(uint32_t count)
{
    uint64_t first;
    uint32_t i;

    if (count == 0)
        return 0;
    pmm_spin_lock();
    if ((uint64_t)pmm_stack_top < count) {
        pmm_spin_unlock();
        return 0;
    }
    first = pmm_free_stack[pmm_stack_top - 1];
    for (i = 0; i < count; i++)
        (void)pmm_pop_frame();
    pmm_spin_unlock();
    return first;
}

void pmm_free_frame(uint64_t phys)
{
    if (!pmm_ready || phys == 0)
        return;
    if (phys < pmm_region_base || phys >= pmm_region_base + pmm_region_size)
        return;
    pmm_spin_lock();
    if (pmm_stack_top < PMM_STACK_CAP && pmm_frames_used > 0) {
        pmm_free_stack[pmm_stack_top++] = phys & ~(PMM_PAGE_SIZE - 1);
        pmm_frames_used--;
    }
    pmm_spin_unlock();
}

void pmm_free_frames(uint64_t phys, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++)
        pmm_free_frame(phys + (uint64_t)i * PMM_PAGE_SIZE);
}

uint64_t pmm_total_frames(void)      { return pmm_frames_total; }
uint64_t pmm_free_frames_count(void) { return pmm_stack_top; }
uint64_t pmm_used_frames(void)       { return pmm_frames_used; }

bool pmm_user_oom(void)
{
    return pmm_stack_top < PMM_KERNEL_RESERVE;
}

void pmm_dump_stats(void)
{
}
