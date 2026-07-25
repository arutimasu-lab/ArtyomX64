/*
 * Cooperative task scheduler.
 *
 * Design notes (read this before touching the file):
 *
 * - The context that calls initTasking() (in practice: the kernel's
 *   main/compositor loop) is itself registered as a real Task -- slot 0,
 *   pid TASK_ROOT_PID. This is required for yield()/task_exit() to have
 *   somewhere valid to switch *back* to.
 *
 * - The root task is a NORMAL scheduling candidate, exactly like every
 *   spawned app task. It must NEVER be excluded from pick_next_ready()'s
 *   search -- doing so means that as soon as exactly one app task exists,
 *   yield() has nowhere to switch to (the only other task in the system
 *   is root, and it's filtered out), so control never returns to the
 *   compositor: mouse/keyboard/compositing all run as part of the root
 *   task and freeze along with it. The only thing that's special about
 *   root is that it may never be *terminated* (see task_exit()).
 *
 * - Scheduling is purely cooperative: a task keeps the CPU until it calls
 *   yield() (directly, or indirectly via the AX_SYS_YIELD syscall) or
 *   task_exit()s. There is no timer-driven preemption yet. A task that
 *   spins forever without ever calling yield() will freeze the entire
 *   system. Every long-running task (including every userspace app's
 *   event loop) MUST yield periodically.
 */

#include "task.h"
#include "../fs/fs.h"
#include "../mm/kheap.h"
#include "../drivers/monitor.h"
#include "../lib/common.h"
#include <stddef.h>

#define malloc kmalloc
#define free   kfree

#define STACK_SIZE 16384
#define MAX_TASKS  16

static Task tasks[MAX_TASKS];
Task *runningTask   = NULL;
static Task *taskListHead = NULL;
static int   task_count = 0;
static int   next_pid   = 1;

static void debug_putc(char c) { outb(0x3F8, c); }
static void debug_puts(const char *s) { while (*s) debug_putc(*s++); }
static void debug_putnum(uint64_t n)
{
    char buf[32];
    int i = 0;
    if (n == 0) { debug_putc('0'); return; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i) debug_putc(buf[--i]);
}

static void task_list_insert(Task *t)
{
    if (taskListHead == NULL) {
        taskListHead = t;
        t->next = t;
        t->prev = t;
        return;
    }
    Task *last = taskListHead->prev;
    last->next = t;
    t->prev = last;
    t->next = taskListHead;
    taskListHead->prev = t;
}

static void task_list_remove(Task *t)
{
    if (t->next == t) {
        taskListHead = NULL;
        return;
    }
    t->prev->next = t->next;
    t->next->prev = t->prev;
    if (taskListHead == t) taskListHead = t->next;
}

static void copy_name(char *dst, const char *src, size_t dstsz)
{
    size_t i = 0;
    if (src) {
        for (; src[i] && i < dstsz - 1; i++) dst[i] = src[i];
    }
    dst[i] = 0;
}

void initTasking(void)
{
    debug_puts("TASK: init\n");

    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state      = TASK_TERMINATED;
        tasks[i].stack      = NULL;
        tasks[i].stack_base = NULL;
        tasks[i].next       = NULL;
        tasks[i].prev       = NULL;
        tasks[i].pid        = 0;
        tasks[i].name[0]    = 0;
    }

    task_count   = 0;
    taskListHead = NULL;
    runningTask  = NULL;
    next_pid     = 1;

    Task *root = &tasks[0];
    root->state = TASK_RUNNING;
    root->pid   = next_pid++;   /* == TASK_ROOT_PID, i.e. 1 */
    copy_name(root->name, "kernel", sizeof(root->name));
    task_list_insert(root);

    runningTask = root;
    task_count  = 1;

    debug_puts("TASK: init done (root pid=");
    debug_putnum(root->pid);
    debug_puts(")\n");
}

