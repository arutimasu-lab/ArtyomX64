#include <stdint.h>
#include <stddef.h>

#include "multiboot.h"
#include "limine.h"

#define LIMINE_COMMON_MAGIC_0 0xc7b1dd30df4c8b88
#define LIMINE_COMMON_MAGIC_1 0x0a82e883a194f07b

#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES 6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

extern volatile struct limine_framebuffer_request limine_framebuffer_request;
extern volatile struct limine_module_request       limine_module_request;
extern volatile struct limine_hhdm_request         limine_hhdm_request;
extern volatile struct limine_memmap_request       limine_memmap_request;
extern volatile struct limine_bootloader_info_request limine_bootloader_info_request;
extern volatile struct limine_executable_cmdline_request limine_executable_cmdline_request;
extern volatile struct limine_executable_file_request  limine_executable_file_request;
extern volatile struct limine_rsdp_request         limine_rsdp_request;
extern volatile struct limine_smbios_request       limine_smbios_request;
extern volatile struct limine_tsc_frequency_request limine_tsc_frequency_request;

extern uint64_t limine_base_revision[3];

extern uint64_t framebuffer_addr;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;
extern uint32_t framebuffer_bpp;
extern uint64_t limine_hhdm_offset;
extern uint64_t limine_rsdp_addr;
extern uint64_t limine_smbios_addr;
extern uint64_t limine_bootloader_name;
extern uint64_t limine_cmdline_addr;
extern uint64_t limine_tsc_frequency;
extern uint64_t limine_loaded_base_revision;
extern uint64_t limine_executable_file_ptr;
extern uint64_t limine_base_revision[3];

extern struct multiboot mbi;
extern uint8_t itdo_mod_storage[8192];
extern uint8_t itdo_mmap_storage[8192];

struct mb1_mmap_entry {
    uint32_t size;
    uint32_t base_lo;
    uint32_t base_hi;
    uint32_t len_lo;
    uint32_t len_hi;
    uint32_t type;
} __attribute__((packed));

struct mb1_mod_list {
    uint64_t mod_start;
    uint64_t mod_end;
    uint64_t string;
    uint64_t reserved;
} __attribute__((packed));

static uint32_t cvt_memmap_type(uint64_t t)
{
    switch (t) {
        case LIMINE_MEMMAP_USABLE:                 return 1;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       return 3;
        case LIMINE_MEMMAP_ACPI_NVS:               return 4;
        case LIMINE_MEMMAP_BAD_MEMORY:             return 5;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: return 2;
        case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES: return 1;
        case LIMINE_MEMMAP_FRAMEBUFFER:            return 2;
        case LIMINE_MEMMAP_RESERVED:
        default:                                   return 2;
    }
}

static void bridge_framebuffer(void)
{
    struct limine_framebuffer_response *resp =
        (struct limine_framebuffer_response *)limine_framebuffer_request.response;

    if (resp == NULL || resp->framebuffer_count == 0) {
        framebuffer_addr = 0;
        mbi.flags &= ~MULTIBOOT_FLAG_FB;
        return;
    }

    struct limine_framebuffer *fb =
        (struct limine_framebuffer *)resp->framebuffers[0];

    if (fb == NULL || fb->address == NULL) {
        framebuffer_addr = 0;
        mbi.flags &= ~MULTIBOOT_FLAG_FB;
        return;
    }

    uint64_t addr = (uint64_t)(uintptr_t)fb->address;
    framebuffer_addr    = addr;
    framebuffer_width   = (uint32_t)fb->width;
    framebuffer_height  = (uint32_t)fb->height;
    framebuffer_pitch   = (uint32_t)fb->pitch;
    framebuffer_bpp     = (uint32_t)fb->bpp;

    mbi.framebuffer_addr   = addr;
    mbi.framebuffer_width  = framebuffer_width;
    mbi.framebuffer_height = framebuffer_height;
    mbi.framebuffer_pitch  = framebuffer_pitch;
    mbi.framebuffer_bpp    = (uint8_t)fb->bpp;
    mbi.framebuffer_type   = (fb->memory_model == LIMINE_FRAMEBUFFER_RGB) ? 1 : 2;

    if (fb->memory_model == LIMINE_FRAMEBUFFER_RGB) {
        uint8_t *ci = mbi.color_info;
        ci[0] = fb->red_mask_size;   ci[1] = fb->red_mask_shift;
        ci[2] = fb->green_mask_size; ci[3] = fb->green_mask_shift;
        ci[4] = fb->blue_mask_size;  ci[5] = fb->blue_mask_shift;
    }

    mbi.flags |= MULTIBOOT_FLAG_FB;
}

static void bridge_modules(void)
{
    struct limine_module_response *resp =
        (struct limine_module_response *)limine_module_request.response;

    if (resp == NULL || resp->module_count == 0) {
        mbi.mods_count = 0;
        mbi.flags &= ~MULTIBOOT_FLAG_MODS;
        return;
    }

    struct mb1_mod_list *mods = (struct mb1_mod_list *)itdo_mod_storage;
    uint64_t count = resp->module_count;
    if (count > 250) count = 250;

    uint64_t usable = 0;
    for (uint64_t i = 0; i < count; i++) {
        struct limine_file *mf = (struct limine_file *)resp->modules[i];
        if (mf == NULL) continue;

        uint64_t start = (uint64_t)(uintptr_t)mf->address;
        uint64_t end   = start + mf->size;

        mods[usable].mod_start = start;
        mods[usable].mod_end   = end;
        mods[usable].string    = (uint64_t)(uintptr_t)mf->string;
        mods[usable].reserved  = 0;
        usable++;
    }

    mbi.mods_addr  = (uint64_t)(uintptr_t)itdo_mod_storage;
    mbi.mods_count = (uint32_t)usable;
    mbi.flags |= MULTIBOOT_FLAG_MODS;
}

