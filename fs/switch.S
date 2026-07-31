.section .text
.global switchTask
.type switchTask, @function
switchTask:
    movq %rbx, 0(%rdi)
    movq %r12, 72(%rdi)
    movq %r13, 80(%rdi)
    movq %r14, 88(%rdi)
    movq %r15, 96(%rdi)
    movq %rbp, 104(%rdi)

    pushfq
    popq 120(%rdi)

    movq %cr3, %rax
    movq %rax, 112(%rdi)

    movq (%rsp), %rax
    movq %rax, 136(%rdi)
    leaq 8(%rsp), %rax
    movq %rax, 128(%rdi)

    movq 136(%rsi), %rax
    movq 128(%rsi), %rsp

    movq 112(%rsi), %rdx
    testq %rdx, %rdx
    jz 1f
    movq %rdx, %cr3
1:
    pushq 120(%rsi)
    popfq

    movq 0(%rsi), %rbx
    movq 72(%rsi), %r12
    movq 80(%rsi), %r13
    movq 88(%rsi), %r14
    movq 96(%rsi), %r15
    movq 104(%rsi), %rbp

    jmp *%rax
.size switchTask, .-switchTask

.global task_user_entry
.type task_user_entry, @function
task_user_entry:
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    iretq
.size task_user_entry, .-task_user_entry
