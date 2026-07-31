#include "vmm.h"
#include "pmm.h"
#include "malloc.h"
#include "../lib/common.h"

static uint64_t vmm_hhdm;
static uint64_t vmm_kernel_pml4;
static volatile uint32_t vmm_lock;

void     vmm_cow_ref(uint64_t phys);
void     vmm_cow_unref(uint64_t phys);
uint32_t vmm_cow_count(uint64_t phys);


static void vmm_spin_lock(void)
{
    while (__sync_lock_test_and_set(&vmm_lock, 1u))
        __asm__ volatile("pause" ::: "memory");
}

static void vmm_spin_unlock(void)
{
    __sync_lock_release(&vmm_lock);
}

static inline void vmm_invlpg(uint64_t virt)
{
    __asm__ volatile("invlpg (%0)" :: "r"(virt) : "memory");
}

static inline uint64_t read_cr3(void)
{
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v)
{
    __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory");
}

void vmm_init(uint64_t hhdm_offset)
{
    vmm_hhdm = hhdm_offset;
    vmm_kernel_pml4 = read_cr3() & ~0xFFFull;
}

uint64_t vmm_kernel_pml4_phys(void)
{
    return vmm_kernel_pml4;
}

static uint64_t *vmm_table(uint64_t phys)
{
    return (uint64_t *)(uintptr_t)(VMM_HHDM_BASE + phys);
}

static bool vmm_ensure_table(uint64_t *parent, int idx, uint64_t **out, bool user)
{
    uint64_t entry = parent[idx];
    if (entry & PTE_PRESENT) {
        *out = vmm_table(entry & ~0xFFFull);
        return true;
    }
    uint64_t phys = pmm_alloc_frame();
    if (!phys) return false;
    uint64_t *tab = vmm_table(phys);
    memset(tab, 0, VMM_PAGE_SIZE);
    uint64_t flags = PTE_PRESENT | PTE_WRITABLE;
    if (user) flags |= PTE_USER;
    parent[idx] = phys | flags;
    *out = tab;
    return true;
}

bool vmm_map_page(vas_t *as, uint64_t virt, uint64_t phys, uint64_t flags)
{
    uint64_t *pml4 = as ? as->pml4 : vmm_table(vmm_kernel_pml4);
    uint64_t *pdpt, *pd, *pt;
    int pml4i = (virt >> 39) & 0x1FF;
    int pdpti = (virt >> 30) & 0x1FF;
    int pdi   = (virt >> 21) & 0x1FF;
    int pti   = (virt >> 12) & 0x1FF;
    bool user = (flags & PTE_USER) != 0;

    vmm_spin_lock();
    if (!vmm_ensure_table(pml4, pml4i, &pdpt, user)) { vmm_spin_unlock(); return false; }
    if (!vmm_ensure_table(pdpt, pdpti, &pd, user))   { vmm_spin_unlock(); return false; }
    if (!vmm_ensure_table(pd, pdi, &pt, user))       { vmm_spin_unlock(); return false; }

    pt[pti] = (phys & ~0xFFFull) | flags | PTE_PRESENT;
    vmm_invlpg(virt);
    vmm_spin_unlock();
    return true;
}

bool vmm_unmap_page(vas_t *as, uint64_t virt)
{
    uint64_t *pml4 = as ? as->pml4 : vmm_table(vmm_kernel_pml4);
    int pml4i = (virt >> 39) & 0x1FF;
    int pdpti = (virt >> 30) & 0x1FF;
    int pdi   = (virt >> 21) & 0x1FF;
    int pti   = (virt >> 12) & 0x1FF;
    uint64_t e;

    vmm_spin_lock();
    e = pml4[pml4i];
    if (!(e & PTE_PRESENT)) { vmm_spin_unlock(); return false; }
    uint64_t *pdpt = vmm_table(e & ~0xFFFull);
    e = pdpt[pdpti];
    if (!(e & PTE_PRESENT)) { vmm_spin_unlock(); return false; }
    uint64_t *pd = vmm_table(e & ~0xFFFull);
    e = pd[pdi];
    if (!(e & PTE_PRESENT)) { vmm_spin_unlock(); return false; }
    uint64_t *pt = vmm_table(e & ~0xFFFull);
    e = pt[pti];
    if (!(e & PTE_PRESENT)) { vmm_spin_unlock(); return false; }
    pt[pti] = 0;
    vmm_invlpg(virt);
    uint64_t frame = e & ~0xFFFull;
    if (e & PTE_USER)
        vmm_cow_unref(frame);
    else
        pmm_free_frame(frame);
    vmm_spin_unlock();
    return true;
}

