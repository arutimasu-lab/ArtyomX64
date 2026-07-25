[bits 64]

section .limine_requests write progbits alloc
align 8

global limine_base_revision
limine_base_revision:
    dq 0xf9562b2d5c95a6c8
    dq 0x6a7b384944536bdc
    dq 1

global limine_framebuffer_request
limine_framebuffer_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x9d5827dcd881dd75
    dq 0xa3148604f6fab11b
    dq 0
    dq 0

global limine_module_request
limine_module_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x3e7e279702be32af
    dq 0xca1c4f3bd1280cee
    dq 0
    dq 0
    dq 0
    dq 0

global limine_hhdm_request
limine_hhdm_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x48dcf1cb8ad2b852
    dq 0x63984e959a98244b
    dq 0
    dq 0

global limine_memmap_request
limine_memmap_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x67cf3d9d378a806f
    dq 0xe304acdfc50c3c62
    dq 0
    dq 0

global limine_bootloader_info_request
limine_bootloader_info_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0xf55038d8e2a1202f
    dq 0x279426fcf5f59740
    dq 0
    dq 0

global limine_executable_file_request
limine_executable_file_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0xad97e90e83f1ed67
    dq 0x31eb5d1c5ff23b69
    dq 0
    dq 0

global limine_executable_cmdline_request
limine_executable_cmdline_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x4b161536e598651e
    dq 0xb390ad4a2f1f303a
    dq 0
    dq 0

global limine_rsdp_request
limine_rsdp_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0xc5e77b6b397e7b43
    dq 0x27637845accdcf3c
    dq 0
    dq 0

global limine_smbios_request
limine_smbios_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x9e9046f11e095391
    dq 0xaa4a520fefbde5ee
    dq 0
    dq 0

global limine_tsc_frequency_request
limine_tsc_frequency_request:
    dq 0xc7b1dd30df4c8b88
    dq 0x0a82e883a194f07b
    dq 0x10f2ee1d87d195e4
    dq 0xf747a2b78f6ddb31
    dq 0
    dq 0

section .limine_requests_start write progbits alloc
align 8
global limine_requests_start_marker
limine_requests_start_marker:
    dq 0xf6b8f4b39de7d1ae
    dq 0xfab91a6940fcb9cf
    dq 0x785c6ed015d3e316
    dq 0x181e920a7852b9d9

section .limine_requests_end write progbits alloc
align 8
global limine_requests_end_marker
limine_requests_end_marker:
    dq 0xadc0e0531bb10d03
    dq 0x9572709f31764c62

section .bss
align 16
itdo_stack_bottom: resb 262144
itdo_stack_top:

align 16
global mbi
mbi:
    .flags:            resd 1
    .mem_lower:        resd 1
    .mem_upper:        resd 1
    .boot_device:      resd 1
    .cmdline:          resd 1
    .mods_count:       resd 1
    .mods_addr:        resq 1
    .syms:             resd 4
    .mmap_length:      resd 1
    .mmap_addr:        resq 1
    .drives_length:    resd 1
    .drives_addr:      resd 1
    .config_table:     resd 1
    .boot_loader_name: resd 1
    .apm_table:        resd 1
    .vbe_control:      resd 1
    .vbe_mode_info:    resd 1
    .vbe_mode:         resw 1
    .vbe_if_seg:       resw 1
    .vbe_if_off:       resw 1
    .vbe_if_len:       resw 1
    .fb_addr_lo:       resd 1
    .fb_addr_hi:       resd 1
    .fb_pitch:         resd 1
    .fb_width:         resd 1
    .fb_height:        resd 1
    .fb_bpp:           resb 1
    .fb_type:          resb 1
    .fb_color_info:    resb 6

align 16
global itdo_mod_storage
global itdo_mmap_storage
itdo_mod_storage: resb 8192
itdo_mmap_storage: resb 8192

section .data
global framebuffer_addr, framebuffer_width, framebuffer_height, framebuffer_pitch, framebuffer_bpp
global limine_hhdm_offset, limine_rsdp_addr, limine_smbios_addr
global limine_bootloader_name
global limine_cmdline_addr, limine_tsc_frequency
global limine_executable_file_ptr
global limine_loaded_base_revision
framebuffer_addr:   dq 0
framebuffer_width:  dd 0
framebuffer_height: dd 0
framebuffer_pitch:  dd 0
framebuffer_bpp:    dd 0
limine_hhdm_offset:     dq 0
limine_rsdp_addr:       dq 0
limine_smbios_addr:     dq 0
limine_bootloader_name: dq 0
limine_cmdline_addr:        dq 0
limine_tsc_frequency:       dq 0
limine_executable_file_ptr: dq 0
limine_loaded_base_revision: dq 0
splash_msg: db "ArtyomX OS", 0

section .text
global start, _start
extern main
extern limine_boot_init
extern vgafnt

