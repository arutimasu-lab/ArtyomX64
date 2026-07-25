.section .text
.global switchTask

# void switchTask(Registers *old, Registers *new)
switchTask:
    testq %rsi, %rsi
    jz 9f                       # new == NULL: переключаться некуда

    testq %rdi, %rdi
    jz 1f                       # old == NULL: нечего сохранять, сразу восстанавливаем new

    # ---- Сохраняем контекст текущей задачи в *old (%rdi) ----
    movq %r15, 0(%rdi)
    movq %r14, 8(%rdi)
    movq %r13, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r11, 32(%rdi)
    movq %r10, 40(%rdi)
    movq %r9,  48(%rdi)
    movq %r8,  56(%rdi)
    movq %rdi, 64(%rdi)
    movq %rsi, 72(%rdi)
    movq %rbp, 80(%rdi)
    movq %rdx, 88(%rdi)
    movq %rcx, 96(%rdi)
    movq %rbx, 104(%rdi)
    movq %rax, 112(%rdi)

    # EFLAGS
    pushfq
    popq %rax
    movq %rax, 136(%rdi)

    # RIP (адрес возврата) -- лежит на верхушке стека вызывающего switchTask
    movq (%rsp), %rax
    movq %rax, 120(%rdi)

    # RSP (учитываем, что мы сохранили rip)
    leaq 8(%rsp), %rax
    movq %rax, 128(%rdi)

1:
    # ---- Восстанавливаем контекст новой задачи из *new (%rsi) ----
    # ВАЖНО: %rsi используется как база для ВСЕХ чтений ниже и не
    # перезаписывается до самого конца -- иначе следующая же операция
    # через [rsi+xx] читает по неверному (испорченному) адресу.

    movq 144(%rsi), %rax
    testq %rax, %rax
    jz 2f
    movq %rax, %cr3
2:
    # RSP -- переключаемся на стек новой задачи
    movq 128(%rsi), %rsp

    # EFLAGS
    movq 136(%rsi), %rax
    pushq %rax
    popfq

    movq 0(%rsi), %r15
    movq 8(%rsi), %r14
    movq 16(%rsi), %r13
    movq 24(%rsi), %r12
    movq 32(%rsi), %r11
    movq 40(%rsi), %r10
    movq 48(%rsi), %r9
    movq 56(%rsi), %r8
    movq 64(%rsi), %rdi
    movq 80(%rsi), %rbp
    movq 88(%rsi), %rdx
    movq 96(%rsi), %rcx
    movq 104(%rsi), %rbx
    movq 112(%rsi), %rax

    # Кладём точку входа новой задачи на ЕЁ СОБСТВЕННЫЙ (уже
    # переключённый) стек напрямую из памяти -- ни один регистр
    # общего назначения при этом не трогается.
    pushq 120(%rsi)

    # И только теперь, когда %rsi как база больше не нужен,
    # восстанавливаем его собственное значение.
    movq 72(%rsi), %rsi

    # "Прыжок" на RIP новой задачи через ret -- заберёт то, что
    # мы только что положили инструкцией pushq выше.
    ret
9:
    ret