uint64_t vmm_get_phys(vas_t *as, uint64_t virt)
{
    uint64_t *pml4 = as ? as->pml4 : vmm_table(vmm_kernel_pml4);
    int pml4i = (virt >> 39) & 0x1FF;
    int pdpti = (virt >> 30) & 0x1FF;
    int pdi   = (virt >> 21) & 0x1FF;
    int pti   = (virt >> 12) & 0x1FF;
    uint64_t e = pml4[pml4i];
    if (!(e & PTE_PRESENT)) return 0;
    uint64_t *pdpt = vmm_table(e & ~0xFFFull);
    e = pdpt[pdpti];
    if (!(e & PTE_PRESENT)) return 0;
    uint64_t *pd = vmm_table(e & ~0xFFFull);
    e = pd[pdi];
    if (!(e & PTE_PRESENT)) return 0;
    uint64_t *pt = vmm_table(e & ~0xFFFull);
    e = pt[pti];
    if (!(e & PTE_PRESENT)) return 0;
    return (e & ~0xFFFull) | (virt & 0xFFFull);
}

static void vmm_copy_kernel_half(uint64_t *dst_pml4)
{
    uint64_t *src = vmm_table(vmm_kernel_pml4);
    for (int i = 256; i < 512; i++)
        dst_pml4[i] = src[i];
}

vas_t *vmm_create_address_space(void)
{
    vas_t *as = (vas_t*)malloc(sizeof(vas_t));
    if (!as) return NULL;
    memset(as, 0, sizeof(*as));

    uint64_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) { free(as); return NULL; }

    uint64_t *pml4 = vmm_table(pml4_phys);
    memset(pml4, 0, VMM_PAGE_SIZE);
    vmm_copy_kernel_half(pml4);

    as->pml4 = pml4;
    as->vmas = NULL;
    as->brk_base = 0;
    as->brk_cur = 0;
    as->mmap_base = 0x00007f0000000000ull;
    as->stack_top = 0x00007ffffffff000ull;
    as->stack_bottom = as->stack_top - (2ull * 1024 * 1024);
    as->mapped_pages = 0;

    vma_t *stk = (vma_t*)malloc(sizeof(vma_t));
    if (stk) {
        stk->start = as->stack_bottom;
        stk->end = as->stack_top;
        stk->flags = VMA_READ | VMA_WRITE | VMA_USER | VMA_ANON;
        stk->next = as->vmas;
        as->vmas = stk;
    }
    vma_t *guard = (vma_t*)malloc(sizeof(vma_t));
    if (guard) {
        guard->start = as->stack_bottom - VMM_PAGE_SIZE;
        guard->end = as->stack_bottom;
        guard->flags = VMA_GUARD | VMA_USER;
        guard->next = as->vmas;
        as->vmas = guard;
    }
    return as;
}

static void vmm_free_page_tables(uint64_t *table, int level)
{
    if (level > 3) return;
    for (int i = 0; i < 512; i++) {
        uint64_t e = table[i];
        if (!(e & PTE_PRESENT)) continue;
        if (level == 0 && i >= 256) break;
        if (level < 3) {
            vmm_free_page_tables(vmm_table(e & ~0xFFFull), level + 1);
            pmm_free_frame(e & ~0xFFFull);
        } else {
            uint64_t frame = e & ~0xFFFull;
            if (e & PTE_USER)
                vmm_cow_unref(frame);
            else
                pmm_free_frame(frame);
        }
    }
}

