#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

// Порты контроллера 8042 PS/2
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STAT_PORT 0x64

// Макросы для проверки состояния Shift
#define LSHIFT_MAKE  0x2A
#define LSHIFT_BREAK 0xAA
#define RSHIFT_MAKE  0x36
#define RSHIFT_BREAK 0xB6

// Инициализация клавиатуры (если требуется)
void keyboard_init(void);

// Функция, которую вы должны вызывать из вашего обработчика прерывания (IRQ 1)
void keyboard_handler(void);

// Функция для чтения символа из буфера ядра вашими программами/оболочкой
char keyboard_getchar(void);

#endif // KEYBOARD_H
