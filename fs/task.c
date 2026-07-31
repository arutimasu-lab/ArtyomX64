#include "task.h"
#include "../mm/malloc.h"
#include "../mm/pmm.h"
#include "../mm/vmm.h"
#include "../drivers/timer.h"
#include <stddef.h>

#define STACK_SIZE (64u * 1024u)
#define RFLAGS_IF (1ull << 9)

static Task tasks[TASK_MAX];
static uint32_t task_count;
static uint32_t current_task;
static uint32_t tasking_initialized;
static void *reap_stacks[TASK_MAX];
static uint32_t reap_count;
static volatile uint32_t task_lock;
static int next_pid = 1;
static volatile uint32_t need_resched;

extern void switchTask(Registers *prev, Registers *next);

static void spin_lock(volatile uint32_t *lock)
{
    while (__sync_lock_test_and_set(lock, 1u))
        __asm__ volatile("pause" ::: "memory");
}

static void spin_unlock(volatile uint32_t *lock)
{
    __sync_lock_release(lock);
}

static void task_capture_current(Registers *regs)
{
    __asm__ volatile("mov %%rsp, %0" : "=m"(regs->rsp));
    __asm__ volatile("lea 1f(%%rip), %%rax; mov %%rax, %0\n1:" : "=m"(regs->rip) : : "rax", "memory");
    __asm__ volatile("pushfq; pop %0" : "=m"(regs->rflags));
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %0" : "=m"(regs->cr3) : : "rax", "memory");
}

static uint32_t task_next_active(uint32_t from)
{
    uint32_t offset;

    for (offset = 1u; offset < TASK_MAX; offset++) {
        uint32_t candidate = (from + offset) % TASK_MAX;
        if (tasks[candidate].active && tasks[candidate].state != TASK_STATE_SLEEPING)
            return candidate;
    }

    return from;
}

static void task_reap_stacks(void)
{
    void *stacks[TASK_MAX];
    uint32_t count = 0u;

    spin_lock(&task_lock);
    while (count < reap_count) {
        stacks[count] = reap_stacks[count];
        reap_stacks[count] = NULL;
        count++;
    }
    reap_count = 0u;
    spin_unlock(&task_lock);

    for (uint32_t i = 0u; i < count; i++)
        free(stacks[i]);
}

static void task_entry_trampoline(void)
{
    uint32_t slot = current_task;
    void (*entry)(void) = tasks[slot].entry;

    if (entry)
        entry();
    task_exit();
}

void initTasking(void)
{
    Task *boot_task;

    if (tasking_initialized)
        return;

    spin_lock(&task_lock);
    if (tasking_initialized) {
        spin_unlock(&task_lock);
        return;
    }

    for (uint32_t i = 0u; i < TASK_MAX; i++) {
        tasks[i].active = 0u;
        tasks[i].stack_base = NULL;
        tasks[i].entry = NULL;
        tasks[i].generation = 0u;
        tasks[i].state = TASK_STATE_FREE;
        tasks[i].vas = NULL;
        tasks[i].is_user = false;
        tasks[i].pid = 0;
        tasks[i].ppid = 0;
        reap_stacks[i] = NULL;
    }
    reap_count = 0u;

    boot_task = &tasks[0];
    task_capture_current(&boot_task->regs);
    boot_task->active = 1u;
    boot_task->generation = 1u;
    boot_task->state = TASK_STATE_RUNNING;
    boot_task->pid = next_pid++;
    boot_task->ppid = 0;
    boot_task->tgid = boot_task->pid;
    boot_task->tid = boot_task->pid;
    boot_task->is_user = false;
    boot_task->vas = NULL;
    boot_task->signal_pending = 0;
    boot_task->signal_blocked = 0;
    memset(boot_task->signal_handlers, 0, sizeof(boot_task->signal_handlers));
    boot_task->name[0] = 'k'; boot_task->name[1] = 'e'; boot_task->name[2] = 'r';
    boot_task->name[3] = 'n'; boot_task->name[4] = 'e'; boot_task->name[5] = 'l';
    boot_task->name[6] = 0;
    task_count = 1u;
    current_task = 0u;
    tasking_initialized = 1u;
    need_resched = 0u;
    spin_unlock(&task_lock);
}

