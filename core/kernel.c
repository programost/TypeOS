
#include "include/types.h"
#include "include/vga.h"
#include "include/keyboard.h"
#include "include/idt.h"

void kmain(void) {
    vga_init();
    vga_write("[INIT]: VGA: OK", VGA_COLOR_GREEN);
    vga_putchar('\n');
    idt_init();
    vga_write("[INIT]: IDT: OK", VGA_COLOR_GREEN);
    vga_putchar('\n');
    keyboard_init();
    vga_write("[INIT]: Keyboard: OK", VGA_COLOR_GREEN);
    vga_putchar('\n');
    keyboard_getchar();
    while (1) {
        __asm__ volatile("hlt");
    }
}