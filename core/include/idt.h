#ifndef IDT_H
#define IDT_H

#include "types.h"

// Структура дескриптора прерывания x86-64 (16 байт)
struct idt_entry {
    uint16_t isr_low;   // Младшие 16 бит адреса обработчика
    uint16_t kernel_cs; // Селектор сегмента кода ядра (обычно 0x08)
    uint8_t  ist;       // Interrupt Stack Table (обычно 0)
    uint8_t  attributes;// Флаги типа и прав доступа (0x8E для прерываний)
    uint16_t isr_mid;   // Средние 16 бит адреса обработчика
    uint32_t isr_high;  // Старшие 32 бита адреса обработчика
    uint32_t reserved;  // Всегда 0
} __attribute__((packed));

// Структура указателя IDT для инструкции lidt
struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Инициализация IDT и контроллера прерываний (PIC)
void idt_init(void);

// Функция для регистрации конкретного обработчика
void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags);

// Объявление ассемблерных обработчиков прерываний
extern void irq0_handler_asm(void); // Таймер (IRQ 0)
extern void irq1_handler_asm(void); // Клавиатура (IRQ 1)

#endif
