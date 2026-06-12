#include "keyboard.h"

// Флаг состояния клавиши Shift
static bool shift_pressed = false;

// Простейший кольцевой буфер для хранения введенных символов
#define BUFFER_SIZE 256
static char keyboard_buffer[BUFFER_SIZE];
static int buffer_head = 0;
static int buffer_tail = 0;

// Функции для работы с портами ввода-вывода (вводятся через ассемблер)
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Таблица перевода скан-кодов Set 1 в ASCII (без Shift)
static const char kbd_us_map[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',   0, ' ',
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0, '-',   0,   0,   0, '+',   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

// Таблица перевода скан-кодов Set 1 в ASCII (с зажатым Shift)
static const char kbd_us_shift_map[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0, '*',   0, ' ',
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0, '-',   0,   0,   0, '+',   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

void keyboard_init(void) {
    // Очищаем буфер при старте
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = false;
}

void keyboard_handler(void) {
    // Шаг 1: Проверяем, есть ли данные в контроллере клавиатуры
    if ((inb(KEYBOARD_STAT_PORT) & 0x01) == 0) {
        return; // Данных нет, выходим
    }

    // Шаг 2: Считываем скан-код из порта данных
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    // Шаг 3: Обрабатываем нажатие/отпускание Shift
    if (scancode == LSHIFT_MAKE || scancode == RSHIFT_MAKE) {
        shift_pressed = true;
        return;
    }
    if (scancode == LSHIFT_BREAK || scancode == RSHIFT_BREAK) {
        shift_pressed = false;
        return;
    }

    // Игнорируем остальные break-коды (у них установлен 7-й бит)
    if (scancode & 0x80) {
        return;
    }

    // Шаг 4: Конвертируем скан-код в ASCII символ
    char ascii = shift_pressed ? kbd_us_shift_map[scancode] : kbd_us_map[scancode];

    // Если символ валидный, кладем его в кольцевой буфер
    if (ascii != 0) {
        int next = (buffer_head + 1) % BUFFER_SIZE;
        if (next != buffer_tail) { // Проверяем, что буфер не переполнен
            keyboard_buffer[buffer_head] = ascii;
            buffer_head = next;
        }
    }
}

char keyboard_getchar(void) {
    // Ждем, пока в буфере появится хотя бы один символ (блокирующая функция)
    while (buffer_head == buffer_tail) {
        // Здесь можно вставить инструкцию hlt, чтобы не загружать процессор:
        __asm__ volatile("hlt");
    }

    char ascii = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
    return ascii;
}
