#include "../drivers/monitor.h"
#include "elf.h"
#include "../mm/malloc.h"
#include "../lib/common.h"
void *memmove(void *dst, const void *src, unsigned long n);

extern void call_elf32_thunk(unsigned int entry, unsigned int arg1, unsigned int arg2);

static int is_image_valid(void *hdr_ptr)
{
    uint8_t *hdr = (uint8_t*)hdr_ptr;
    ASSERT(hdr[EI_MAG0] == 0x7F);
    ASSERT(hdr[EI_MAG1] == 0x45);
    ASSERT(hdr[EI_MAG2] == 0x4c);
    ASSERT(hdr[EI_MAG3] == 0x46);
    return 1;
}

static int is_elf64(void *hdr_ptr)
{
    uint8_t *hdr = (uint8_t*)hdr_ptr;
    return hdr[EI_CLASS] == ELFCLASS64;
}

static void relocate32(Elf32_Shdr* shdr, const Elf32_Sym* syms, const char* strings, const char* src, char* dst)
{
    Elf32_Rel* rel = (Elf32_Rel*)(src + shdr->sh_offset);

    for(int j = 0; j < shdr->sh_size / sizeof(Elf32_Rel); j += 1)
    {
        const char* sym = strings + syms[ELF32_R_SYM(rel[j].r_info)].st_name;

        switch (ELF32_R_TYPE(rel[j].r_info))
        {
            case R_386_JMP_SLOT:
            case R_386_GLOB_DAT:
                break;
        }
    }
}

static int find_global_symbol_table32(Elf32_Ehdr* hdr, Elf32_Shdr* shdr)
{
    for (int i = 0; i < hdr->e_shnum; i++)
    {
        if (shdr[i].sh_type == SHT_DYNSYM)
            return i;
    }
    return -1;
}

static int find_symbol_table32(Elf32_Ehdr* hdr, Elf32_Shdr* shdr)
{
    for (int i = 0; i < hdr->e_shnum; i++)
    {
        if (shdr[i].sh_type == SHT_SYMTAB)
            return i;
    }
    return -1;
}

static void* find_sym32(const char* name, Elf32_Shdr* shdr, Elf32_Shdr* shdr_sym, const char* src, char* dst)
{
    Elf32_Sym* syms = (Elf32_Sym*)(src + shdr_sym->sh_offset);
    const char* strings = src + shdr[shdr_sym->sh_link].sh_offset;

    for (int i = 0; i < shdr_sym->sh_size / sizeof(Elf32_Sym); i += 1)
    {
        if (strcmp(name, strings + syms[i].st_name) == 0)
            return dst + syms[i].st_value;
    }
    return NULL;
}

static void* image_load32(char *elf_start, unsigned int size)
{
    Elf32_Ehdr      *hdr     = NULL;
    Elf32_Phdr      *phdr    = NULL;
    Elf32_Shdr      *shdr    = NULL;
    char            *start   = NULL;
    char            *taddr   = NULL;
    void            *entry   = NULL;
    int i = 0;
    char *exec = NULL;

    hdr = (Elf32_Ehdr *)elf_start;

    if (!is_image_valid(hdr))
    {
        monitor_write("Invalid ELF image\n");
        return 0;
    }

    exec = (char*)malloc(size);
    if (!exec)
    {
        monitor_write("Error allocating memory\n");
        return 0;
    }

    memset(exec, 0x0, size);

    phdr = (Elf32_Phdr *)(elf_start + hdr->e_phoff);

    for (i = 0; i < hdr->e_phnum; ++i)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        if (phdr[i].p_filesz > phdr[i].p_memsz)
        {
            monitor_write("image_load32:: p_filesz > p_memsz\n");
            free(exec);
            return 0;
        }

        if (!phdr[i].p_filesz)
            continue;

        start = elf_start + phdr[i].p_offset;
        taddr = phdr[i].p_vaddr + exec;
        memmove(taddr, start, phdr[i].p_filesz);
    }

    shdr = (Elf32_Shdr *)(elf_start + hdr->e_shoff);

    int global_symbol_table_index = find_global_symbol_table32(hdr, shdr);
    if (global_symbol_table_index >= 0)
    {
        Elf32_Sym* global_syms = (Elf32_Sym*)(elf_start + shdr[global_symbol_table_index].sh_offset);
        char* global_strings = elf_start + shdr[shdr[global_symbol_table_index].sh_link].sh_offset;

        for (i = 0; i < hdr->e_shnum; ++i)
        {
            if (shdr[i].sh_type == SHT_REL)
                relocate32(shdr + i, global_syms, global_strings, elf_start, exec);
        }
    }

    int symbol_table_index = find_symbol_table32(hdr, shdr);
    if (symbol_table_index >= 0)
        entry = find_sym32("_start", shdr, shdr + symbol_table_index, elf_start, exec);

    return entry;
}

