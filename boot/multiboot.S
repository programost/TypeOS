; Настройки для 32-битного режима, в котором GRUB передает управление
bits 32

section .multiboot_header
header_start:
    ; Магическое число Multiboot2 (0xE85250D6)
    dd 0xE85250D6
    ; Архитектура (0 - для i386/x86_64)
    dd 0
    ; Длина всего заголовка
    dd header_end - header_start
    ; Контрольная сумма
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start))
    
    ; Завершающий тег
    dw 0
    dw 0
    dd 8
header_end:

section .bss
align 4096
; Таблицы страниц (требуются для Long Mode)
pml4_table:
    resb 4096
pdp_table:
    resb 4096
pd_table:
    resb 4096

stack_bottom:
    resb 16384 ; Стек размером 16 КБ
stack_top:

section .text
global _start   ; ИЗМЕНЕНО: теперь линкер увидит эту точку входа
_start:         ; ИЗМЕНЕНО
    ; Инициализация стека
    mov esp, stack_top

    ; 1. Проверка поддержки CPU режима Long Mode (64-bit)
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode

    mov eax, 0x80000001
    cpuid
    bt edx, 29
    jnc .no_long_mode

    ; 2. Включение PAE (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    ; 3. Создание базовых таблиц страниц (Identity Mapping для 1 ГБ)
    ; Настройка PML4
    mov eax, pdp_table
    or eax, 3 ; Present + Read/Write
    mov [pml4_table], eax

    ; Настройка PDP
    mov eax, pd_table
    or eax, 3 ; Present + Read/Write
    mov [pdp_table], eax

    ; Настройка PD (отображаем первые 2 МБ)
    mov eax, 0 | 0x83 ; Present + Read/Write + Page Size (2MB)
    mov [pd_table], eax

    ; Загрузка адреса PML4 в регистр CR3
    mov eax, pml4_table
    mov cr3, eax

    ; 4. Включение Long Mode (через EFER MSR)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8 ; LME (Long Mode Enable)
    wrmsr

    ; 5. Включение страничной адресации (Paging)
    mov eax, cr0
    or eax, 1 << 31
    or eax, 1 << 16 ; WP
    mov cr0, eax

    ; 6. Переход в 64-битный режим с использованием GDT
    lgdt [gdt64.pointer]
    jmp gdt64.code_seg:long_mode_start

.no_long_mode:
    hlt
    jmp .no_long_mode

bits 64
long_mode_start:
    ; Очистка неиспользуемых сегментных регистров
    mov ax, 0
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Вызов вашего 64-битного кода на Си
    extern kmain   ; ИЗМЕНЕНО: под имя нашей функции в kernel.c
    call kmain    ; ИЗМЕНЕНО

    ; Остановка процессора, если kernel_main завершился
    hlt
    jmp $

; Глобальная таблица дескрипторов (GDT)
section .rodata
gdt64:
    dq 0 ; Нулевой дескриптор
.code_seg: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; Кодовый сегмент 64-bit
.pointer:
    dw $ - gdt64 - 1
    dq gdt64 
