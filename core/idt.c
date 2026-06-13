#include "include/idt.h"
#include "include/drivers.h"
#include "include/types.h"

#define PIT_HZ       100U
#define PIT_BASE_HZ  1193182U

// Всего в x86 может быть 256 прерываний
__attribute__((aligned(0x10))) 
static struct idt_entry idt[256];
static struct idt_ptr idtr;

extern void idt_load(struct idt_ptr* ptr);
extern void pic_remap_asm(void);
extern void pf_handler_asm(void);

static void
pit_init (void)
{
        uint16_t divisor = (uint16_t) (PIT_BASE_HZ / PIT_HZ);

        outb (0x43, 0x36);
        outb (0x40, (uint8_t) (divisor & 0xFFU));
        outb (0x40, (uint8_t) (divisor >> 8));
}

void idt_set_descriptor(uint8_t vector, uint64_t isr, uint8_t flags) {
        struct idt_entry* descriptor = &idt[vector];

        descriptor->isr_low   = (uint16_t)(isr & 0xFFFF);
        descriptor->kernel_cs = 0x08; // Селектор сегмента кода вашего GDT (обычно 0x08)
        descriptor->ist       = 0;
        descriptor->attributes= flags;
        descriptor->isr_mid   = (uint16_t)((isr >> 16) & 0xFFFF);
        descriptor->isr_high  = (uint32_t)((isr >> 32) & 0xFFFFFFFF);
        descriptor->reserved  = 0;
}

void idt_set_handler(uint8_t num, void* handler) {
        idt_set_descriptor(num, (uint64_t)handler, 0x8E);
}

void idt_init(void) {
        // Настраиваем указатель IDTR
        idtr.base = (uint64_t)&idt;
        idtr.limit = (uint16_t)(sizeof(struct idt_entry) * 256) - 1;
 
        // Настраиваем PIC, чтобы прерывания клавиатуры не вызывали ошибки CPU
        pic_remap_asm();
        pit_init();

        idt_set_handler(0x0E, pf_handler_asm);
        idt_set_handler(0x20, irq0_handler_asm);
        idt_set_handler(0x21, irq1_handler_asm);
        idt_set_handler(0x09, irq1_handler_asm);
 
        // Загружаем IDT в процессор и включаем прерывания через ассемблер
        idt_load(&idtr);
}
