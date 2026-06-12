ENTRY(_start)

SECTIONS
{
        /* Загружаем ядро по стандартному адресу 1 МБ */
        . = 1M;

        /* Секция кода */
        .text :
        {
                /* Важно: заголовок multiboot_header обязан быть в самом начале */
                KEEP(*(.multiboot_header))
                *(.text)
        }

        /* Секция данных (только для чтения) */
        .rodata :
        {
                *(.rodata)
        }

        /* Секция инициализированных данных */
        .data :
        {
                *(.data)
        }

        /* Секция неинициализированных данных */
        .bss :
        {
                *(.common)
                *(.bss)
        }

        PROVIDE(kernel_end = .);

        /* Стек ядра — в конце образа, отдельно от остального .bss */
        . = ALIGN(16);
        PROVIDE(stack_bottom = .);
        . += 64K;
        PROVIDE(stack_top = .);
}
