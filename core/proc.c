/*
 * proc.c - user process execution and minimal fork/wait.
 */

#include "include/proc.h"
#include "include/fs/vfs.h"
#include "include/heap.h"
#include "include/elf.h"
#include "include/gdt.h"
#include "include/string.h"
#include "include/vga.h"
#include "include/gdt.h"
#include "include/paging.h"
#include "include/syscall.h"

#define MSR_FS_BASE 0xC0000100U

extern int setjmp_asm (uint64_t *buf);
extern void longjmp_asm (uint64_t *buf, int val);

static uint64_t fork_jmp_buf[8];
static int fork_jmp_active;
static struct process *fork_jmp_parent;

static struct process tasks[PROC_MAX_TASKS];
static struct process *current_task = &tasks[0];
static struct pipe pipes[PROC_MAX_PIPE];

static int next_pid = 2;

static void
wrmsr (uint32_t msr, uint64_t value)
{
        uint32_t low = (uint32_t) value;
        uint32_t high = (uint32_t) (value >> 32);

        __asm__ volatile ("wrmsr" :: "c" (msr), "a" (low), "d" (high) : "memory");
}

static struct process *
proc_alloc_task (void)
{
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state == TASK_UNUSED) {
                        memset (&tasks[i], 0, sizeof (tasks[i]));
                        tasks[i].pid = next_pid++;
                        tasks[i].state = TASK_RUNNABLE;
                        tasks[i].cr3 = paging_current_cr3 ();
                        tasks[i].kernel_stack = (uint8_t *) kmalloc (TASK_KERNEL_STACK_SIZE);
                        if (tasks[i].kernel_stack == NULL) {
                                tasks[i].state = TASK_UNUSED;
                                return NULL;
                        }
                        tasks[i].kernel_rsp = (uint64_t) (uintptr_t)
                                (tasks[i].kernel_stack + TASK_KERNEL_STACK_SIZE);
                        return &tasks[i];
                }
        }

        return NULL;
}

static void
proc_open_console_fd (int fd)
{
        struct vfs_file *file;

        if (fd < 0 || fd >= PROC_MAX_FD || current_task->fds[fd] != NULL) {
                return;
        }

        file = (struct vfs_file *) kmalloc (sizeof (*file));
        if (file == NULL) {
                return;
        }

        file->inode = NULL;
        file->pos = 0;
        strcpy (file->path, "/dev/console");
        file->pipe = NULL;
        file->pipe_end = -1;
        current_task->fds[fd] = file;
}

static void
proc_open_standard_fds (void)
{
        proc_open_console_fd (0);
        proc_open_console_fd (1);
        proc_open_console_fd (2);
}

struct process *
proc_current (void)
{
        return current_task;
}

struct process *
proc_task_by_pid (int pid)
{
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state != TASK_UNUSED && tasks[i].pid == pid) {
                        return &tasks[i];
                }
        }

        return NULL;
}

struct process *
proc_tasks (void)
{
        return tasks;
}

void
proc_switch_current (struct process *task)
{
        if (task != NULL) {
                current_task = task;
        }
}

struct pipe *
proc_pipe_alloc (void)
{
        unsigned i;

        for (i = 0; i < PROC_MAX_PIPE; i++) {
                if (!pipes[i].in_use) {
                        memset (&pipes[i], 0, sizeof (pipes[i]));
                        pipes[i].in_use = 1;
                        return &pipes[i];
                }
        }

        return NULL;
}

void
proc_pipe_free (struct pipe *pipe)
{
        if (pipe != NULL) {
                pipe->in_use = 0;
        }
}

void
proc_fd_close_all (void)
{
        unsigned i;

        for (i = 3; i < PROC_MAX_FD; i++) {
                if (current_task->fds[i] != NULL) {
                        vfs_close (current_task->fds[i]);
                        kfree (current_task->fds[i]);
                        current_task->fds[i] = NULL;
                }
        }
}

int
proc_fork_is_child (void)
{
        return 0;
}

void
proc_fork_clear_child (void)
{
}

int
proc_fork_child_pid (void)
{
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state != TASK_UNUSED && tasks[i].parent == current_task) {
                        return tasks[i].pid;
                }
        }

        return -1;
}

int
proc_has_children (void)
{
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state != TASK_UNUSED
                    && tasks[i].parent == current_task
                    && tasks[i].state != TASK_ZOMBIE) {
                        return 1;
                }
        }

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                if (tasks[i].state == TASK_ZOMBIE
                    && tasks[i].parent == current_task) {
                        return 1;
                }
        }

        return 0;
}

