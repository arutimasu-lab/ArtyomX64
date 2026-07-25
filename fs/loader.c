#include "../drivers/monitor.h"
#include "elf.h"
//#include "../mm/malloc.h"
#include "../lib/common.h"
#include "../mm/kheap.h"
#define malloc kmalloc
#define free kfree
extern void serial_puts_ax(const char *s);
extern void serial_putc_ax(char c);
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

// Добавьте эти структуры и определения в начало файла или в elf.h

// Определения для 64-битных релокаций
#define R_X86_64_NONE       0
#define R_X86_64_64         1
#define R_X86_64_PC32       2
#define R_X86_64_GOT32      3
#define R_X86_64_PLT32      4
#define R_X86_64_COPY       5
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7
#define R_X86_64_RELATIVE   8
#define R_X86_64_GOTPCREL   9
#define R_X86_64_32         10
#define R_X86_64_32S        11
#define R_X86_64_16         12
#define R_X86_64_PC16       13
#define R_X86_64_8          14
#define R_X86_64_PC8        15
#define R_X86_64_PC64       24
#define R_X86_64_GOTOFF64   25
#define R_X86_64_GOTPC32    26
#define R_X86_64_SIZE32     32
#define R_X86_64_SIZE64     33

#define ELF64_R_SYM(i)      ((i) >> 32)
#define ELF64_R_TYPE(i)     ((i) & 0xffffffffL)
#define ELF64_R_INFO(s,t)   (((s) << 32) + ((t) & 0xffffffffL))

// Структура для 64-битных релокаций
/*typedef struct {
    uint64_t r_offset;    // Адрес, куда применить релокацию
    uint64_t r_info;      // Тип релокации и индекс символа
    int64_t  r_addend;    // Явное добавление (для RELA)
} Elf64_Rela;
*/
void print_hex(uint64_t val) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[19];  // "0x" + 16 hex digits + null
    int i = 18;
    
    buffer[18] = '\0';
    
    do {
        buffer[--i] = hex_chars[val & 0xF];
        val >>= 4;
    } while (val != 0 && i > 2);
    
    buffer[--i] = 'x';
    buffer[--i] = '0';
    
    while (i > 0) {
        buffer[--i] = ' ';
    }
    
    serial_puts_ax(buffer + i);
}
// Функция для поиска секции по имени
static int find_section64(Elf64_Ehdr* hdr, Elf64_Shdr* shdr, const char* name, char* elf_start)
{
    // Находим секцию с именами секций
    Elf64_Shdr* shstrtab = &shdr[hdr->e_shstrndx];
    const char* shstr = elf_start + shstrtab->sh_offset;
    
    for (int i = 0; i < hdr->e_shnum; i++) {
        if (strcmp(name, shstr + shdr[i].sh_name) == 0) {
            return i;
        }
    }
    return -1;
}