void vmm_destroy_address_space(vas_t *as)
{
    if (!as) return;
    vma_t *v = as->vmas;
    while (v) {
        vma_t *nx = v->next;
        free(v);
        v = nx;
    }
    uint64_t *pml4 = as->pml4;
    for (int i = 0; i < 256; i++) {
        uint64_t e = pml4[i];
        if (!(e & PTE_PRESENT)) continue;
        vmm_free_page_tables(vmm_table(e & ~0xFFFull), 1);
        pmm_free_frame(e & ~0xFFFull);
    }
    pmm_free_frame(vmm_virt_to_phys_hhdm((uint64_t)(uintptr_t)pml4));
    free(as);
}

void vmm_switch_to(vas_t *as)
{
    if (!as) return;
    write_cr3(vmm_virt_to_phys_hhdm((uint64_t)(uintptr_t)as->pml4));
}

vma_t *vmm_vma_find(vas_t *as, uint64_t addr)
{
    if (!as) return NULL;
    vma_t *v = as->vmas;
    while (v) {
        if (addr >= v->start && addr < v->end) return v;
        v = v->next;
    }
    return NULL;
}

vma_t *vmm_vma_alloc(vas_t *as, uint64_t start, uint64_t len, uint32_t flags)
{
    if (!as || len == 0) return NULL;
    vma_t *v = (vma_t*)malloc(sizeof(vma_t));
    if (!v) return NULL;
    v->start = start & ~(VMM_PAGE_SIZE - 1);
    v->end = (start + len + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
    v->flags = flags;
    v->next = as->vmas;
    as->vmas = v;
    return v;
}

static int vmm_vma_split(vas_t *as, vma_t *v, uint64_t start, uint64_t end, uint32_t new_flags)
{
    if (v->start >= start && v->end <= end) {
        v->flags = new_flags;
        return 0;
    }
    if (v->start < start && v->end > end) {
        vma_t *mid = (vma_t*)malloc(sizeof(vma_t));
        vma_t *tail = (vma_t*)malloc(sizeof(vma_t));
        if (!mid || !tail) { if (mid) free(mid); if (tail) free(tail); return -1; }
        mid->start = start; mid->end = end; mid->flags = new_flags;
        tail->start = end; tail->end = v->end; tail->flags = v->flags;
        v->end = start;
        mid->next = tail; tail->next = v->next; v->next = mid;
        return 0;
    }
    if (v->start < start) {
        vma_t *tail = (vma_t*)malloc(sizeof(vma_t));
        if (!tail) return -1;
        tail->start = start; tail->end = v->end; tail->flags = new_flags;
        v->end = start;
        tail->next = v->next; v->next = tail;
        return 0;
    }
    vma_t *head = (vma_t*)malloc(sizeof(vma_t));
    if (!head) return -1;
    head->start = v->start; head->end = end; head->flags = new_flags;
    v->start = end;
    head->next = v;
    vma_t **pp = &as->vmas;
    while (*pp && *pp != v) pp = &(*pp)->next;
    if (*pp == v) *pp = head;
    return 0;
}

int vmm_vma_protect(vas_t *as, uint64_t start, uint64_t len, uint32_t flags)
{
    if (!as || len == 0) return -1;
    uint64_t end = start + len;
    uint64_t addr = start & ~(VMM_PAGE_SIZE - 1);
    end = (end + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
    while (addr < end) {
        vma_t *v = vmm_vma_find(as, addr);
        if (!v) return -1;
        uint64_t clip_end = v->end < end ? v->end : end;
        if (vmm_vma_split(as, v, addr, clip_end, flags) != 0) return -1;
        uint64_t page_flags = PTE_USER;
        if (flags & VMA_WRITE) page_flags |= PTE_WRITABLE;
        if (!(flags & VMA_EXEC)) page_flags |= PTE_NX;
        for (uint64_t p = addr; p < clip_end; p += VMM_PAGE_SIZE) {
            uint64_t phys = vmm_get_phys(as, p);
            if (!phys) continue;
            vmm_spin_lock();
            {
                uint64_t *pml4 = as->pml4;
                int pml4i = (p >> 39) & 0x1FF;
                int pdpti = (p >> 30) & 0x1FF;
                int pdi   = (p >> 21) & 0x1FF;
                int pti   = (p >> 12) & 0x1FF;
                uint64_t e = pml4[pml4i];
                if (e & PTE_PRESENT) {
                    uint64_t *pdpt = vmm_table(e & ~0xFFFull);
                    e = pdpt[pdpti];
                    if (e & PTE_PRESENT) {
                        uint64_t *pd = vmm_table(e & ~0xFFFull);
                        e = pd[pdi];
                        if (e & PTE_PRESENT) {
                            uint64_t *pt = vmm_table(e & ~0xFFFull);
                            uint64_t old = pt[pti];
                            pt[pti] = (old & ~0xFFFull) | page_flags | PTE_PRESENT;
                            vmm_invlpg(p);
                        }
                    }
                }
            }
            vmm_spin_unlock();
        }
        addr = clip_end;
    }
    return 0;
}

void vmm_vma_free_range(vas_t *as, uint64_t start, uint64_t len)
{
    if (!as || len == 0) return;
    uint64_t end = (start + len + VMM_PAGE_SIZE - 1) & ~(VMM_PAGE_SIZE - 1);
    start &= ~(VMM_PAGE_SIZE - 1);
    for (uint64_t p = start; p < end; p += VMM_PAGE_SIZE)
        vmm_unmap_page(as, p);
    vma_t **pp = &as->vmas;
    while (*pp) {
        vma_t *v = *pp;
        if (v->start >= start && v->end <= end) {
            *pp = v->next;
            free(v);
            continue;
        }
        pp = &v->next;
    }
}

bool vmm_handle_fault(vas_t *as, uint64_t fault_addr, uint64_t err_code)
{
    if (!as) return false;
    uint64_t page = fault_addr & ~(VMM_PAGE_SIZE - 1);
    vma_t *v = vmm_vma_find(as, fault_addr);
    if (!v) return false;
    if (v->flags & VMA_GUARD) return false;

    bool present = (err_code & 1) != 0;
    bool write   = (err_code & 2) != 0;
    bool exec    = (err_code & 16) != 0;

    if (present) {
        if (write && !(v->flags & VMA_WRITE)) return false;
        if (exec  && !(v->flags & VMA_EXEC))  return false;

        uint64_t old_phys = vmm_get_phys(as, page);
        if (old_phys && write) {
            uint64_t old_frame = old_phys & ~0xFFFull;
            if (vmm_cow_count(old_frame) > 1) {
                if (pmm_user_oom()) return false;
                uint64_t new_phys = pmm_alloc_frame();
                if (!new_phys) return false;
                uint64_t *src = (uint64_t*)(uintptr_t)(VMM_HHDM_BASE + old_frame);
                uint64_t *dst = (uint64_t*)(uintptr_t)(VMM_HHDM_BASE + new_phys);
                for (int i = 0; i < 512; i++) dst[i] = src[i];
                uint64_t flags = PTE_USER | PTE_WRITABLE;
                if (!(v->flags & VMA_EXEC)) flags |= PTE_NX;
                vmm_unmap_page(as, page);
                vmm_cow_unref(old_frame);
                if (!vmm_map_page(as, page, new_phys, flags)) {
                    pmm_free_frame(new_phys);
                    return false;
                }
                return true;
            }
        }
        return false;
    }

    if (pmm_user_oom()) return false;

    uint64_t phys = pmm_alloc_frame();
    if (!phys) return false;
    uint64_t *page_ptr = (uint64_t*)(uintptr_t)(VMM_HHDM_BASE + phys);
    memset(page_ptr, 0, VMM_PAGE_SIZE);

    uint64_t flags = PTE_USER;
    if (v->flags & VMA_WRITE) flags |= PTE_WRITABLE;
    if (!(v->flags & VMA_EXEC)) flags |= PTE_NX;

    if (!vmm_map_page(as, page, phys, flags)) {
        pmm_free_frame(phys);
        return false;
    }
    as->mapped_pages++;
    return true;
}

#define VMM_COW_HASH 8192u

typedef struct cow_entry {
    uint64_t phys;
    uint32_t count;
    uint8_t  used;
} cow_entry_t;

static cow_entry_t vmm_cow_table[VMM_COW_HASH];

static uint32_t vmm_cow_hash(uint64_t phys)
{
    return (uint32_t)((phys >> 12) % VMM_COW_HASH);
}

void vmm_cow_ref(uint64_t phys)
{
    uint32_t h = vmm_cow_hash(phys);
    for (uint32_t i = 0; i < VMM_COW_HASH; i++) {
        uint32_t idx = (h + i) % VMM_COW_HASH;
        if (vmm_cow_table[idx].used && vmm_cow_table[idx].phys == phys) {
            vmm_cow_table[idx].count++;
            return;
        }
        if (!vmm_cow_table[idx].used) {
            vmm_cow_table[idx].used = 1;
            vmm_cow_table[idx].phys = phys;
            vmm_cow_table[idx].count = 1;
            return;
        }
    }
}

void vmm_cow_unref(uint64_t phys)
{
    uint32_t h = vmm_cow_hash(phys);
    for (uint32_t i = 0; i < VMM_COW_HASH; i++) {
        uint32_t idx = (h + i) % VMM_COW_HASH;
        if (!vmm_cow_table[idx].used) return;
        if (vmm_cow_table[idx].phys == phys) {
            if (vmm_cow_table[idx].count > 1) {
                vmm_cow_table[idx].count--;
            } else {
                vmm_cow_table[idx].used = 0;
                vmm_cow_table[idx].count = 0;
                pmm_free_frame(phys);
            }
            return;
        }
    }
}

uint32_t vmm_cow_count(uint64_t phys)
{
    uint32_t h = vmm_cow_hash(phys);
    for (uint32_t i = 0; i < VMM_COW_HASH; i++) {
        uint32_t idx = (h + i) % VMM_COW_HASH;
        if (!vmm_cow_table[idx].used) return 0;
        if (vmm_cow_table[idx].phys == phys)
            return vmm_cow_table[idx].count;
    }
    return 0;
}

static uint64_t vmm_fork_clone_table(int level, uint64_t parent_phys)
{
    uint64_t *parent_tab = vmm_table(parent_phys);
    uint64_t child_phys = pmm_alloc_frame();
    if (!child_phys) return 0;
    uint64_t *child_tab = vmm_table(child_phys);
    memset(child_tab, 0, VMM_PAGE_SIZE);

    for (int i = 0; i < 512; i++) {
        uint64_t e = parent_tab[i];
        if (!(e & PTE_PRESENT)) continue;
        if (level == 0 && i >= 256) { child_tab[i] = e; continue; }
        if (level < 3) {
            uint64_t sub = vmm_fork_clone_table(level + 1, e & ~0xFFFull);
            if (sub)
                child_tab[i] = sub | (e & 0xFFF) | PTE_PRESENT;
        } else {
            uint64_t phys = e & ~0xFFFull;
            uint64_t flags = e & (PTE_NX | 0xFFF);
            if (flags & PTE_USER) {
                flags &= ~PTE_WRITABLE;
                vmm_cow_ref(phys);
            }
            child_tab[i] = phys | flags | PTE_PRESENT;
        }
    }
    return child_phys;
}

vas_t *vmm_fork_space(vas_t *parent)
{
    if (!parent) return NULL;
    vas_t *child = (vas_t*)malloc(sizeof(vas_t));
    if (!child) return NULL;
    memset(child, 0, sizeof(*child));
    child->refcount = 1;

    uint64_t parent_pml4_phys = vmm_virt_to_phys_hhdm((uint64_t)(uintptr_t)parent->pml4);
    uint64_t child_pml4_phys = vmm_fork_clone_table(0, parent_pml4_phys);
    if (!child_pml4_phys) { free(child); return NULL; }
    child->pml4 = vmm_table(child_pml4_phys);

    vma_t *pv = parent->vmas;
    vma_t **tail = &child->vmas;
    while (pv) {
        vma_t *nv = (vma_t*)malloc(sizeof(vma_t));
        if (!nv) break;
        *nv = *pv;
        nv->next = NULL;
        *tail = nv;
        tail = &nv->next;
        pv = pv->next;
    }

    child->brk_base = parent->brk_base;
    child->brk_cur = parent->brk_cur;
    child->mmap_base = parent->mmap_base;
    child->stack_top = parent->stack_top;
    child->stack_bottom = parent->stack_bottom;
    child->mapped_pages = parent->mapped_pages;
    return child;
}
