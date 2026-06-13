bits 64

global gdt_load
global syscall_entry
global enter_usermode
global usermode_resume
global usermode_resume_task

extern syscall_dispatch
extern proc_save_user_context
extern proc_save_syscall_frame
extern sched_after_syscall
extern sched_current_user_rip
extern sched_current_user_rsp
extern sched_current_user_rflags
extern tss_rsp0

section .text

gdt_load:
        lgdt [rdi]
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov ss, ax
        mov fs, ax
        mov gs, ax
        ret

syscall_entry:
        mov [rel sys_save_rax], rax
        mov [rel sys_save_rdi], rdi
        mov [rel sys_save_rsi], rsi
        mov [rel sys_save_rdx], rdx
        mov [rel sys_save_r10], r10
        mov [rel sys_save_r8], r8
        mov [rel sys_save_r9], r9
        mov [rel sys_save_r11], r11
        mov [rel sys_save_rcx], rcx
        mov [rel sys_save_rsp], rsp
        mov rdi, rcx
        mov rsi, rsp
        mov rdx, r11
        mov rsp, [rel tss_rsp0]
        and rsp, ~0xF
        sub rsp, 8
        call proc_save_user_context
        add rsp, 8
        mov rsp, [rel tss_rsp0]

        push r15
        push r14
        push r13
        push r12
        push r11
        push r10
        push r9
        push r8
        push rbp
        push rbx
        push qword [rel sys_save_rdx]
        push qword [rel sys_save_rsi]
        push qword [rel sys_save_rdi]
        push qword [rel sys_save_rax]

        mov rdi, rsp
        mov rsi, [rel sys_save_rcx]
        mov rdx, [rel sys_save_r11]
        sub rsp, 8
        call proc_save_syscall_frame
        add rsp, 8

        mov rdi, [rsp + 0]          ; num
        mov rsi, [rsp + 8]          ; a0
        mov rdx, [rsp + 16]         ; a1
        mov rcx, [rsp + 24]         ; a2
        mov r8,  [rel sys_save_r10] ; a3
        mov r9,  [rel sys_save_r8]  ; a4
        push qword [rel sys_save_r9]; a5
        call syscall_dispatch
        add rsp, 8
        mov rdi, rax
        sub rsp, 8
        call sched_after_syscall
        add rsp, 8

        mov [rsp], rax
        mov [rel sys_return_rax], rax

        pop rax
        pop rdi
        pop rsi
        pop rdx
        pop rbx
        pop rbp
        pop r8
        pop r9
        pop r10
        pop r11
        pop r12
        pop r13
        pop r14
        pop r15

        sub rsp, 8
        call sched_current_user_rip
        add rsp, 8
        mov rcx, rax
        sub rsp, 8
        call sched_current_user_rflags
        add rsp, 8
        mov r11, rax
        sub rsp, 8
        call sched_current_user_rsp
        add rsp, 8
        mov rsp, rax
        mov rax, [rel sys_return_rax]
        o64 sysret

; void enter_usermode (uint64_t rip, uint64_t rsp, uint64_t argc,
;                      uint64_t argv, uint64_t envp);
enter_usermode:
        mov [rel save_rip], rdi
        mov [rel save_rsp], rsi
        mov [rel save_argc], rdx
        mov [rel save_argv], rcx
        mov [rel save_envp], r8

        mov rdi, [rel save_argc]
        mov rsi, [rel save_argv]
        mov rdx, [rel save_envp]

        mov ax, 0x1B
        mov ds, ax
        mov es, ax

        pushfq
        pop rax
        or rax, 2
        or rax, 0x200
        mov [rel save_rflags], rax

        mov rax, [rel save_rip]
        and rax, ~0xFFF
        invlpg [rax]

        mov rsp, [rel tss_rsp0]
        and rsp, ~0xF
        sub rsp, 40
        mov rax, [rel save_rip]
        mov [rsp + 0], rax
        mov qword [rsp + 8], 0x23
        mov rax, [rel save_rflags]
        mov [rsp + 16], rax
        mov rax, [rel save_rsp]
        mov [rsp + 24], rax
        mov qword [rsp + 32], 0x1B

        mov rax, cr3
        mov cr3, rax
        iretq

; void usermode_resume_task(void *task);
; struct process layout used below:
; user_rsp +24, user_rip +32, user_rflags +40, user_rax +48, user_regs +56
usermode_resume_task:
        mov r14, rdi
        mov rax, [r14 + 32]
        mov rcx, [r14 + 24]
        mov rdx, [r14 + 40]

        mov rsp, [rel tss_rsp0]
        and rsp, ~0xF
        sub rsp, 40
        mov [rsp + 0], rax
        mov qword [rsp + 8], 0x23
        mov [rsp + 16], rdx
        mov [rsp + 24], rcx
        mov qword [rsp + 32], 0x1B

        mov rax, [r14 + 48]
        mov rbx, [r14 + 64]
        mov rdx, [r14 + 80]
        mov rsi, [r14 + 88]
        mov rdi, [r14 + 96]
        mov rbp, [r14 + 104]
        mov r8,  [r14 + 112]
        mov r9,  [r14 + 120]
        mov r10, [r14 + 128]
        mov r12, [r14 + 144]
        mov r13, [r14 + 152]
        mov r15, [r14 + 168]
        mov r14, [r14 + 160]
        iretq

; void usermode_resume(uint64_t rip, uint64_t rsp, uint64_t rflags, uint64_t retval);
usermode_resume:
        mov rax, rcx
        mov r11, rdx
        mov rsp, [rel tss_rsp0]
        and rsp, ~0xF
        sub rsp, 40
        mov [rsp + 0], rdi
        mov qword [rsp + 8], 0x23
        mov [rsp + 16], r11
        mov [rsp + 24], rsi
        mov qword [rsp + 32], 0x1B
        iretq

section .bss
align 8
sys_save_rax:
        resq 1
sys_save_rdi:
        resq 1
sys_save_rsi:
        resq 1
sys_save_rdx:
        resq 1
sys_save_r10:
        resq 1
sys_save_r8:
        resq 1
sys_save_r9:
        resq 1
sys_save_r11:
        resq 1
sys_save_rcx:
        resq 1
sys_save_rsp:
        resq 1
sys_return_rax:
        resq 1
save_rip:
        resq 1
save_rsp:
        resq 1
save_argc:
        resq 1
save_argv:
        resq 1
save_envp:
        resq 1
save_rflags:
        resq 1
