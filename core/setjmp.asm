bits 64

global setjmp_asm
global longjmp_asm

; int setjmp_asm(jmp_buf *buf)
; buf: rbx, rbp, r12, r13, r14, r15, rsp, rip
setjmp_asm:
        mov [rdi + 0],  rbx
        mov [rdi + 8],  rbp
        mov [rdi + 16], r12
        mov [rdi + 24], r13
        mov [rdi + 32], r14
        mov [rdi + 40], r15
        mov [rdi + 48], rsp
        lea rax, [rel .ret]
        mov [rdi + 56], rax
        xor eax, eax
.ret:
        ret

; void longjmp_asm(jmp_buf *buf, int val)
longjmp_asm:
        mov rbx, [rdi + 0]
        mov rbp, [rdi + 8]
        mov r12, [rdi + 16]
        mov r13, [rdi + 24]
        mov r14, [rdi + 32]
        mov r15, [rdi + 40]
        mov rsp, [rdi + 48]
        mov rax, rsi
        test rax, rax
        jnz .go
        inc rax
.go:
        jmp qword [rdi + 56]