int task_spawn(void (*entry)(void *arg), void *arg, const char *name)
{
    debug_puts("TASK: spawn ");
    debug_puts(name ? name : "unknown");
    debug_putc('\n');

    if (!entry) {
        debug_puts("TASK: entry is NULL\n");
        return -1;
    }
    if (task_count >= MAX_TASKS) {
        debug_puts("TASK: too many tasks\n");
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_TERMINATED) { slot = i; break; }
    }
    if (slot < 0) {
        debug_puts("TASK: no free slot\n");
        return -1;
    }

    Task *task = &tasks[slot];

    task->stack = malloc(STACK_SIZE);
    if (!task->stack) {
        debug_puts("TASK: malloc failed\n");
        return -1;
    }
    task->stack_base = task->stack;

    memset((uint8_t *)&task->regs, 0, sizeof(Registers));

    uint64_t *stack_top = (uint64_t *)((uint64_t)task->stack + STACK_SIZE);
    *--stack_top = (uint64_t)task_exit;

    task->regs.rsp    = (uint64_t)stack_top;
    task->regs.rip    = (uint64_t)entry;
    task->regs.rdi    = (uint64_t)arg;
    task->regs.rflags = 0x202; /* IF set */

    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    task->regs.cr3 = cr3;

    task->pid = next_pid++;
    copy_name(task->name, name, sizeof(task->name));
    task->state = TASK_READY;

    task_list_insert(task);
    task_count++;

    debug_puts("TASK: spawned PID=");
    debug_putnum(task->pid);
    debug_puts(" entry=");
    debug_putnum((uint64_t)entry);
    debug_putc('\n');

    return task->pid;
}

/*
 * Single source of truth for "who runs next". Root is a completely
 * ordinary candidate here -- do not add any pid-based exclusion for it.
 * The only exclusion is "not the currently running task" and "actually
 * READY".
 */
static Task *pick_next_ready(void)
{
    if (!runningTask || task_count <= 1) return NULL;

    /* Round-robin: start the scan right after the currently running
     * task's own slot, wrapping around. Starting from index 0 every
     * time means two low-index tasks that keep yielding to each other
     * (e.g. root and a stuck task at slot 1) will starve every task at
     * a higher index forever -- a newly spawned task can sit READY and
     * simply never get picked. */
    int start = (int)(runningTask - tasks);
    for (int off = 1; off <= MAX_TASKS; off++) {
        Task *t = &tasks[(start + off) % MAX_TASKS];
        if (t->state == TASK_READY) return t;
    }
    return NULL;
}

bool task_is_alive(int pid)
{
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].pid == pid && tasks[i].state != TASK_TERMINATED)
            return true;
    return false;
}

void yield(void)
{
    Task *next = pick_next_ready();
    if (!next) return; /* nothing else ready to run */

    Task *old = runningTask;

    if (old) {
        old->state = TASK_READY;
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        old->regs.cr3 = cr3;
    }

    next->state = TASK_RUNNING;
    runningTask = next;

    switchTask(old ? &old->regs : NULL, &next->regs);
    /* Execution resumes here once someone yields back to `old`. */
}

void task_exit(int code)
{
    (void)code;

    Task *self = runningTask;
    if (!self) return;

    debug_puts("TASK: exit PID=");
    debug_putnum(self->pid);
    debug_putc('\n');

    if (self->pid == TASK_ROOT_PID) {
        /* The only thing actually special about root: it must never
         * be torn down. Everything else about it is a normal task. */
        debug_puts("TASK: root task tried to exit -- halting\n");
        for (;;) __asm__ volatile("cli; hlt");
    }

    self->state = TASK_TERMINATED;

    if (self->stack) {
        free(self->stack);
        self->stack = NULL;
        self->stack_base = NULL;
    }

    Task *next = pick_next_ready();
    task_list_remove(self);
    task_count--;

    if (!next) next = &tasks[0]; /* nobody else ready -- fall back to root */

    next->state = TASK_RUNNING;
    runningTask = next;

    switchTask(NULL, &next->regs);
    /* Never reached. */
    for (;;) __asm__ volatile("cli; hlt");
}
