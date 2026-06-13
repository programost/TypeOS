#ifndef SCHED_H
#define SCHED_H

#include "types.h"

void sched_init (void);
void sched_tick (void);
int sched_irq_frame (uint64_t *frame);
void sched_yield (void);
void sched_block_current (void);
void sched_wake_pid (int pid);
void sched_wake_all (void);
int sched_after_syscall (int64_t syscall_ret);
uint64_t sched_current_user_rip (void);
uint64_t sched_current_user_rsp (void);
uint64_t sched_current_user_rflags (void);
uint64_t sched_current_user_rax (void);

#endif
