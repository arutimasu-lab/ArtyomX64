OUTPUT_FORMAT(elf64-x86-64)
ENTRY(start)

SECTIONS
{
    . = 0xffffffff80200000;

    .limine_requests_start :
    {
        KEEP(*(.limine_requests_start))
    }

    .limine_requests :
    {
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    }

    .multiboot :
    {
        KEEP(*(.multiboot))
    }

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

    .data :
    {
        *(.data*)
    }

    .bss :
    {
        *(COMMON)
        *(.bss*)
    }

    end = .; _end = .; __end = .;

    .heap :
    {
        _heap_start = .;
        . = . + 64 * 1024 * 1024;
        _heap_end = .;
    }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) }
}