int
proc_fork_take_status (int pid, int *status_out)
{
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                struct process *child = &tasks[i];

                if (child->state != TASK_ZOMBIE || child->parent != current_task) {
                        continue;
                }

                if (pid != -1 && pid != child->pid) {
                        continue;
                }

                if (status_out != NULL) {
                        *status_out = child->exit_status;
                }

                pid = child->pid;
                child->state = TASK_UNUSED;
                return pid;
        }

        return -1;
}

int64_t
proc_fork (void)
{
        struct process *parent = current_task;
        struct process *child = proc_alloc_task ();
        uint64_t child_cr3;
        unsigned i;

        if (child == NULL) {
                return -1;
        }

        child_cr3 = paging_clone_current_address_space (1);
        if (child_cr3 == 0) {
                child->state = TASK_UNUSED;
                return -1;
        }

        child->cr3 = child_cr3;
        child->brk = parent->brk;
        child->ppid = parent->pid;
        child->sid = parent->sid;
        child->pgid = parent->pgid;
        child->parent = parent;
        child->user_rip = parent->user_rip;
        child->user_rsp = parent->user_rsp;
        child->user_rflags = parent->user_rflags;
        child->user_rax = 0;
        memcpy (child->user_regs, parent->user_regs, sizeof (child->user_regs));
        child->fs_base = parent->fs_base;
        strcpy (child->cwd, parent->cwd);

        for (i = 0; i < PROC_MAX_FD; i++) {
                if (parent->fds[i] == NULL) {
                        continue;
                }

                child->fds[i] = (struct vfs_file *) kmalloc (sizeof (*child->fds[i]));
                if (child->fds[i] == NULL) {
                        child->state = TASK_UNUSED;
                        return -1;
                }

                memcpy (child->fds[i], parent->fds[i], sizeof (*child->fds[i]));
                if (child->fds[i]->pipe != NULL && child->fds[i]->pipe_end == 0) {
                        child->fds[i]->pipe->readers++;
                } else if (child->fds[i]->pipe != NULL && child->fds[i]->pipe_end == 1) {
                        child->fds[i]->pipe->writers++;
                }
        }

        child->state = TASK_RUNNABLE;

        return child->pid;
}

static void
proc_activate (struct process *task)
{
        proc_switch_current (task);
        paging_switch_cr3 (task->cr3);
        wrmsr (MSR_FS_BASE, task->fs_base);
        if (task->kernel_rsp != 0) {
                gdt_set_rsp0 (task->kernel_rsp);
        }
        task->state = TASK_RUNNING;
}

int
proc_run_runnable (void)
{
        struct process *self = current_task;
        struct process *task;
        unsigned i;

        for (i = 0; i < PROC_MAX_TASKS; i++) {
                task = &tasks[i];
                if (task->state != TASK_RUNNABLE || task == self) {
                        continue;
                }

                fork_jmp_parent = self;
                fork_jmp_active = 1;
                if (setjmp_asm (fork_jmp_buf) == 0) {
                        proc_activate (task);
                        usermode_resume_task (task);
                }
                fork_jmp_active = 0;
                fork_jmp_parent = NULL;
                proc_activate (self);
                return 1;
        }

        return 0;
}

int
proc_fork_sync_exit (int status)
{
        if (!fork_jmp_active || fork_jmp_parent == NULL) {
                return 0;
        }

        current_task->exit_status = status;
        current_task->state = TASK_ZOMBIE;
        proc_wake_parent ();
        longjmp_asm (fork_jmp_buf, 1);
        return 1;
}

void
proc_fork_return_to_parent (int status)
{
        (void) status;
}

void
proc_save_user_context (uint64_t rip, uint64_t rsp, uint64_t rflags)
{
        current_task->user_rip = rip;
        current_task->user_rsp = rsp;
        current_task->user_rflags = rflags;
}

void
proc_save_syscall_frame (uint64_t *kframe, uint64_t rcx, uint64_t r11)
{
        current_task->user_regs[0] = kframe[0];
        current_task->user_regs[1] = kframe[4];
        current_task->user_regs[2] = rcx;
        current_task->user_regs[3] = kframe[3];
        current_task->user_regs[4] = kframe[2];
        current_task->user_regs[5] = kframe[1];
        current_task->user_regs[6] = kframe[5];
        current_task->user_regs[7] = kframe[6];
        current_task->user_regs[8] = kframe[7];
        current_task->user_regs[9] = kframe[8];
        current_task->user_regs[10] = r11;
        current_task->user_regs[11] = kframe[10];
        current_task->user_regs[12] = kframe[11];
        current_task->user_regs[13] = kframe[12];
        current_task->user_regs[14] = kframe[13];
        current_task->user_rax = kframe[0];
}