start:
_start:
    cli
    mov rsp, itdo_stack_top
    mov rbp, 0
    cld

    clts
    mov rax, cr0
    and ax, 0xFFF3
    or ax, 0x2
    mov cr0, rax
    fninit

    mov rax, cr4
    or ax, 0x600
    mov cr4, rax

    call limine_boot_init

    mov dx, 0x3F8
    mov al, 'I'
    out dx, al
    mov al, 'N'
    out dx, al
    mov al, 'I'
    out dx, al
    mov al, 'T'
    out dx, al
    mov al, 0x0A
    out dx, al

    call boot_splash

    mov dx, 0x3F8
    mov al, 'S'
    out dx, al
    mov al, 'P'
    out dx, al
    mov al, 'L'
    out dx, al
    mov al, 'S'
    out dx, al
    mov al, 'H'
    out dx, al
    mov al, 0x0A
    out dx, al

    lea rdi, [mbi]
    call main

.hang:
    cli
    hlt
    jmp .hang

boot_splash:
    mov r8,  [framebuffer_addr]
    mov r9d, [framebuffer_width]
    mov r10d,[framebuffer_height]
    mov r11d,[framebuffer_pitch]
    mov r12d,[framebuffer_bpp]
    cmp r8,  0
    je  .ret
    cmp r12d,32
    jne .ret

    xor ecx,ecx
.blue:
    mov rdi,r8
    mov eax,ecx
    imul eax,r11d
    add rdi,rax
    xor edx,edx
.bx:
    mov dword [rdi+rdx*4],0xFF1020A0
    inc edx
    cmp edx,r9d
    jb .bx
    inc ecx
    cmp ecx,r10d
    jb .blue

    mov eax,r9d
    sub eax,400
    shr eax,1
    mov r13d,eax
    mov eax,r10d
    sub eax,160
    shr eax,1
    mov r14d,eax

    xor ecx,ecx
.shadow:
    mov eax,r14d
    add eax,4
    add eax,ecx
    imul eax,r11d
    mov edi,r13d
    shl edi,2
    add edi,4
    add eax,edi
    lea rdi,[r8+rax]
    mov edx,400
.sx:
    mov eax,[rdi]
    shr eax,1
    and eax,0x7F7F7F7F
    mov [rdi],eax
    add rdi,4
    dec edx
    jnz .sx
    inc ecx
    cmp ecx,160
    jb .shadow

    xor ecx,ecx
.white:
    mov eax,r14d
    add eax,ecx
    imul eax,r11d
    mov edi,r13d
    shl edi,2
    add eax,edi
    lea rdi,[r8+rax]
    mov edx,400
.wx:
    mov dword [rdi],0xFFFFFFFF
    add rdi,4
    dec edx
    jnz .wx
    inc ecx
    cmp ecx,160
    jb .white

    xor ecx,ecx
.red:
    mov eax,r14d
    add eax,ecx
    imul eax,r11d
    mov edi,r13d
    shl edi,2
    add eax,edi
    lea rdi,[r8+rax]
    cmp ecx,2
    jb .rf
    cmp ecx,157
    ja .rf
    mov dword [rdi],0xFFFF2020
    mov dword [rdi+4],0xFFFF2020
    mov dword [rdi+1592],0xFFFF2020
    mov dword [rdi+1596],0xFFFF2020
    jmp .rn
.rf:
    mov edx,400
.rx:
    mov dword [rdi],0xFFFF2020
    add rdi,4
    dec edx
    jnz .rx
.rn:
    inc ecx
    cmp ecx,160
    jb .red

    lea r15,[rel vgafnt]
    lea rbx,[rel splash_msg]
    mov eax,r13d
    add eax,160
    mov r12d,eax
    mov eax,r14d
    add eax,72
    mov ebp,eax
.tloop:
    movzx eax,byte [rbx]
    test al,al
    jz .tdone
    cmp al,' '
    je .tskip
    movzx eax,al
    shl eax,4
    lea rsi,[r15+rax]
    xor ecx,ecx
.trow:
    movzx edx,byte [rsi+rcx]
    test edx,edx
    jz .tnext
    mov eax,ebp
    add eax,ecx
    imul eax,r11d
    push rcx
    mov ecx,r12d
    shl ecx,2
    add eax,ecx
    lea rdi,[r8+rax]
    mov ecx,8
.tpix:
    test edx,0x80
    jz .tpsk
    mov dword [rdi],0xFF202020
.tpsk:
    add rdi,4
    shl edx,1
    dec ecx
    jnz .tpix
    pop rcx
.tnext:
    inc ecx
    cmp ecx,16
    jb .trow
.tskip:
    add r12d,9
    inc rbx
    jmp .tloop
.tdone:

    mov eax,r9d
    sub eax,300
    shr eax,1
    shl eax,2
    mov ebx,eax
    mov eax,r10d
    sub eax,28
    imul eax,r11d
    mov r15d,eax
    mov ebp,0
.pbar:
    cmp ebp,8
    jae .bar_done
    mov eax,ebp
    imul eax,300
    shr eax,3
    mov edi,eax
    mov eax,ebp
    inc eax
    imul eax,300
    shr eax,3
    sub eax,edi
    shl edi,2
    mov edx,r15d
    add edx,ebx
    add edx,edi
    mov esi,edx
    mov edx,eax
.py:
    mov edi,esi
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add edi,r11d
    mov dword [rdi+r8],0xFF40FF40
    add esi,4
    dec edx
    jnz .py
    mov ecx,40000
.pd:
    pause
    dec ecx
    jnz .pd
    inc ebp
    jmp .pbar
.bar_done:

    mov ecx,100000
.dout:
    pause
    dec ecx
    jnz .dout

.ret:
    ret

global _end
_end: db 0