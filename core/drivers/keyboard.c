#include <keyboard.h>
#include <types.h>
#include <drivers.h>
#include <vga.h>
#include <sched.h>

static bool shift_pressed = false;
static bool extended_scancode = false;

#define BUFFER_SIZE 256
static int keyboard_buffer[BUFFER_SIZE];
static volatile int buffer_head = 0;
static volatile int buffer_tail = 0;

static const char kbd_us_map[128] = {
        0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
        0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',   0, ' ',
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0, '-',   0,   0,   0, '+',   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static const char kbd_us_shift_map[128] = {
        0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        0,  'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
  '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0, '*',   0, ' ',
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0, '-',   0,   0,   0, '+',   0,   0,   0,   0,   0,   0,   0,   0,
        0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0
};

static int i8042_wait_input_timeout(int limit) {
        while ((inb(KEYBOARD_STAT_PORT) & 0x02) && --limit > 0) {
                io_wait();
        }
        return limit > 0;
}

static int i8042_wait_output_timeout(int limit) {
        while (((inb(KEYBOARD_STAT_PORT) & 0x01) == 0) && --limit > 0) {
                io_wait();
        }
        return limit > 0;
}

static void keyboard_drain(void) {
        while (inb(KEYBOARD_STAT_PORT) & 0x01) {
                (void)inb(KEYBOARD_DATA_PORT);
        }
}

static void i8042_write_command(uint8_t value) {
        if (!i8042_wait_input_timeout(100000)) {
                return;
        }
        outb(KEYBOARD_STAT_PORT, value);
}

static void i8042_write_data(uint8_t value) {
        if (!i8042_wait_input_timeout(100000)) {
                return;
        }
        outb(KEYBOARD_DATA_PORT, value);
}

static void keyboard_write(uint8_t value) {
        i8042_write_command(0xD2);
        i8042_write_data(value);
}

static void i8042_configure(void) {
        keyboard_drain();

        i8042_write_command(0x20);
        if (!i8042_wait_output_timeout(100000)) {
                return;
        }

        uint8_t config = inb(KEYBOARD_DATA_PORT);
        config |= 0x01;
        config |= 0x40;
        config &= (uint8_t)~0x10;

        i8042_write_command(0x60);
        i8042_write_data(config);

        keyboard_write(0xF4);
}

static void keyboard_push(int key) {
        int next = (buffer_head + 1) % BUFFER_SIZE;
        if (next == buffer_tail) {
                return;
        }
        keyboard_buffer[buffer_head] = key;
        buffer_head = next;
        sched_wake_all ();
}

static int keyboard_handle_extended(uint8_t scancode) {
        if (scancode & 0x80) {
                return 0;
        }

        switch (scancode) {
                case 0x4B: keyboard_push(KEY_LEFT);  return 1;
                case 0x4D: keyboard_push(KEY_RIGHT); return 1;
                case 0x48: keyboard_push(KEY_UP);    return 1;
                case 0x50: keyboard_push(KEY_DOWN);  return 1;
                default:   return 0;
        }
}

void keyboard_init(void) {
        buffer_head = 0;
        buffer_tail = 0;
        shift_pressed = false;
        extended_scancode = false;
        i8042_configure();
}

static void keyboard_handle_scancode(uint8_t scancode) {
        if (scancode == 0xE0) {
                extended_scancode = true;
                return;
        }

        if (scancode == 0xE1) {
                extended_scancode = false;
                return;
        }

        if (extended_scancode) {
                extended_scancode = false;
                (void)keyboard_handle_extended(scancode);
                return;
        }

        if (scancode == LSHIFT_MAKE || scancode == RSHIFT_MAKE) {
                shift_pressed = true;
                return;
        }
        if (scancode == LSHIFT_BREAK || scancode == RSHIFT_BREAK) {
                shift_pressed = false;
                return;
        }

        if (scancode & 0x80) {
                return;
        }

        char ascii = shift_pressed ? kbd_us_shift_map[scancode] : kbd_us_map[scancode];
        if (ascii == 0) {
                return;
        }

        keyboard_push((unsigned char)ascii);
}

void keyboard_poll(void) {
        while (inb(KEYBOARD_STAT_PORT) & 0x01) {
                keyboard_handle_scancode(inb(KEYBOARD_DATA_PORT));
        }
}

void keyboard_handler(void) {
        keyboard_poll();
}

int keyboard_haschar(void) {
        keyboard_poll();
        return buffer_head != buffer_tail;
}

int keyboard_getchar(void) {
        for (;;) {
                if (keyboard_haschar()) {
                        break;
                }
                sched_block_current();
        }

        int key = keyboard_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % BUFFER_SIZE;
        return key;
}

static char str[1024];

static void input_redraw(int line_x, int line_y, const char* s, int len, int cursor, int clear_to) {
        vga_set_cursor(line_x, line_y);
        for (int i = 0; i < len; i++) {
                vga_putchar(s[i]);
        }
        for (int i = len; i < clear_to; i++) {
                vga_putchar(' ');
        }
        vga_set_cursor(line_x + cursor, line_y);
}

char* get_string(void) {
        int len = 0;
        int cursor = 0;
        int max_drawn = 0;
        int line_x = 0;
        int line_y = 0;

        vga_get_cursor(&line_x, &line_y);

        while (1) {
                int c = keyboard_getchar();

                if (c == '\n') {
                        str[len] = '\0';
                        vga_putchar('\n');
                        return str;
                }

                if (c == KEY_LEFT) {
                        if (cursor > 0) {
                                cursor--;
                                vga_set_cursor(line_x + cursor, line_y);
                        }
                        continue;
                }

                if (c == KEY_RIGHT) {
                        if (cursor < len) {
                                cursor++;
                                vga_set_cursor(line_x + cursor, line_y);
                        }
                        continue;
                }

                if (c == '\b') {
                        if (cursor > 0) {
                                for (int i = cursor - 1; i < len; i++) {
                                        str[i] = str[i + 1];
                                }
                                len--;
                                cursor--;
                                input_redraw(line_x, line_y, str, len, cursor, max_drawn);
                        }
                        continue;
                }

                if (c >= 32 && c < 127 && len < (int)sizeof(str) - 1) {
                        for (int i = len; i > cursor; i--) {
                                str[i] = str[i - 1];
                        }
                        str[cursor] = (char)c;
                        len++;
                        cursor++;
                        if (len > max_drawn) {
                                max_drawn = len;
                        }
                        input_redraw(line_x, line_y, str, len, cursor, max_drawn);
                }
        }
}