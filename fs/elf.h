#include <stdint.h>
#include <stddef.h>
# define ELF_NIDENT	16

#define EI_MAG0		0
#define ELFMAG0		0x7f
#define EI_MAG1		1
#define ELFMAG1		'E'
#define EI_MAG2		2
#define ELFMAG2		'L'
#define EI_MAG3		3
#define ELFMAG3		'F'

#define EI_CLASS	4
#define ELFCLASS32	1
#define ELFCLASS64	2

#define R_386_JMP_SLOT	   7
#define R_386_GLOB_DAT	   6

#define R_X86_64_NONE      0
#define R_X86_64_64        1
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

#define PT_DYNAMIC  2
#define DT_NULL     0
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_SYMENT   11
#define DT_JMPREL   23
#define DT_PLTRELSZ 2

typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

typedef struct {
	uint8_t		e_ident[ELF_NIDENT];
	Elf32_Half	e_type;
	Elf32_Half	e_machine;
	Elf32_Word	e_version;
	Elf32_Addr	e_entry;
	Elf32_Off	e_phoff;
	Elf32_Off	e_shoff;
	Elf32_Word	e_flags;
	Elf32_Half	e_ehsize;
	Elf32_Half	e_phentsize;
	Elf32_Half	e_phnum;
	Elf32_Half	e_shentsize;
	Elf32_Half	e_shnum;
	Elf32_Half	e_shstrndx;
} Elf32_Ehdr;

#define PT_LOAD		1
typedef struct
{
  Elf32_Word	p_type;
  Elf32_Off		p_offset;
  Elf32_Addr	p_vaddr;
  Elf32_Addr	p_paddr;
  Elf32_Word	p_filesz;
  Elf32_Word	p_memsz;
  Elf32_Word	p_flags;
  Elf32_Word	p_align;
} Elf32_Phdr;

#define SHT_SYMTAB	  2
#define SHT_DYNSYM	  11
#define SHT_REL		  9
#define SHT_RELA         4    /* ← Добавить эту строку */
typedef struct
{
  Elf32_Word	sh_name;
  Elf32_Word	sh_type;
  Elf32_Word	sh_flags;
  Elf32_Addr	sh_addr;
  Elf32_Off	sh_offset;
  Elf32_Word	sh_size;
  Elf32_Word	sh_link;
  Elf32_Word	sh_info;
  Elf32_Word	sh_addralign;
  Elf32_Word	sh_entsize;
} Elf32_Shdr;
typedef struct {
	Elf32_Word		st_name;
	Elf32_Addr		st_value;
	Elf32_Word		st_size;
	uint8_t			st_info;
	uint8_t			st_other;
	Elf32_Half		st_shndx;
} Elf32_Sym;

#define ELF32_R_SYM(val)		((val) >> 8)
#define ELF32_R_TYPE(val)		((val) & 0xff)
typedef struct {
	Elf32_Addr		r_offset;
	Elf32_Word		r_info;
} Elf32_Rel;

typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Off;
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

typedef struct {
	uint8_t     e_ident[ELF_NIDENT];
	Elf64_Half  e_type;
	Elf64_Half  e_machine;
	Elf64_Word  e_version;
	Elf64_Addr  e_entry;
	Elf64_Off   e_phoff;
	Elf64_Off   e_shoff;
	Elf64_Word  e_flags;
	Elf64_Half  e_ehsize;
	Elf64_Half  e_phentsize;
	Elf64_Half  e_phnum;
	Elf64_Half  e_shentsize;
	Elf64_Half  e_shnum;
	Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	Elf64_Word  p_type;
	Elf64_Word  p_flags;
	Elf64_Off   p_offset;
	Elf64_Addr  p_vaddr;
	Elf64_Addr  p_paddr;
	Elf64_Xword p_filesz;
	Elf64_Xword p_memsz;
	Elf64_Xword p_align;
} Elf64_Phdr;

typedef struct {
	Elf64_Word    sh_name;
	Elf64_Word    sh_type;
	Elf64_Xword   sh_flags;
	Elf64_Addr    sh_addr;
	Elf64_Off     sh_offset;
	Elf64_Xword   sh_size;
	Elf64_Word    sh_link;
	Elf64_Word    sh_info;
	Elf64_Xword   sh_addralign;
	Elf64_Xword   sh_entsize;
} Elf64_Shdr;

typedef struct {
	Elf64_Word    st_name;
	uint8_t       st_info;
	uint8_t       st_other;
	Elf64_Half    st_shndx;
	Elf64_Addr    st_value;
	Elf64_Xword   st_size;
} Elf64_Sym;

#define ELF64_R_SYM(val)		((val) >> 32)
#define ELF64_R_TYPE(val)		((val) & 0xffffffff)

typedef struct {
	Elf64_Addr    r_offset;
	Elf64_Xword   r_info;
	Elf64_Sxword  r_addend;
} Elf64_Rela;

typedef struct {
	Elf64_Sxword  d_tag;
	Elf64_Xword   d_val;
} Elf64_Dyn;
