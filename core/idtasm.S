bits 64

global idt_load
global irq0_handler_asm
global irq1_handler_asm

extern keyboard_handler

; Функция загрузки IDT (Аргумент приходит в RDI по стандарту System V AMD64 ABI)
idt_load:
    lidt [rdi]
    sti          ; Разрешаем прерывания процессора
    ret

; Макросы для сохранения и восстановления контекста x86-64
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

; Обработчик IRQ 0 (Таймер)
irq0_handler_asm:
    PUSH_ALL
    
    ; Отправляем EOI (0x20) в ведущий PIC (порт 0x20)
    mov al, 0x20
    out 0x20, al
    
    POP_ALL
    iretq

; Обработчик IRQ 1 (Клавиатура)
irq1_handler_asm:
    PUSH_ALL
    
    ; Вызываем сишную функцию обработки скан-кода
    call keyboard_handler
    
    ; Отправляем EOI (0x20) в ведущий PIC (порт 0x20)
    mov al, 0x20
    out 0x20, al
    
    POP_ALL
    iretq
