[bits 64]

section .text
global call_compat64_thunk

call_compat64_thunk:
    push rbp
    mov rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov r12, rdi
    mov r13, rsi

    mov rax, rsp
    and rax, ~0xF
    mov rsp, rax

    call r12

    mov rsp, rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

global compat_jmp64

compat_jmp64:
    jmp rdi