// Функция для обработки 64-битных релокаций
static void relocate64(Elf64_Shdr* rel_sec, Elf64_Sym* syms, const char* strings, 
                       char* elf_start, char* exec, uint64_t load_addr)
{
    Elf64_Rela* rel = (Elf64_Rela*)(elf_start + rel_sec->sh_offset);
    int num_relocs = rel_sec->sh_size / sizeof(Elf64_Rela);
    
    for (int j = 0; j < num_relocs; j++) {
        uint64_t* target = (uint64_t*)(exec + (rel[j].r_offset - load_addr));
        uint64_t sym_idx = ELF64_R_SYM(rel[j].r_info);
        uint64_t type = ELF64_R_TYPE(rel[j].r_info);
        
        // Символ, если есть
        Elf64_Sym* sym = NULL;
        const char* sym_name = NULL;
        uint64_t sym_value = 0;
        
        if (sym_idx != 0 && syms != NULL) {
            sym = &syms[sym_idx];
            if (strings != NULL) {
                sym_name = strings + sym->st_name;
            }
            sym_value = sym->st_value;
        }
        
        uint64_t P = (uint64_t)(exec + (rel[j].r_offset - load_addr));
        uint64_t A = rel[j].r_addend;
        uint64_t value = sym_value + A;
        
        switch (type) {
            case R_X86_64_NONE:
                break;
                
            case R_X86_64_64:
                // Абсолютная 64-битная релокация
                if (sym_value == 0) {
                    // Символ не определен - используем addend
                    *target = A;
                } else {
                    *target = value;
                }
                break;
                
            case R_X86_64_RELATIVE:
                // Относительно базового адреса загрузки
                //*target = load_addr + A;
                  *target =  (uint64_t)exec + A;
                break;
                
            case R_X86_64_32:
                // 32-битная абсолютная
                *(uint32_t*)target = (uint32_t)(value);
                break;
                
            case R_X86_64_32S:
                // 32-битная знаковая абсолютная
                *(int32_t*)target = (int32_t)(value);
                break;
                
            case R_X86_64_PC32:
                // 32-битная относительно PC
                *(int32_t*)target = (int32_t)(value - P);
                break;
                
            case R_X86_64_PC64:
                // 64-битная относительно PC
                *target = value - P;
                break;
                
            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
                // Для динамического связывания - пока просто копируем значение
                if (sym_value != 0) {
                    *target = sym_value;
                } else {
                    *target = A;
                }
                break;
                
            default:
                serial_puts_ax("Unsupported relocation type: ");
                print_hex(type);
                serial_putc_ax('\n');
                break;
        }
    }
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
    uint64_t load_addr = 0;
    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFFULL;
    uint64_t max_vaddr = 0;

    hdr = (Elf64_Ehdr *)elf_start;

    if (!is_image_valid(hdr))
    {
        monitor_write("Invalid ELF image\n");
        return 0;
    }

    // Находим виртуальные адреса
    phdr = (Elf64_Phdr *)(elf_start + hdr->e_phoff);
    for (i = 0; i < hdr->e_phnum; ++i) {
        if (phdr[i].p_type == PT_LOAD) {
            if (phdr[i].p_vaddr < min_vaddr) {
                min_vaddr = phdr[i].p_vaddr;
            }
            if (phdr[i].p_vaddr + phdr[i].p_memsz > max_vaddr) {
                max_vaddr = phdr[i].p_vaddr + phdr[i].p_memsz;
            }
        }
    }
    
    load_addr = min_vaddr;
    uint64_t mem_size = max_vaddr - min_vaddr;
    
    // Выравниваем
    load_addr = load_addr & ~0xFFFULL;
    mem_size = (mem_size + 0xFFF) & ~0xFFFULL;
    
    serial_puts_ax("LOAD_ADDR: ");
    print_hex(load_addr);
    serial_putc_ax('\n');
    
    serial_puts_ax("MEM_SIZE: ");
    print_hex(mem_size);
    serial_putc_ax('\n');

    // Выделяем буфер
    exec = (char*)malloc(mem_size + 0x1000);
    if (!exec)
    {
        monitor_write("Error allocating memory\n");
        return 0;
    }

    memset(exec, 0x0, mem_size + 0x1000);

    serial_puts_ax("BUFFER at: ");
    print_hex((uint64_t)exec);
    serial_putc_ax('\n');

    // Загружаем сегменты в буфер
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
        uint64_t offset = phdr[i].p_vaddr - load_addr;
        taddr = exec + offset;

        if (phdr[i].p_filesz > 0)
            memmove(taddr, start, phdr[i].p_filesz);

        if (phdr[i].p_memsz > phdr[i].p_filesz)
            memset(taddr + phdr[i].p_filesz, 0x0, phdr[i].p_memsz - phdr[i].p_filesz);
    }
     // ===== ОБРАБОТКА РЕЛОКАЦИЙ =====
    Elf64_Shdr* shdr = (Elf64_Shdr*)(elf_start + hdr->e_shoff);
    
    // Находим таблицу символов
    int symtab_idx = -1;
    int dynsym_idx = -1;
    
    for (i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_SYMTAB) {
            symtab_idx = i;
        }
        if (shdr[i].sh_type == SHT_DYNSYM) {
            dynsym_idx = i;
        }
    }
    
    // Обрабатываем RELA секции
    for (i = 0; i < hdr->e_shnum; i++) {
        if (shdr[i].sh_type == SHT_RELA) {
            // Определяем, какую таблицу символов использовать
            Elf64_Sym* syms = NULL;
            const char* strings = NULL;
            
            if (dynsym_idx >= 0 && shdr[i].sh_link == dynsym_idx) {
                syms = (Elf64_Sym*)(elf_start + shdr[dynsym_idx].sh_offset);
                strings = elf_start + shdr[shdr[dynsym_idx].sh_link].sh_offset;
            } else if (symtab_idx >= 0 && shdr[i].sh_link == symtab_idx) {
                syms = (Elf64_Sym*)(elf_start + shdr[symtab_idx].sh_offset);
                strings = elf_start + shdr[shdr[symtab_idx].sh_link].sh_offset;
            }
            
            serial_puts_ax("Relocating section: ");
            print_hex((uint64_t)(elf_start + shdr[shdr[i].sh_name].sh_offset));
            serial_putc_ax('\n');
            
            relocate64(&shdr[i], syms, strings, elf_start, exec, load_addr);
        }
    }
    
    serial_puts_ax("RELOCATIONS_DONE\n");
    
    
    // НЕ КОПИРУЕМ по 0x400000 - используем буфер напрямую
    // Точка входа = начало буфера + смещение
    uint64_t entry_offset = hdr->e_entry - load_addr;
    entry = (void*)(exec + entry_offset);
    
    serial_puts_ax("ENTRY_OFFSET: ");
    print_hex(entry_offset);
    serial_putc_ax('\n');
    
    serial_puts_ax("FINAL_ENTRY: ");
    print_hex((uint64_t)entry);
    serial_putc_ax('\n');

    // НЕ освобождаем буфер!
    return entry;
}
// В loader.c - добавьте отладочный вывод

void* image_load(char *elf_start, unsigned int size)
{
    serial_puts_ax("IMAGE_LOAD: size=");
    char buf[16];
    int n = 0;
    unsigned int x = size;
    char t[16];
    if (x == 0) t[n++] = '0';
    while (x) { t[n++] = '0' + (x % 10); x /= 10; }
    while (n) serial_putc_ax(t[--n]);
    serial_putc_ax('\n');
    
    if (!is_image_valid(elf_start)) {
        serial_puts_ax("INVALID_ELF\n");
        return 0;
    }

    void *entry;
    if (is_elf64(elf_start)) {
        serial_puts_ax("ELF64\n");
        entry = image_load64(elf_start, size);
    } else {
        serial_puts_ax("ELF32\n");
        entry = image_load32(elf_start, size);
    }
    
    if (entry) {
        serial_puts_ax("FINAL_ENTRY: ");
        uint64_t addr = (uint64_t)entry;
        char t[16];
        int n = 0;
        if (addr == 0) t[n++] = '0';
        while (addr) { t[n++] = '0' + (addr % 10); addr /= 10; }
        while (n) serial_putc_ax(t[--n]);
        serial_putc_ax('\n');
    }
    
    return entry;
}
