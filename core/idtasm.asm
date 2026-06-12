bits 64

global idt_load
global pic_remap_asm
global irq0_handler_asm
global irq1_handler_asm

extern keyboard_handler

pic_remap_asm:
        mov al, 0xFF
        out 0x21, al
        out 0xA1, al

        mov al, 0x11
        out 0x20, al
        call .io_wait
        in al, 0x21
        call .io_wait

        mov al, 0x11
        out 0xA0, al
        call .io_wait
        in al, 0xA1
        call .io_wait

        mov al, 0x20
        out 0x21, al
        call .io_wait
        mov al, 0x28
        out 0xA1, al
        call .io_wait

        mov al, 0x04
        out 0x21, al
        call .io_wait
        mov al, 0x02
        out 0xA1, al
        call .io_wait

        mov al, 0x01
        out 0x21, al
        call .io_wait
        out 0xA1, al
        call .io_wait

        mov al, 0x20
        out 0x20, al
        call .io_wait
        out 0xA0, al
        call .io_wait

        mov al, 0xFE
        out 0x21, al
        mov al, 0xFF
        out 0xA1, al
        ret

.io_wait:
        push rax
        mov al, 0
        out 0x80, al
        pop rax
        ret

idt_load:
        lidt [rdi]
        sti
        ret

%macro PUSH_ALL 0
        push rax
        push rbx
        push rcx
        push rdx
        push rsi
        push rdi
        push rbp
        push r8
        push r9
        push r10
        push r11
        push r12
        push r13
        push r14
        push r15
%endmacro

%macro POP_ALL 0
        pop r15
        pop r14
        pop r13
        pop r12
        pop r11
        pop r10
        pop r9
        pop r8
        pop rbp
        pop rdi
        pop rsi
        pop rdx
        pop rcx
        pop rbx
        pop rax
%endmacro

irq0_handler_asm:
        PUSH_ALL
        mov al, 0x20
        out 0x20, al
        POP_ALL
        iretq

irq1_handler_asm:
        PUSH_ALL
        sub rsp, 8
        call keyboard_handler
        add rsp, 8
        mov al, 0x20
        out 0x20, al
        POP_ALL
        iretq