int createTask(Task *task, void (*entry)(void), uint64_t flags, uint64_t *pagedir)
{
    uintptr_t stack_top;
    void *stack;

    if (!task || !entry)
        return -1;

    stack = malloc(STACK_SIZE);
    if (!stack)
        return -2;

    stack_top = (((uintptr_t)stack + STACK_SIZE) & ~(uintptr_t)0xFul) - sizeof(uint64_t);
    *(uint64_t *)stack_top = 0u;
    task->regs.rbx = 0u;
    task->regs.rcx = 0u;
    task->regs.rdx = 0u;
    task->regs.rsi = 0u;
    task->regs.rdi = 0u;
    task->regs.r8 = 0u;
    task->regs.r9 = 0u;
    task->regs.r10 = 0u;
    task->regs.r11 = 0u;
    task->regs.r12 = 0u;
    task->regs.r13 = 0u;
    task->regs.r14 = 0u;
    task->regs.r15 = 0u;
    task->regs.rbp = 0u;
    task->regs.cr3 = (uint64_t)(uintptr_t)pagedir;
    task->regs.rflags = (flags | RFLAGS_IF) & ~(3ull << 12);
    task->regs.rsp = (uint64_t)stack_top;
    task->regs.rip = (uint64_t)(uintptr_t)task_entry_trampoline;
    task->stack_base = stack;
    task->entry = entry;
    task->active = 1u;
    task->state = TASK_STATE_READY;
    task->generation++;
    return 0;
}

int task_spawn(void (*entry)(void))
{
    uint32_t slot;
    int result;

    if (!entry)
        return -1;
    if (!tasking_initialized)
        initTasking();
    task_reap_stacks();

    spin_lock(&task_lock);
    if (task_count >= TASK_MAX) {
        spin_unlock(&task_lock);
        return -2;
    }

    for (slot = 1u; slot < TASK_MAX; slot++) {
        if (!tasks[slot].active)
            break;
    }
    if (slot == TASK_MAX) {
        spin_unlock(&task_lock);
        return -3;
    }

    result = createTask(&tasks[slot], entry, tasks[current_task].regs.rflags,
                        (uint64_t *)(uintptr_t)tasks[current_task].regs.cr3);
    if (result == 0) {
        task_count++;
        tasks[slot].pid = next_pid++;
        tasks[slot].ppid = tasks[current_task].pid;
        tasks[slot].is_user = false;
        tasks[slot].vas = tasks[current_task].vas;
    }
    spin_unlock(&task_lock);
    return result == 0 ? tasks[slot].pid : result;
}

extern void task_user_entry(void);