static void* image_load64(char *elf_start, unsigned int size)
{
    Elf64_Ehdr      *hdr     = NULL;
    Elf64_Phdr      *phdr    = NULL;
    char            *start   = NULL;
    char            *taddr   = NULL;
    void            *entry   = NULL;
    int i = 0;
    char *exec = NULL;
    unsigned int alloc_size = size;

    hdr = (Elf64_Ehdr *)elf_start;

    if (!is_image_valid(hdr))
    {
        monitor_write("Invalid ELF image\n");
        return 0;
    }

    phdr = (Elf64_Phdr *)(elf_start + hdr->e_phoff);
    {
        uint64_t max_addr = 0;
        for (i = 0; i < hdr->e_phnum; ++i) {
            if (phdr[i].p_type != PT_LOAD) continue;
            uint64_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            if (end > max_addr) max_addr = end;
        }
        if (max_addr > (uint64_t)alloc_size) alloc_size = (unsigned int)max_addr + 0x10000;
    }

    exec = (char*)malloc(alloc_size + 0x10000);
    if (!exec)
    {
        monitor_write("Error allocating memory\n");
        return 0;
    }

    memset(exec, 0x0, alloc_size + 0x10000);

    for (i = 0; i < hdr->e_phnum; ++i)
    {
        if (phdr[i].p_type != PT_LOAD)
            continue;

        if (phdr[i].p_filesz > phdr[i].p_memsz)
        {
            monitor_write("image_load64:: p_filesz > p_memsz\n");
            free(exec);
            return 0;
        }

        start = elf_start + phdr[i].p_offset;
        taddr = phdr[i].p_vaddr + exec;

        if (phdr[i].p_filesz > 0)
            memmove(taddr, start, phdr[i].p_filesz);

        if (phdr[i].p_memsz > phdr[i].p_filesz)
            memset(taddr + phdr[i].p_filesz, 0x0, phdr[i].p_memsz - phdr[i].p_filesz);
    }

    for (i = 0; i < hdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_DYNAMIC) continue;
        Elf64_Dyn *dyn = (Elf64_Dyn*)(elf_start + phdr[i].p_offset);
        uint64_t rela_off = 0, rela_sz = 0, rela_ent = sizeof(Elf64_Rela);
        const char *strtab = NULL;
        uint64_t symtab_off = 0, symtab_entsz = 0, jmprel_off = 0, jmprel_sz = 0;
        for (int d = 0; dyn[d].d_tag != DT_NULL; d++) {
            switch (dyn[d].d_tag) {
                case DT_RELA:   rela_off = dyn[d].d_val; break;
                case DT_RELASZ: rela_sz  = dyn[d].d_val; break;
                case DT_RELAENT:rela_ent = dyn[d].d_val; break;
                case DT_STRTAB: strtab = exec + dyn[d].d_val; break;
                case DT_SYMTAB: symtab_off = dyn[d].d_val; break;
                case DT_SYMENT: symtab_entsz = dyn[d].d_val; break;
                case DT_JMPREL: jmprel_off = dyn[d].d_val; break;
                case DT_PLTRELSZ: jmprel_sz = dyn[d].d_val; break;
            }
        }
        if (rela_off && rela_sz) {
            uint64_t n = rela_sz / rela_ent;
            for (uint64_t r = 0; r < n; r++) {
                Elf64_Rela *rel = (Elf64_Rela*)(exec + rela_off + r * rela_ent);
                uint32_t type = ELF64_R_TYPE(rel->r_info);
                if (type == R_X86_64_RELATIVE) {
                    uint64_t *slot = (uint64_t*)(exec + rel->r_offset);
                    *slot = (uint64_t)(uintptr_t)exec + rel->r_addend;
                } else if (type == R_X86_64_GLOB_DAT || type == R_X86_64_JUMP_SLOT) {
                    if (strtab && symtab_off && symtab_entsz) {
                        uint32_t sym_idx = (uint32_t)(rel->r_info >> 32);
                        Elf64_Sym *sym = (Elf64_Sym*)(exec + symtab_off + sym_idx * symtab_entsz);
                        uint64_t *slot = (uint64_t*)(exec + rel->r_offset);
                        if (sym->st_value) {
                            *slot = (uint64_t)(uintptr_t)exec + sym->st_value;
                        } else {
                            *slot = (uint64_t)(uintptr_t)exec;
                        }
                    }
                }
            }
        }
        if (jmprel_off && jmprel_sz && rela_ent) {
            uint64_t n = jmprel_sz / rela_ent;
            for (uint64_t r = 0; r < n; r++) {
                Elf64_Rela *rel = (Elf64_Rela*)(exec + jmprel_off + r * rela_ent);
                uint32_t type = ELF64_R_TYPE(rel->r_info);
                uint64_t *slot = (uint64_t*)(exec + rel->r_offset);
                if (type == R_X86_64_JUMP_SLOT) {
                    if (strtab && symtab_off && symtab_entsz) {
                        uint32_t sym_idx = (uint32_t)(rel->r_info >> 32);
                        Elf64_Sym *sym = (Elf64_Sym*)(exec + symtab_off + sym_idx * symtab_entsz);
                        if (sym->st_value) {
                            *slot = (uint64_t)(uintptr_t)exec + sym->st_value;
                        } else {
                            *slot = (uint64_t)(uintptr_t)exec;
                        }
                    }
                }
            }
        }
    }

    entry = (void*)((uint64_t)(uintptr_t)exec + (uint64_t)hdr->e_entry);

    return entry;
}

void* image_load(char *elf_start, unsigned int size)
{
    if (!is_image_valid(elf_start))
    {
        monitor_write("Invalid ELF image\n");
        return 0;
    }

    if (is_elf64(elf_start))
        return image_load64(elf_start, size);
    else
        return image_load32(elf_start, size);
}
