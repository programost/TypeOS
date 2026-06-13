#ifndef PROC_H
#define PROC_H

#include "types.h"

struct vfs_file;

#define PROC_MAX_FD       32
#define PROC_MAX_PIPE     8
#define PROC_MAX_TASKS    16
#define TASK_KERNEL_STACK_SIZE (64U * 1024U)
#define USER_STACK_TOP    0x7FF00000ULL
#define USER_STACK_SIZE   (128ULL * 1024ULL)
#define USER_BRK_START    0x80000000ULL

enum task_state {
        TASK_UNUSED = 0,
        TASK_RUNNABLE,
        TASK_RUNNING,
        TASK_BLOCKED,
        TASK_ZOMBIE,
};

struct pipe {
        uint8_t  buf[4096];
        size_t   head;
        size_t   tail;
        size_t   count;
        int      readers;
        int      writers;
        int      in_use;
};

struct process {
        uint64_t brk;
        uint64_t cr3;
        uint64_t kernel_rsp;
        uint64_t user_rsp;
        uint64_t user_rip;
        uint64_t user_rflags;
        uint64_t user_rax;
        uint64_t user_regs[15];
        uint64_t fs_base;
        uint64_t wake_deadline;
        uint8_t  *kernel_stack;
        char     cwd[256];
        int      pid;
        int      ppid;
        int      sid;
        int      pgid;
        int      exit_status;
        enum task_state state;
        struct process *parent;
        struct vfs_file *fds[PROC_MAX_FD];
};

struct process *proc_current (void);
struct process *proc_task_by_pid (int pid);
struct process *proc_tasks (void);
void proc_switch_current (struct process *task);
void proc_init (void);
int proc_exec (const char *path, const char *const *argv, const char *const *envp);
void proc_exit (int status);
void proc_fd_close_all (void);
int64_t proc_fork (void);
int proc_fork_is_child (void);
void proc_fork_clear_child (void);
void proc_fork_return_to_parent (int status);
int proc_fork_child_pid (void);
int proc_fork_take_status (int pid, int *status_out);
int proc_has_children (void);
struct pipe *proc_pipe_alloc (void);
void proc_pipe_free (struct pipe *pipe);
void proc_save_user_context (uint64_t rip, uint64_t rsp, uint64_t rflags);
void proc_save_syscall_frame (uint64_t *kframe, uint64_t rcx, uint64_t r11);
void proc_save_irq_frame (uint64_t *frame);
void proc_restore_irq_frame (uint64_t *frame);
void proc_set_user_context (uint64_t rip, uint64_t rsp, uint64_t rflags);
void proc_set_fs_base (uint64_t fs_base);
void proc_wake_parent (void);
int proc_run_runnable (void);
int proc_fork_sync_exit (int status);

#endif
