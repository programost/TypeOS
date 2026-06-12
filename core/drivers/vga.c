#include "vga.h"
#include "types.h"
#include "drivers.h"
#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((unsigned short*)0xB8000)

// Глобальное состояние драйвера
static int cursor_x = 0;
static int cursor_y = 0;
static unsigned char current_color = 0x07; // По умолчанию: белый на черном

// Обновление аппаратного (мигающего) курсора на экране
static void vga_update_hardware_cursor(void) {
    unsigned short position = cursor_y * VGA_WIDTH + cursor_x;
    
    // Передаем нижний байт позиции контроллеру CRT
    outb(0x3D4, 0x0F);
    outb(0x3D5, (unsigned char)(position & 0xFF));
    // Передаем верхний байт позиции
    outb(0x3D4, 0x0E);
    outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}

// Функция прокрутки экрана вверх на одну строку
static void vga_scroll(void) {
    // Сдвигаем строки с 1 по 24 на место строк 0-23
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }
    
    // Очищаем самую нижнюю строку пробелами
    unsigned short empty_cell = ' ' | (current_color << 8);
    for (int x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = empty_cell;
    }
}

// Установка базового цвета (текст / фон)
void vga_setcolorbase(vga_color_t color) {
    // Оставляем черный фон (0), меняем только цвет текста
    current_color = (unsigned char)color | (VGA_COLOR_BLACK << 4);
}

// Инициализация: очистка экрана и сброс курсора
void vga_init(void) {
    cursor_x = 0;
    cursor_y = 0;
    vga_setcolorbase(VGA_COLOR_WHITE); // Цвет по умолчанию
    
    unsigned short empty_cell = ' ' | (current_color << 8);
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = empty_cell;
    }
    vga_update_hardware_cursor();
}

// Вывод одного символа
void vga_putchar(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7; // Табуляция на 8 позиций
    } else {
        int index = cursor_y * VGA_WIDTH + cursor_x;
        VGA_MEMORY[index] = c | (current_color << 8);
        cursor_x++;
    }

    // Если ушли за правый край — перенос строки
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }

    // Если ушли за нижний край — скроллим
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll();
        cursor_y = VGA_HEIGHT - 1;
    }

    vga_update_hardware_cursor();
}

void vga_write(const char* str, vga_color_t color) {
    unsigned char old_color = current_color;
    current_color = (unsigned char)color | (VGA_COLOR_BLACK << 4);
    while (*str) {
        vga_putchar(*str);
        str++;
    }
    current_color = old_color;
}
