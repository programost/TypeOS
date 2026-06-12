#ifndef IDT_H
#define IDT_H

#include "types.h"

struct idt_entry {
        uint16_t isr_low;
        uint16_t kernel_cs;
        uint8_t  ist;
        uint8_t  attributes;
        uint16_t isr_mid;
        uint32_t isr_high;
        uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
        uint16_t limit;
        uint64_t base;
} __attribute__((packed));

void idt_init(void);

void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags);

void idt_set_handler(uint8_t num, void* handler);

extern void irq0_handler_asm(void);
extern void irq1_handler_asm(void);

#endif