static void bridge_memmap(void)
{
    struct limine_memmap_response *resp =
        (struct limine_memmap_response *)limine_memmap_request.response;

    if (resp == NULL || resp->entry_count == 0) {
        mbi.mmap_length = 0;
        mbi.flags &= ~MULTIBOOT_FLAG_MMAP;
        return;
    }

    struct mb1_mmap_entry *dst = (struct mb1_mmap_entry *)itdo_mmap_storage;
    uint64_t count = resp->entry_count;
    if (count > 320) count = 320;

    uint32_t total = 0;
    uint64_t low_top = 0;
    uint64_t hi_base = 0xFFFFFFFFu;

    for (uint64_t i = 0; i < count; i++) {
        struct limine_memmap_entry *e =
            (struct limine_memmap_entry *)resp->entries[i];
        if (e == NULL) continue;

        uint64_t base = e->base;
        uint64_t len  = e->length;

        if (e->type == LIMINE_MEMMAP_USABLE) {
            if (base < 0x100000 && base + len > low_top)
                low_top = base + len;
            if (base >= 0x100000 && base < hi_base)
                hi_base = base;
        }

        dst[total].size    = (uint32_t)(sizeof(struct mb1_mmap_entry) - 4);
        dst[total].base_lo = (uint32_t)(base & 0xFFFFFFFFu);
        dst[total].base_hi = (uint32_t)(base >> 32);
        dst[total].len_lo  = (uint32_t)(len & 0xFFFFFFFFu);
        dst[total].len_hi  = (uint32_t)(len >> 32);
        dst[total].type    = cvt_memmap_type(e->type);
        total++;

        uint32_t sz = (uint32_t)sizeof(struct mb1_mmap_entry);
        if (total * sz > sizeof(itdo_mmap_storage) - sz) break;
    }

    mbi.mmap_addr   = (uint64_t)(uintptr_t)itdo_mmap_storage;
    mbi.mmap_length = total * (uint32_t)sizeof(struct mb1_mmap_entry);
    mbi.mem_lower   = (uint32_t)((low_top < 0x100000 ? low_top : 0x100000) >> 10);
    mbi.mem_upper   = (uint32_t)(((hi_base - 0x100000) > 0 ? (hi_base - 0x100000) : 0) >> 10);
    mbi.flags |= MULTIBOOT_FLAG_MMAP | MULTIBOOT_FLAG_MEM;
}

static void bridge_misc(void)
{
    if (limine_base_revision[2] != 0) {
        if (limine_base_revision[1] == 0x6a7b384944536bdc) {
            limine_loaded_base_revision = 0;
        } else {
            limine_loaded_base_revision = limine_base_revision[1];
        }
    } else {
        limine_loaded_base_revision = 0;
    }

    struct limine_executable_file_response *ef =
        (struct limine_executable_file_response *)limine_executable_file_request.response;
    if (ef != NULL && ef->executable_file != NULL) {
        limine_executable_file_ptr = (uint64_t)(uintptr_t)ef->executable_file;
    }

    struct limine_executable_cmdline_response *cmd =
        (struct limine_executable_cmdline_response *)limine_executable_cmdline_request.response;
    if (cmd != NULL && cmd->cmdline != NULL) {
        limine_cmdline_addr = (uint64_t)(uintptr_t)cmd->cmdline;
        mbi.cmdline = (uint32_t)(uintptr_t)cmd->cmdline;
        mbi.flags |= MULTIBOOT_FLAG_CMDLINE;
    }

    struct limine_tsc_frequency_response *tsc =
        (struct limine_tsc_frequency_response *)limine_tsc_frequency_request.response;
    if (tsc != NULL) {
        limine_tsc_frequency = tsc->frequency;
    }

    struct limine_hhdm_response *hhdm =
        (struct limine_hhdm_response *)limine_hhdm_request.response;
    if (hhdm != NULL) {
        limine_hhdm_offset = hhdm->offset;
    }

    struct limine_rsdp_response *rsdp =
        (struct limine_rsdp_response *)limine_rsdp_request.response;
    if (rsdp != NULL && rsdp->address != NULL) {
        limine_rsdp_addr = (uint64_t)(uintptr_t)rsdp->address;
        mbi.config_table = (uint32_t)(uintptr_t)rsdp->address;
    }

    struct limine_smbios_response *smbios =
        (struct limine_smbios_response *)limine_smbios_request.response;
    if (smbios != NULL) {
        if (smbios->entry_32 != NULL)
            limine_smbios_addr = (uint64_t)(uintptr_t)smbios->entry_32;
        else if (smbios->entry_64 != NULL)
            limine_smbios_addr = (uint64_t)(uintptr_t)smbios->entry_64;
    }

    struct limine_bootloader_info_response *bli =
        (struct limine_bootloader_info_response *)limine_bootloader_info_request.response;
    if (bli != NULL && bli->name != NULL) {
        limine_bootloader_name = (uint64_t)(uintptr_t)bli->name;
        mbi.boot_loader_name = (uint32_t)(uintptr_t)bli->name;
        mbi.flags |= MULTIBOOT_FLAG_LOADER;
    }
}

void limine_boot_init(void)
{
    for (volatile uint8_t *p = (uint8_t *)&mbi;
         p < (uint8_t *)&mbi + sizeof(mbi); p++) {
        *p = 0;
    }

    bridge_framebuffer();
    bridge_modules();
    bridge_memmap();
    bridge_misc();
}