void
proc_save_irq_frame (uint64_t *frame)
{
        unsigned i;

        current_task->user_rip = frame[15];
        current_task->user_rsp = frame[18];
        current_task->user_rflags = frame[17];
        current_task->user_rax = frame[14];

        for (i = 0; i < 15; i++) {
                current_task->user_regs[i] = frame[14 - i];
        }
}

void
proc_restore_irq_frame (uint64_t *frame)
{
        unsigned i;

        frame[15] = current_task->user_rip;
        frame[18] = current_task->user_rsp;
        frame[17] = current_task->user_rflags;
        frame[14] = current_task->user_rax;

        for (i = 0; i < 15; i++) {
                frame[14 - i] = current_task->user_regs[i];
        }
}

void
proc_wake_parent (void)
{
        if (current_task->parent != NULL) {
                if (current_task->parent->state == TASK_BLOCKED) {
                        current_task->parent->state = TASK_RUNNABLE;
                }
        }
}

void
proc_set_user_context (uint64_t rip, uint64_t rsp, uint64_t rflags)
{
        current_task->user_rip = rip;
        current_task->user_rsp = rsp;
        current_task->user_rflags = rflags;
}

void
proc_set_fs_base (uint64_t fs_base)
{
        current_task->fs_base = fs_base;
        wrmsr (MSR_FS_BASE, fs_base);
}

void
proc_exit (int status)
{
        if (proc_fork_sync_exit (status)) {
                return;
        }

        current_task->exit_status = status;
        current_task->state = TASK_ZOMBIE;
        proc_wake_parent ();
}

int
proc_exec (const char *path, const char *const *argv, const char *const *envp)
{
        uint64_t entry = 0;
        uint64_t stack = 0;
        uint64_t argc = 0;
        uint64_t argvp = 0;
        uint64_t envpp = 0;
        struct vfs_stat st;

        if (path == NULL || vfs_stat (path, &st) != 0 || st.type != VFS_TYPE_FILE) {
                kprintf ("exec: not found or not a file: %s\n", path ? path : "(null)");
                return -1;
        }

        proc_fd_close_all ();

        {
                uint64_t new_cr3 = paging_clone_current_address_space (0);

                if (new_cr3 != 0) {
                        current_task->cr3 = new_cr3;
                        paging_switch_cr3 (new_cr3);
                }
        }

        current_task->brk = USER_BRK_START;
        current_task->fs_base = 0;
        wrmsr (MSR_FS_BASE, 0);

        if (elf_load (path, &entry, &stack) != 0) {
                return -1;
        }

        stack = elf_user_stack_init (argv, envp, &argc, &argvp, &envpp);
        if (stack == 0) {
                kprintf ("exec: user stack setup failed\n");
                return -1;
        }

        kprintf ("exec: jumping to %s @ %p sp=%p argc=%llu\n",
                 path, (void *) (uintptr_t) entry, (void *) (uintptr_t) stack,
                 (unsigned long long) argc);

        paging_flush_tlb ();
        current_task->user_rip = entry;
        current_task->user_rsp = stack;
        current_task->user_rflags = 0x202ULL;
        current_task->user_rax = 0;
        memset (current_task->user_regs, 0, sizeof (current_task->user_regs));
        current_task->cr3 = paging_current_cr3 ();
        enter_usermode (entry, stack, argc, argvp, envpp);
        return -1;
}

void
proc_init (void)
{
        memset (tasks, 0, sizeof (tasks));
        current_task = &tasks[0];
        current_task->pid = 1;
        current_task->ppid = 0;
        current_task->sid = 1;
        current_task->pgid = 1;
        current_task->brk = USER_BRK_START;
        current_task->cr3 = paging_current_cr3 ();
        current_task->kernel_stack = (uint8_t *) kmalloc (TASK_KERNEL_STACK_SIZE);
        if (current_task->kernel_stack != NULL) {
                current_task->kernel_rsp = (uint64_t) (uintptr_t)
                        (current_task->kernel_stack + TASK_KERNEL_STACK_SIZE);
        }
        current_task->state = TASK_RUNNING;
        strcpy (current_task->cwd, "/");
        proc_open_standard_fds ();
}