int task_spawn_user(const char *name, uint64_t entry, uint64_t user_stack_top, vas_t *as)
{
    uint32_t slot;
    void *kstack;
    uintptr_t kstack_top;

    if (!entry || !as)
        return -1;
    if (!tasking_initialized)
        initTasking();
    task_reap_stacks();

    spin_lock(&task_lock);
    if (task_count >= TASK_MAX) {
        spin_unlock(&task_lock);
        return -2;
    }
    for (slot = 1u; slot < TASK_MAX; slot++) {
        if (!tasks[slot].active)
            break;
    }
    if (slot == TASK_MAX) {
        spin_unlock(&task_lock);
        return -3;
    }
    task_count++;
    spin_unlock(&task_lock);

    kstack = malloc(TASK_KSTACK_SIZE);
    if (!kstack) {
        spin_lock(&task_lock);
        task_count--;
        spin_unlock(&task_lock);
        return -4;
    }
    kstack_top = (((uintptr_t)kstack + TASK_KSTACK_SIZE) & ~(uintptr_t)0xFul);

    Task *t = &tasks[slot];
    memset(t, 0, sizeof(*t));
    t->stack_base = kstack;
    t->active = 1u;
    t->state = TASK_STATE_READY;
    t->is_user = true;
    t->vas = as;
    t->pid = next_pid++;
    t->ppid = tasks[current_task].pid;
    t->tgid = t->pid;
    t->tid = t->pid;
    t->generation = 1u;
    t->exit_code = 0;
    t->signal_pending = 0;
    t->signal_blocked = 0;
    memset(t->signal_handlers, 0, sizeof(t->signal_handlers));

    if (name) {
        int k = 0;
        while (name[k] && k < 31) { t->name[k] = name[k]; k++; }
        t->name[k] = 0;
    } else {
        t->name[0] = 'u'; t->name[1] = 's'; t->name[2] = 'e'; t->name[3] = 'r'; t->name[4] = 0;
    }

    t->regs.rbx = 0;
    t->regs.rcx = 0;
    t->regs.rdx = 0;
    t->regs.rsi = 0;
    t->regs.rdi = 0;
    t->regs.r8 = 0;
    t->regs.r9 = 0;
    t->regs.r10 = 0;
    t->regs.r11 = 0;
    t->regs.r12 = 0;
    t->regs.r13 = 0;
    t->regs.r14 = 0;
    t->regs.r15 = 0;
    t->regs.rbp = 0;
    t->regs.rflags = RFLAGS_IF | 0x2;
    t->regs.cr3 = vmm_virt_to_phys_hhdm((uint64_t)(uintptr_t)as->pml4);

    uintptr_t sp = kstack_top;
    sp -= 8; *(uint64_t*)sp = 0x23;
    sp -= 8; *(uint64_t*)sp = user_stack_top;
    sp -= 8; *(uint64_t*)sp = RFLAGS_IF | 0x2;
    sp -= 8; *(uint64_t*)sp = 0x1B;
    sp -= 8; *(uint64_t*)sp = entry;
    t->regs.rsp = (uint64_t)sp;
    t->regs.rip = (uint64_t)(uintptr_t)task_user_entry;

    return t->pid;
}

void yield(void)
{
    uint32_t previous;
    uint32_t next;

    if (!tasking_initialized || task_count < 2u)
        return;

    task_reap_stacks();
    spin_lock(&task_lock);
    previous = current_task;
    next = task_next_active(previous);
    if (next == previous) {
        spin_unlock(&task_lock);
        return;
    }
    if (tasks[previous].state == TASK_STATE_RUNNING)
        tasks[previous].state = TASK_STATE_READY;
    tasks[next].state = TASK_STATE_RUNNING;
    current_task = next;
    spin_unlock(&task_lock);
    if (tasks[next].is_user && tasks[next].tls_base) {
        uint64_t base = tasks[next].tls_base;
        uint32_t lo = (uint32_t)(base & 0xFFFFFFFFull);
        uint32_t hi = (uint32_t)(base >> 32);
        __asm__ volatile("wrmsr" :: "c"(0xC0000100u), "a"(lo), "d"(hi));
    }
    switchTask(&tasks[previous].regs, &tasks[next].regs);
}

