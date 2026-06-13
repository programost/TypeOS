#include "include/sched.h"
#include "include/proc.h"
#include "include/gdt.h"
#include "include/paging.h"

#define MSR_FS_BASE 0xC0000100U

static unsigned rr_index;
static uint64_t ticks;

static void
wrmsr (uint32_t msr, uint64_t value)
{
        uint32_t low = (uint32_t) value;
        uint32_t high = (uint32_t) (value >> 32);

        __asm__ volatile ("wrmsr" :: "c" (msr), "a" (low), "d" (high) : "memory");
}

static int
sched_count_runnable (void)
{
        struct process *tasks = proc_tasks ();
        unsigned i;
        int count = 0;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state == TASK_RUNNABLE
                    || tasks[i].state == TASK_RUNNING) {
                        count++;
                }
        }

        return count;
}

static struct process *
sched_pick_next (void)
{
        struct process *tasks = proc_tasks ();
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                unsigned idx = (rr_index + i + 1U) % PROC_MAX_TASKS;

                if (tasks[idx].state == TASK_RUNNABLE
                    || tasks[idx].state == TASK_RUNNING) {
                        rr_index = idx;
                        return &tasks[idx];
                }
        }

        return proc_current ();
}

static void
sched_switch_to (struct process *next)
{
        struct process *cur = proc_current ();

        if (next == NULL || next == cur) {
                if (cur->state == TASK_RUNNABLE) {
                        cur->state = TASK_RUNNING;
                }
                return;
        }

        if (cur->state == TASK_RUNNING) {
                cur->state = TASK_RUNNABLE;
        }

        next->state = TASK_RUNNING;
        proc_switch_current (next);
        paging_switch_cr3 (next->cr3);
        wrmsr (MSR_FS_BASE, next->fs_base);
        if (next->kernel_rsp != 0) {
                gdt_set_rsp0 (next->kernel_rsp);
        }
}

void
sched_init (void)
{
        rr_index = 0;
        ticks = 0;
        if (proc_current ()->kernel_rsp != 0) {
                gdt_set_rsp0 (proc_current ()->kernel_rsp);
        }
}

void
sched_tick (void)
{
        ticks++;
}

int
sched_irq_frame (uint64_t *frame)
{
        (void) frame;
        ticks++;
        return 0;
}

void
sched_yield (void)
{
        if (sched_count_runnable () <= 1) {
                return;
        }

        sched_switch_to (sched_pick_next ());
}

void
sched_block_current (void)
{
        struct process *cur = proc_current ();

        if (proc_run_runnable ()) {
                if (cur->state == TASK_RUNNABLE) {
                        cur->state = TASK_RUNNING;
                }
                return;
        }

        cur->state = TASK_BLOCKED;
        sched_yield ();

        while (proc_current () == cur && cur->state == TASK_BLOCKED) {
                if (proc_run_runnable ()) {
                        break;
                }
                __asm__ volatile ("sti; hlt" ::: "memory");
        }

        if (cur->state == TASK_RUNNABLE) {
                cur->state = TASK_RUNNING;
        }
}

void
sched_wake_pid (int pid)
{
        struct process *task = proc_task_by_pid (pid);

        if (task != NULL && task->state == TASK_BLOCKED) {
                task->state = TASK_RUNNABLE;
        }
}

void
sched_wake_all (void)
{
        struct process *tasks = proc_tasks ();
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state == TASK_BLOCKED) {
                        tasks[i].state = TASK_RUNNABLE;
                }
        }
}

int
sched_after_syscall (int64_t syscall_ret)
{
        proc_current ()->user_rax = (uint64_t) syscall_ret;
        return (int) syscall_ret;
}

uint64_t
sched_current_user_rip (void)
{
        return proc_current ()->user_rip;
}

uint64_t
sched_current_user_rsp (void)
{
        return proc_current ()->user_rsp;
}

uint64_t
sched_current_user_rflags (void)
{
        return proc_current ()->user_rflags;
}

uint64_t
sched_current_user_rax (void)
{
        return proc_current ()->user_rax;
}
