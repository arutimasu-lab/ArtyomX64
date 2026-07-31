#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <stdbool.h>
#include "../mm/vmm.h"

#define TASK_MAX        32u
#define TASK_KSTACK_SIZE (64u * 1024u)
#define TASK_QUANTUM_MS 10u

#define TASK_STATE_FREE     0u
#define TASK_STATE_RUNNING  1u
#define TASK_STATE_READY    2u
#define TASK_STATE_SLEEPING 3u
#define TASK_STATE_ZOMBIE   4u

typedef struct {
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rbp;
    uint64_t cr3;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t rip;
} Registers;

typedef struct Task {
    Registers regs;
    void *stack_base;
    void (*entry)(void);
    uint32_t generation;
    uint8_t active;
    uint8_t reserved[3];

    int      pid;
    int      ppid;
    uint32_t state;
    uint32_t wake_tick;
    int      exit_code;
    bool     is_user;
    vas_t   *vas;
    char     name[32];
    uint64_t tls_base;
    uint64_t clear_child_tid;

    int      tgid;
    int      tid;
    uint64_t signal_pending;
    uint64_t signal_blocked;
    void    *signal_handlers[64];
    uint64_t sigaltstack_sp;
    uint64_t sigaltstack_size;
    uint32_t sigaltstack_flags;
    uint64_t signal_restorer;
    uint64_t signal_fault_addr;
    uint32_t signal_fault_code;
} Task;

void initTasking(void);
int  createTask(Task *task, void (*entry)(void), uint64_t flags, uint64_t *pagedir);
int  task_spawn(void (*entry)(void));
int  task_spawn_user(const char *name, uint64_t entry, uint64_t user_stack_top, vas_t *as);
void task_exit(void) __attribute__((noreturn));
void task_exit_code(int code) __attribute__((noreturn));
void yield(void);
void switchTask(Registers *old, Registers *new);

Task *task_current(void);
Task *task_by_pid(int pid);
int   task_current_pid(void);
void  task_sleep_ticks(uint32_t ticks);
int   task_kill(int pid);
void  task_schedule_from_irq(void);
vas_t *task_current_vas(void);
void  task_set_name(int pid, const char *name);
uint32_t task_count_active(void);
int   task_fork_current(void);
int   task_clone_thread(uint64_t flags, uint64_t stack, uint64_t tls, int *ctid);
void  task_set_tls(uint64_t base);
uint64_t task_get_tls(void);
int   task_signal_send(int pid, int sig);
int   task_signal_send_fault(int pid, int sig, uint64_t fault_addr, uint32_t fault_code);
void  task_signal_deliver(void);
void  task_signal_return(void);

#endif