static void task_do_exit(int code)
{
    uint32_t previous;
    uint32_t next;

    spin_lock(&task_lock);
    previous = current_task;
    if (previous == 0u) {
        spin_unlock(&task_lock);
        for (;;) {
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }

    tasks[previous].active = 0u;
    tasks[previous].state = TASK_STATE_ZOMBIE;
    tasks[previous].exit_code = code;
    tasks[previous].entry = NULL;
    if (tasks[previous].stack_base && reap_count < TASK_MAX) {
        reap_stacks[reap_count++] = tasks[previous].stack_base;
        tasks[previous].stack_base = NULL;
    }
    if (task_count > 1u)
        task_count--;
    next = task_next_active(previous);
    if (next == previous) {
        for (uint32_t i = 0; i < TASK_MAX; i++) {
            if (i != previous && tasks[i].active && tasks[i].state != TASK_STATE_SLEEPING) {
                next = i;
                break;
            }
        }
        if (next == previous) next = 0;
    }
    tasks[next].state = TASK_STATE_RUNNING;
    current_task = next;
    spin_unlock(&task_lock);
    switchTask(&tasks[previous].regs, &tasks[next].regs);
    __builtin_unreachable();
}

void task_exit(void)
{
    task_do_exit(0);
}

void task_exit_code(int code)
{
    task_do_exit(code);
}

Task *task_current(void)
{
    return &tasks[current_task];
}

Task *task_by_pid(int pid)
{
    for (uint32_t i = 0; i < TASK_MAX; i++)
        if (tasks[i].active && tasks[i].pid == pid)
            return &tasks[i];
    return NULL;
}

int task_current_pid(void)
{
    return tasks[current_task].pid;
}

vas_t *task_current_vas(void)
{
    return tasks[current_task].vas;
}

void task_sleep_ticks(uint32_t ticks)
{
    if (!tasking_initialized || task_count < 2u) {
        uint32_t start = tick;
        while ((uint32_t)(tick - start) < ticks)
            __asm__ volatile("hlt" ::: "memory");
        return;
    }
    spin_lock(&task_lock);
    tasks[current_task].state = TASK_STATE_SLEEPING;
    tasks[current_task].wake_tick = tick + ticks;
    spin_unlock(&task_lock);
    yield();
}

int task_kill(int pid)
{
    Task *t = task_by_pid(pid);
    if (!t || t == &tasks[0])
        return -1;
    spin_lock(&task_lock);
    t->state = TASK_STATE_ZOMBIE;
    t->active = 0u;
    if (t->stack_base && reap_count < TASK_MAX) {
        reap_stacks[reap_count++] = t->stack_base;
        t->stack_base = NULL;
    }
    if (t->vas) {
        vmm_destroy_address_space(t->vas);
        t->vas = NULL;
    }
    if (task_count > 1u)
        task_count--;
    spin_unlock(&task_lock);
    return 0;
}

void task_set_name(int pid, const char *name)
{
    Task *t = task_by_pid(pid);
    if (!t || !name) return;
    int k = 0;
    while (name[k] && k < 31) { t->name[k] = name[k]; k++; }
    t->name[k] = 0;
}

void task_set_tls(uint64_t base)
{
    tasks[current_task].tls_base = base;
    uint32_t lo = (uint32_t)(base & 0xFFFFFFFFull);
    uint32_t hi = (uint32_t)(base >> 32);
    __asm__ volatile("wrmsr" :: "c"(0xC0000100u), "a"(lo), "d"(hi));
}

uint64_t task_get_tls(void)
{
    return tasks[current_task].tls_base;
}

int task_clone_thread(uint64_t flags, uint64_t stack, uint64_t tls, int *ctid)
{
    Task *parent = &tasks[current_task];
    if (!parent->is_user) return -1;

    spin_lock(&task_lock);
    if (task_count >= TASK_MAX) { spin_unlock(&task_lock); return -11; }
    uint32_t slot;
    for (slot = 1u; slot < TASK_MAX; slot++) {
        if (!tasks[slot].active) break;
    }
    if (slot == TASK_MAX) { spin_unlock(&task_lock); return -11; }
    task_count++;
    spin_unlock(&task_lock);

    void *kstack = malloc(TASK_KSTACK_SIZE);
    if (!kstack) {
        spin_lock(&task_lock);
        task_count--;
        spin_unlock(&task_lock);
        return -12;
    }
    uintptr_t kstack_top = (((uintptr_t)kstack + TASK_KSTACK_SIZE) & ~(uintptr_t)0xFul);

    Task *child = &tasks[slot];
    memcpy((u8int*)child, (u8int*)parent, sizeof(Task));
    child->stack_base = kstack;
    child->vas = parent->vas;
    child->vas->refcount++;
    child->pid = next_pid++;
    child->ppid = parent->pid;
    child->tgid = parent->tgid;
    child->tid = child->pid;
    child->state = TASK_STATE_READY;
    child->generation++;
    child->regs.cr3 = parent->regs.cr3;
    child->regs.rsp = (uint64_t)kstack_top;
    child->regs.rip = (uint64_t)(uintptr_t)task_user_entry;

    uint64_t *iframe = (uint64_t*)(kstack_top - 40);
    iframe[0] = 0x23;
    iframe[1] = stack;
    iframe[2] = RFLAGS_IF | 0x2;
    iframe[3] = 0x1B;
    iframe[4] = parent->regs.rip;
    child->regs.rsp = (uint64_t)iframe;

    if (flags & 0x00080000ull) {
        child->tls_base = tls;
        uint32_t lo = (uint32_t)(tls & 0xFFFFFFFFull);
        uint32_t hi = (uint32_t)(tls >> 32);
        __asm__ volatile("wrmsr" :: "c"(0xC0000100u), "a"(lo), "d"(hi));
    }
    if (ctid) *ctid = child->tid;
    return child->tid;
}

int task_signal_send(int pid, int sig)
{
    if (sig < 1 || sig > 64) return -1;
    Task *t = task_by_pid(pid);
    if (!t) return -3;
    __sync_or_and_fetch(&t->signal_pending, 1ull << (sig - 1));
    return 0;
}

#define UC_NGREG 23

typedef struct {
    uint64_t si_signo;
    uint64_t si_errno;
    uint64_t si_code;
    uint64_t si_addr;
    uint64_t __pad[12];
} ax_siginfo_t;

typedef struct {
    uint64_t uc_flags;
    uint64_t uc_link;
    uint64_t ss_sp;
    uint32_t ss_flags;
    uint32_t __pad1;
    uint64_t ss_size;
    uint64_t gregs[UC_NGREG];
    uint64_t fpregs_mem[64];
    uint64_t __pad2[2];
} ax_ucontext_t;

typedef struct {
    uint64_t pretcode;
    int      sig;
    uint64_t pinfo;
    uint64_t puc;
    ax_siginfo_t info;
    ax_ucontext_t uc;
    uint8_t  retcode[16];
} ax_rt_sigframe_t;

#define REG_R8   0
#define REG_R9   1
#define REG_R10  2
#define REG_R11  3
#define REG_R12  4
#define REG_R13  5
#define REG_R14  6
#define REG_R15  7
#define REG_RDI  8
#define REG_RSI  9
#define REG_RBP  10
#define REG_RBX  11
#define REG_RDX  12
#define REG_RAX  13
#define REG_RCX  14
#define REG_RSP  15
#define REG_RIP  16
#define REG_EFL  17
#define REG_CSGSFS 18
#define REG_ERR  19
#define REG_TRAPNO 20
#define REG_OLDMASK 21
#define REG_CR2  22

int task_signal_send_fault(int pid, int sig, uint64_t fault_addr, uint32_t fault_code)
{
    if (sig < 1 || sig > 64) return -1;
    Task *t = task_by_pid(pid);
    if (!t) return -3;
    t->signal_fault_addr = fault_addr;
    t->signal_fault_code = fault_code;
    __sync_or_and_fetch(&t->signal_pending, 1ull << (sig - 1));
    return 0;
}

void task_signal_deliver(void)
{
    Task *t = &tasks[current_task];
    uint64_t pending = t->signal_pending & ~t->signal_blocked;
    if (!pending) return;

    int sig = 1;
    while (!(pending & (1ull << (sig - 1)))) sig++;
    __sync_and_and_fetch(&t->signal_pending, ~(1ull << (sig - 1)));

    void (*handler)(int) = (void (*)(int))t->signal_handlers[sig];
    if (!handler || handler == (void*)1) {
        if (sig == 11 || sig == 6 || sig == 4 || sig == 8 || sig == 7) {
            task_exit_code(128 + sig);
        }
        return;
    }

    uint64_t frame_rsp = t->regs.rsp;
    if (t->sigaltstack_sp && t->sigaltstack_size &&
        (t->regs.rsp < t->sigaltstack_sp || t->regs.rsp >= t->sigaltstack_sp + t->sigaltstack_size)) {
        frame_rsp = t->sigaltstack_sp + t->sigaltstack_size;
    }
    frame_rsp -= sizeof(ax_rt_sigframe_t);
    frame_rsp &= ~0xFull;
    frame_rsp -= 8;

    ax_rt_sigframe_t *frame = (ax_rt_sigframe_t*)frame_rsp;
    memset(frame, 0, sizeof(*frame));

    frame->pretcode = (uint64_t)(uintptr_t)frame->retcode;
    frame->sig = sig;
    frame->pinfo = (uint64_t)(uintptr_t)&frame->info;
    frame->puc = (uint64_t)(uintptr_t)&frame->uc;

    frame->info.si_signo = (uint64_t)sig;
    frame->info.si_errno = 0;
    frame->info.si_code = t->signal_fault_code;
    frame->info.si_addr = t->signal_fault_addr;

    frame->uc.uc_flags = 0;
    frame->uc.uc_link = 0;
    frame->uc.ss_sp = t->sigaltstack_sp;
    frame->uc.ss_flags = t->sigaltstack_flags;
    frame->uc.ss_size = t->sigaltstack_size;

    frame->uc.gregs[REG_R8]  = t->regs.r8;
    frame->uc.gregs[REG_R9]  = t->regs.r9;
    frame->uc.gregs[REG_R10] = t->regs.r10;
    frame->uc.gregs[REG_R11] = t->regs.r11;
    frame->uc.gregs[REG_R12] = t->regs.r12;
    frame->uc.gregs[REG_R13] = t->regs.r13;
    frame->uc.gregs[REG_R14] = t->regs.r14;
    frame->uc.gregs[REG_R15] = t->regs.r15;
    frame->uc.gregs[REG_RDI] = t->regs.rdi;
    frame->uc.gregs[REG_RSI] = t->regs.rsi;
    frame->uc.gregs[REG_RBP] = t->regs.rbp;
    frame->uc.gregs[REG_RBX] = t->regs.rbx;
    frame->uc.gregs[REG_RDX] = t->regs.rdx;
    frame->uc.gregs[REG_RAX] = t->regs.rcx;
    frame->uc.gregs[REG_RCX] = t->regs.rcx;
    frame->uc.gregs[REG_RSP] = t->regs.rsp;
    frame->uc.gregs[REG_RIP] = t->regs.rip;
    frame->uc.gregs[REG_EFL] = t->regs.rflags;
    frame->uc.gregs[REG_CSGSFS] = 0x33;
    frame->uc.gregs[REG_ERR] = 0;
    frame->uc.gregs[REG_TRAPNO] = 0;
    frame->uc.gregs[REG_OLDMASK] = t->signal_blocked;
    frame->uc.gregs[REG_CR2] = t->signal_fault_addr;

    frame->retcode[0] = 0x48;
    frame->retcode[1] = 0xC7;
    frame->retcode[2] = 0xC0;
    frame->retcode[3] = 0x0F;
    frame->retcode[4] = 0x00;
    frame->retcode[5] = 0x00;
    frame->retcode[6] = 0x00;
    frame->retcode[7] = 0xCD;
    frame->retcode[8] = 0x81;

    uint64_t handler_rsp = frame_rsp;
    handler_rsp -= 8;
    *(uint64_t*)handler_rsp = (uint64_t)(uintptr_t)frame->retcode;

    t->regs.rsp = handler_rsp;
    t->regs.rip = (uint64_t)handler;
    t->regs.rdi = (uint64_t)sig;
    t->regs.rsi = (uint64_t)(uintptr_t)&frame->info;
    t->regs.rdx = (uint64_t)(uintptr_t)&frame->uc;
    t->signal_restorer = (uint64_t)(uintptr_t)frame->retcode;
}

void task_signal_return(void)
{
    Task *t = &tasks[current_task];
    uint64_t rsp = t->regs.rsp;
    ax_rt_sigframe_t *frame = (ax_rt_sigframe_t*)(rsp - 8);

    t->regs.r8  = frame->uc.gregs[REG_R8];
    t->regs.r9  = frame->uc.gregs[REG_R9];
    t->regs.r10 = frame->uc.gregs[REG_R10];
    t->regs.r11 = frame->uc.gregs[REG_R11];
    t->regs.r12 = frame->uc.gregs[REG_R12];
    t->regs.r13 = frame->uc.gregs[REG_R13];
    t->regs.r14 = frame->uc.gregs[REG_R14];
    t->regs.r15 = frame->uc.gregs[REG_R15];
    t->regs.rdi = frame->uc.gregs[REG_RDI];
    t->regs.rsi = frame->uc.gregs[REG_RSI];
    t->regs.rbp = frame->uc.gregs[REG_RBP];
    t->regs.rbx = frame->uc.gregs[REG_RBX];
    t->regs.rdx = frame->uc.gregs[REG_RDX];
    t->regs.rcx = frame->uc.gregs[REG_RCX];
    t->regs.rsp = frame->uc.gregs[REG_RSP];
    t->regs.rip = frame->uc.gregs[REG_RIP];
    t->regs.rflags = frame->uc.gregs[REG_EFL];
    t->signal_blocked = frame->uc.gregs[REG_OLDMASK];
}

int task_fork_current(void)
{
    if (!tasking_initialized) return -1;
    Task *parent = &tasks[current_task];
    if (!parent->is_user || !parent->vas) return -1;

    vas_t *child_vas = vmm_fork_space(parent->vas);
    if (!child_vas) return -12;

    spin_lock(&task_lock);
    if (task_count >= TASK_MAX) {
        spin_unlock(&task_lock);
        vmm_destroy_address_space(child_vas);
        return -11;
    }
    uint32_t slot;
    for (slot = 1u; slot < TASK_MAX; slot++) {
        if (!tasks[slot].active)
            break;
    }
    if (slot == TASK_MAX) {
        spin_unlock(&task_lock);
        vmm_destroy_address_space(child_vas);
        return -11;
    }
    task_count++;
    spin_unlock(&task_lock);

    void *kstack = malloc(TASK_KSTACK_SIZE);
    if (!kstack) {
        spin_lock(&task_lock);
        task_count--;
        spin_unlock(&task_lock);
        vmm_destroy_address_space(child_vas);
        return -12;
    }
    uintptr_t kstack_top = (((uintptr_t)kstack + TASK_KSTACK_SIZE) & ~(uintptr_t)0xFul);

    Task *child = &tasks[slot];
    memcpy((u8int*)child, (u8int*)parent, sizeof(Task));
    child->stack_base = kstack;
    child->vas = child_vas;
    child->pid = next_pid++;
    child->ppid = parent->pid;
    child->tgid = child->pid;
    child->tid = child->pid;
    child->state = TASK_STATE_READY;
    child->generation++;
    child->regs.cr3 = vmm_virt_to_phys_hhdm((uint64_t)(uintptr_t)child_vas->pml4);

    uintptr_t parent_kstack_top = (((uintptr_t)parent->stack_base + TASK_KSTACK_SIZE) & ~(uintptr_t)0xFul);
    uintptr_t parent_rsp = parent->regs.rsp;
    uintptr_t stack_used = parent_kstack_top - parent_rsp;
    if (stack_used > TASK_KSTACK_SIZE - 64) stack_used = TASK_KSTACK_SIZE - 64;
    uintptr_t child_rsp = kstack_top - stack_used;
    memcpy((void*)child_rsp, (void*)parent_rsp, stack_used);
    child->regs.rsp = (uint64_t)child_rsp;
    child->regs.rip = parent->regs.rip;
    child->regs.rbx = parent->regs.rbx;
    child->regs.rbp = parent->regs.rbp;
    child->regs.r12 = parent->regs.r12;
    child->regs.r13 = parent->regs.r13;
    child->regs.r14 = parent->regs.r14;
    child->regs.r15 = parent->regs.r15;
    child->regs.rflags = parent->regs.rflags;

    return child->pid;
}

uint32_t task_count_active(void)
{
    return task_count;
}

static void task_wake_sleepers(void)
{
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        if (tasks[i].active && tasks[i].state == TASK_STATE_SLEEPING) {
            if ((int32_t)(tick - tasks[i].wake_tick) >= 0)
                tasks[i].state = TASK_STATE_READY;
        }
    }
}

void task_schedule_from_irq(void)
{
    if (!tasking_initialized || task_count < 2u)
        return;
    task_wake_sleepers();
    if (task_next_active(current_task) != current_task)
        yield();
}
