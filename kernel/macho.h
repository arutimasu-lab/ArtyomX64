#ifndef MACHO_H
#define MACHO_H

#include <stdint.h>

#define MH_MAGIC    0xfeedface
#define MH_MAGIC_64 0xfeedfacf
#define MH_CIGAM    0xcefaedfe
#define MH_CIGAM_64 0xcffaedfe

#define MH_OBJECT    0x1
#define MH_EXECUTE   0x2
#define MH_FVMLIB    0x3
#define MH_CORE      0x4
#define MH_PRELOAD   0x5
#define MH_DYLIB     0x6
#define MH_DYLINKER  0x7
#define MH_BUNDLE    0x8

#define LC_SEGMENT      0x1
#define LC_SEGMENT_64   0x19
#define LC_SYMTAB       0x2
#define LC_UNIXTHREAD   0x5
#define LC_MAIN         0x80000028
#define LC_LOAD_DYLIB   0xc
#define LC_UUID         0x1b

#define CPU_TYPE_X86_64  0x01000007
#define CPU_TYPE_I386    0x00000007

typedef struct {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
} macho_header;

typedef struct {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} macho_header_64;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
} macho_load_command;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
} macho_segment_command;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    int32_t  maxprot;
    int32_t  initprot;
    uint32_t nsects;
    uint32_t flags;
} macho_segment_command_64;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t flavor;
    uint32_t count;
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cs;
    uint64_t fs;
    uint64_t gs;
} macho_thread_state_64;

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
} macho_entry_point_command;

#endif