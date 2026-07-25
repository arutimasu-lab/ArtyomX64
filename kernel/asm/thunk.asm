[bits 64]

section .text
global call_elf32_thunk

call_elf32_thunk:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    xor rax, rax
    mov eax, edi

    test rax, rax
    jz .done

    push rdx
    push rsi
    push rdi

    and rsp, ~0xF

    call rax

    add rsp, 24

.done:
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
