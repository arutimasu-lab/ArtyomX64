[bits 32]

section .multiboot
align 4
multiboot_header:
    dd 0x1BADB002
    dd 0x00000007
    dd -(0x1BADB002 + 0x00000007)
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

align 4096
pml4_table:
    resb 4096
pdpt_table:
    resb 4096
pd_tables:
    resb 4096 * 4

section .rodata
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1<<41) | (1<<43) | (1<<44) | (1<<47) | (1<<53)
.data: equ $ - gdt64
    dq (1<<41) | (1<<44) | (1<<47)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .data
global framebuffer_addr
global framebuffer_width
global framebuffer_height
global framebuffer_pitch
global framebuffer_bpp

framebuffer_addr:   dq 0
framebuffer_width:  dd 0
framebuffer_height: dd 0
framebuffer_pitch:  dd 0
framebuffer_bpp:    dd 0

section .text
global start
extern main

start:
    mov esp, stack_top
    cli

    cmp eax, 0x2BADB002
    jne no_multiboot_error

    mov edi, ebx

    push edi
    call extract_framebuffer_info
    add esp, 4

    mov eax, pdpt_table
    or eax, 0b11
    mov [pml4_table], eax
    mov dword [pml4_table + 4], 0

    mov ecx, 0
.link_pd:
    mov eax, ecx
    shl eax, 12
    add eax, pd_tables
    or eax, 0b11
    mov [pdpt_table + ecx * 8], eax
    mov dword [pdpt_table + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 4
    jne .link_pd

    mov ecx, 0
.map_2mb:
    mov eax, ecx
    shl eax, 21
    or eax, 0b10000011
    mov [pd_tables + ecx * 8], eax
    mov dword [pd_tables + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 2048
    jne .map_2mb

    mov eax, pml4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64.pointer]

    jmp gdt64.code:long_mode_start

no_multiboot_error:
    mov dword [0xB8000], 0x4F3F4D00
    hlt
    jmp no_multiboot_error


[bits 64]
long_mode_start:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    mov rdi, rdi
    call main

.halt:
    hlt
    jmp .halt

extract_framebuffer_info:
    mov ebx, [esp + 4]

    mov eax, [ebx]
    test eax, 0x800
    jz .no_fb

    mov eax, [ebx + 88]
    mov dword [framebuffer_addr], eax
    mov eax, [ebx + 92]
    mov dword [framebuffer_addr + 4], eax

    mov eax, [ebx + 96]
    mov [framebuffer_pitch], eax

    mov eax, [ebx + 100]
    mov [framebuffer_width], eax

    mov eax, [ebx + 104]
    mov [framebuffer_height], eax

    movzx eax, byte [ebx + 108]
    mov [framebuffer_bpp], eax
    ret

.no_fb:
    mov dword [framebuffer_addr], 0xB8000
    mov dword [framebuffer_addr + 4], 0
    ret
