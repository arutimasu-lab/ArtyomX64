#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Layout of this struct is load-bearing: switch.S addresses every    */
/* field by hardcoded byte offset (0, 8, 16, ... 144). If you add,    */
/* remove or reorder fields here, switch.S MUST be updated to match,  */
/* and vice versa. Every field is a plain uint64_t so there is no     */
/* padding between them -- offset(N) == N * 8.                        */
/* ------------------------------------------------------------------ */
typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;  /*   0 .. 56 */
    uint64_t rdi, rsi, rbp, rdx, rcx, rbx, rax;      /*  64 .. 112 */
    uint64_t rip;                                     /* 120 */
    uint64_t rsp;                                     /* 128 */
    uint64_t rflags;                                  /* 136 */
    uint64_t cr3;                                      /* 144 */
} Registers;

typedef enum {
    TASK_TERMINATED = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
} TaskState;

typedef struct Task {
    Registers   regs;
    TaskState   state;
    int         pid;
    void       *stack;       /* base of the kmalloc'd stack, for freeing */
    void       *stack_base;
    struct Task *next;       /* circular doubly-linked list of all tasks */
    struct Task *prev;
    char        name[32];
} Task;

/* Special pid representing the task that called initTasking() (i.e.
 * the "main"/compositor thread). It is always task slot 0 and is
 * never terminated by task_exit(). */
#define TASK_ROOT_PID 1

void  initTasking(void);

/* entry(arg) runs as a new cooperative task. Returns the new task's
 * pid, or -1 on failure (no free slot / allocation failure). */
int   task_spawn(void (*entry)(void *arg), void *arg, const char *name);

/* Voluntarily give up the CPU to the next READY task. No-op if there
 * is nothing else ready to run. Must be called by every long-running
 * task periodically (this scheduler is currently cooperative, not
 * preemptive -- a task that never calls yield()/blocks starves
 * everyone else). */
void  yield(void);

/* Terminate the calling task and switch to whichever task should run
 * next. Never returns. */
void  task_exit(int code);

/* Implemented in switch.S. Called with old == NULL means "there is no
 * previous context to save, just load new". Called with new == NULL
 * is a no-op (nothing to switch to). */
void  switchTask(Registers *old, Registers *new);

/* True if a task with this pid exists and hasn't reached TASK_TERMINATED
 * yet. Used by the compositor to know when it's safe to actually free a
 * closed window's resources (i.e. after the owning task itself has
 * finished handling AX_EV_CLOSE and called task_exit(), not at the
 * moment the user clicked the close button). */
#include <stdbool.h>
bool task_is_alive(int pid);

extern Task *runningTask;

#endif
