#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define VMM_PAGE_SIZE   4096ull
#define VMM_HHDM_BASE   0xffff800000000000ull

#define PTE_PRESENT     (1ull << 0)
#define PTE_WRITABLE    (1ull << 1)
#define PTE_USER        (1ull << 2)
#define PTE_PWT         (1ull << 3)
#define PTE_PCD         (1ull << 4)
#define PTE_ACCESSED    (1ull << 5)
#define PTE_DIRTY       (1ull << 6)
#define PTE_HUGE        (1ull << 7)
#define PTE_GLOBAL      (1ull << 8)
#define PTE_COW         (1ull << 9)
#define PTE_NX          (1ull << 63)

#define VMA_READ    0x1u
#define VMA_WRITE   0x2u
#define VMA_EXEC    0x4u
#define VMA_USER    0x8u
#define VMA_ANON    0x10u
#define VMA_GUARD   0x20u

#define VMA_MAX_PER_PROC 128u

typedef struct vma {
    uint64_t start;
    uint64_t end;
    uint32_t flags;
    struct vma *next;
} vma_t;

typedef struct vas {
    uint64_t *pml4;
    vma_t    *vmas;
    uint64_t  brk_base;
    uint64_t  brk_cur;
    uint64_t  mmap_base;
    uint64_t  stack_top;
    uint64_t  stack_bottom;
    uint32_t  mapped_pages;
    int       refcount;
} vas_t;

static inline uint64_t vmm_phys_to_virt(uint64_t phys)
{
    return VMM_HHDM_BASE + phys;
}

static inline uint64_t vmm_virt_to_phys_hhdm(uint64_t virt)
{
    return virt - VMM_HHDM_BASE;
}

void     vmm_init(uint64_t hhdm_offset);
vas_t   *vmm_create_address_space(void);
void     vmm_destroy_address_space(vas_t *as);
bool     vmm_map_page(vas_t *as, uint64_t virt, uint64_t phys, uint64_t flags);
bool     vmm_unmap_page(vas_t *as, uint64_t virt);
uint64_t vmm_get_phys(vas_t *as, uint64_t virt);

vma_t   *vmm_vma_alloc(vas_t *as, uint64_t start, uint64_t len, uint32_t flags);
vma_t   *vmm_vma_find(vas_t *as, uint64_t addr);
void     vmm_vma_free_range(vas_t *as, uint64_t start, uint64_t len);
int      vmm_vma_protect(vas_t *as, uint64_t start, uint64_t len, uint32_t flags);

bool     vmm_handle_fault(vas_t *as, uint64_t fault_addr, uint64_t err_code);

uint64_t vmm_kernel_pml4_phys(void);
void     vmm_switch_to(vas_t *as);

vas_t   *vmm_fork_space(vas_t *parent);
void     vmm_cow_ref(uint64_t phys);
void     vmm_cow_unref(uint64_t phys);
uint32_t vmm_cow_count(uint64_t phys);

#endif
