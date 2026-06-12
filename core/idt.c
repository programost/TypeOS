#include "include/idt.h"
#include "include/drivers.h"
#include "include/types.h"

// Всего в x86 может быть 256 прерываний
__attribute__((aligned(0x10))) 
static struct idt_entry idt[256];
static struct idt_ptr idtr;

// Внешняя функция из ассемблера для загрузки IDT
extern void idt_load(struct idt_ptr* ptr);

// Функция инициализации и сдвига векторов PIC
static void pic_remap(void) {
    // ICW1 - инициализация
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    // ICW2 - базовые векторы (сдвигаем IRQ 0-15 на векторы 0x20-0x2F)
    outb(0x21, 0x20); // Master PIC -> 0x20
    outb(0xA1, 0x28); // Slave PIC  -> 0x28
    
    // ICW3 - связь между PIC
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    
    // ICW4 - режим 8086
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    // Маскирование: разрешаем только IRQ 0 (таймер) и IRQ 1 (клавиатура)
    // 0xFC = 11111100b (0 - включено, 1 - выключено)
    outb(0x21, 0xFC); 
    outb(0xA1, 0xFF); // Выключаем все прерывания на Slave PIC
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

void idt_init(void) {
    // Настраиваем указатель IDTR
    idtr.base = (uint64_t)&idt;
    idtr.limit = (uint16_t)(sizeof(struct idt_entry) * 256) - 1;
 
    // Настраиваем PIC, чтобы прерывания клавиатуры не вызывали ошибки CPU
    pic_remap();
 
    // Регистрируем обработчики прерываний
    // Флаг 0x8E: Прерывание присутствует (0x80), кольцо 0 (0x00), тип: 64-bit Interrupt Gate (0x0E)
    idt_set_descriptor(0x20, (uint64_t)irq0_handler_asm, 0x8E); // IRQ 0 (Вектор 32)
    idt_set_descriptor(0x21, (uint64_t)irq1_handler_asm, 0x8E); // IRQ 1 (Вектор 33)
 
    // Загружаем IDT в процессор и включаем прерывания через ассемблер
    idt_load(&idtr);
}
