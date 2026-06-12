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
        ; Контрольная сумма (GRUB: -(magic + arch + total_length))
        dd -(0xE85250D6 + 0 + (header_end - header_start))

        ; Information request tag: memory map (type 6)
        align 8, db 0
        dw 1                          ; MULTIBOOT_TAG_TYPE_INFORMATION_REQUEST
        dw 0
        dd 12
        dd 6                          ; MULTIBOOT_INFO_REQUEST_MEMORY_MAP

        ; End tag (each tag must start on an 8-byte boundary)
        align 8, db 0
        dw 0
        dw 0
        dd 8
header_end:

section .bss
align 4096
global pml4_table
global pdp_table
global pd_table
pml4_table:
        resb 4096
pdp_table:
        resb 4096
pd_table:
        resb 4096

align 8
mb2_magic:
        resq 1
mb2_info:
        resq 1

section .text
global _start   ; ИЗМЕНЕНО: теперь линкер увидит эту точку входа
extern stack_top

_start:
        ; GRUB передаёт magic в EAX и указатель на multiboot info в EBX
        mov [mb2_magic], eax
        mov [mb2_info], ebx

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

        ; 3. Identity mapping первых 256 МБ (128 x 2 МБ huge page)
        mov eax, pdp_table
        or eax, 3
        mov [pml4_table], eax

        mov eax, pd_table
        or eax, 3
        mov [pdp_table], eax

        xor ecx, ecx
.identity_map_loop:
        mov eax, ecx
        shl eax, 21
        or eax, 0x83
        mov [pd_table + ecx * 8], eax
        inc ecx
        cmp ecx, 128
        jb .identity_map_loop

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

        ; Выравнивание стека по 16 байт (требование AMD64 ABI)
        mov rsp, stack_top
        and rsp, ~0xF

        extern kmain
        mov rdi, [rel mb2_magic]
        mov rsi, [rel mb2_info]
        call kmain

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
