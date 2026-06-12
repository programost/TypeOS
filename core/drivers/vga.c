#include <vga.h>
#include <types.h>
#include <drivers.h>
#include <stdarg.h>

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEMORY ((unsigned short*)0xB8000)

#define KPRINTF_NUM_BUF 66

static char kprintf_num_buf[KPRINTF_NUM_BUF];

typedef struct {
        int width;
        int precision;
        int left;
        int zero_pad;
        int alt;
        int uppercase;
        char length;
} fmt_flags_t;

static int cursor_x = 0;
static int cursor_y = 0;
static unsigned char current_color = 0x8f;

static void vga_update_hardware_cursor(void) {
        unsigned short position = cursor_y * VGA_WIDTH + cursor_x;

        outb(0x3D4, 0x0F);
        outb(0x3D5, (unsigned char)(position & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (unsigned char)((position >> 8) & 0xFF));
}

static void vga_scroll(void) {
        for (int y = 1; y < VGA_HEIGHT; y++) {
                for (int x = 0; x < VGA_WIDTH; x++) {
                        VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
                }
        }

        unsigned short empty_cell = ' ' | (current_color << 8);
        for (int x = 0; x < VGA_WIDTH; x++) {
                VGA_MEMORY[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = empty_cell;
        }
}

static int parse_digits(const char** fmt) {
        int value = 0;
        while (**fmt >= '0' && **fmt <= '9') {
                value = value * 10 + (**fmt - '0');
                (*fmt)++;
        }
        return value;
}

static uint64_t abs_s64(int64_t value, char* sign) {
        if (value < 0) {
                *sign = '-';
                return (uint64_t)(-(value + 1)) + 1;
        }
        *sign = 0;
        return (uint64_t)value;
}

static int u64_to_str(char* buf, uint64_t value, int base, int uppercase) {
        static const char digits_lower[] = "0123456789abcdef";
        static const char digits_upper[] = "0123456789ABCDEF";
        const char* digits = uppercase ? digits_upper : digits_lower;
        int len = 0;

        if (value == 0) {
                buf[len++] = '0';
                return len;
        }

        while (value > 0) {
                buf[len++] = digits[value % (unsigned)base];
                value /= (unsigned)base;
        }
        return len;
}

static void reverse_buf(char* buf, int len) {
        for (int i = 0; i < len / 2; i++) {
                char tmp = buf[i];
                buf[i] = buf[len - 1 - i];
                buf[len - 1 - i] = tmp;
        }
}

static void kputs_padded(const char* str, int len, const fmt_flags_t* flags, char pad_char) {
        int pad_count = flags->width - len;
        if (pad_count < 0) {
                pad_count = 0;
        }

        if (!flags->left) {
                for (int i = 0; i < pad_count; i++) {
                        vga_putchar(pad_char);
                }
        }

        for (int i = 0; i < len; i++) {
                vga_putchar(str[i]);
        }

        if (flags->left) {
                for (int i = 0; i < pad_count; i++) {
                        vga_putchar(' ');
                }
        }
}

static void format_number(uint64_t value, char sign, int base, fmt_flags_t* flags) {
        int len = u64_to_str(kprintf_num_buf, value, base, flags->uppercase);
        reverse_buf(kprintf_num_buf, len);

        int precision = flags->precision;
        if (precision < 0) {
                precision = 1;
        }

        if (value == 0 && precision == 0) {
                len = 0;
        } else if (len < precision) {
                int shift = precision - len;
                for (int i = len - 1; i >= 0; i--) {
                        kprintf_num_buf[i + shift] = kprintf_num_buf[i];
                }
                for (int i = 0; i < shift; i++) {
                        kprintf_num_buf[i] = '0';
                }
                len = precision;
        }

        char prefix[3];
        int prefix_len = 0;
        if (flags->alt && value != 0) {
                if (base == 16) {
                        prefix[prefix_len++] = '0';
                        prefix[prefix_len++] = flags->uppercase ? 'X' : 'x';
                } else if (base == 8) {
                        prefix[prefix_len++] = '0';
                }
        }

        char pad_char = ' ';
        if (flags->zero_pad && !flags->left && flags->precision < 0) {
                pad_char = '0';
        }

        fmt_flags_t out_flags = *flags;
        out_flags.width -= prefix_len + (sign ? 1 : 0);

        if (pad_char == '0' && sign) {
                vga_putchar(sign);
                sign = 0;
        }

        if (pad_char == '0') {
                for (int i = 0; i < prefix_len; i++) {
                        vga_putchar(prefix[i]);
                }
        }

        if (sign && pad_char == ' ') {
                char sign_buf[1] = { sign };
                kputs_padded(sign_buf, 1, &out_flags, ' ');
                out_flags.width = flags->width - prefix_len - 1;
        } else if (sign) {
                vga_putchar(sign);
        }

        if (pad_char == ' ') {
                for (int i = 0; i < prefix_len; i++) {
                        vga_putchar(prefix[i]);
                }
        }

        kputs_padded(kprintf_num_buf, len, &out_flags, pad_char);
}

static void format_string(const char* str, fmt_flags_t* flags) {
        if (!str) {
                str = "(null)";
        }

        int len = 0;
        while (str[len]) {
                len++;
        }

        if (flags->precision >= 0 && len > flags->precision) {
                len = flags->precision;
        }

        kputs_padded(str, len, flags, ' ');
}

static void format_char(int ch, fmt_flags_t* flags) {
        char buf[1] = { (char)ch };
        kputs_padded(buf, 1, flags, ' ');
}

static void format_pointer(uintptr_t value, fmt_flags_t* flags) {
        fmt_flags_t ptr_flags = *flags;
        ptr_flags.alt = 1;
        ptr_flags.uppercase = 0;
        if (ptr_flags.precision < 0) {
                ptr_flags.precision = sizeof(uintptr_t) * 2;
        }
        format_number(value, 0, 16, &ptr_flags);
}

static void kvprintf(const char* fmt, va_list args) {
        while (*fmt) {
                if (*fmt != '%') {
                        vga_putchar(*fmt++);
                        continue;
                }

                fmt++;
                if (*fmt == '%') {
                        vga_putchar('%');
                        fmt++;
                        continue;
                }

                fmt_flags_t flags = {
                        .width = 0,
                        .precision = -1,
                        .left = 0,
                        .zero_pad = 0,
                        .alt = 0,
                        .uppercase = 0,
                        .length = 0,
                };

                while (1) {
                        if (*fmt == '-') {
                                flags.left = 1;
                                fmt++;
                        } else if (*fmt == '0') {
                                flags.zero_pad = 1;
                                fmt++;
                        } else if (*fmt == '#') {
                                flags.alt = 1;
                                fmt++;
                        } else {
                                break;
                        }
                }

                if (*fmt == '*') {
                        flags.width = va_arg(args, int);
                        fmt++;
                } else if (*fmt >= '0' && *fmt <= '9') {
                        flags.width = parse_digits(&fmt);
                }

                if (*fmt == '.') {
                        fmt++;
                        flags.precision = 0;
                        if (*fmt == '*') {
                                flags.precision = va_arg(args, int);
                                fmt++;
                        } else if (*fmt >= '0' && *fmt <= '9') {
                                flags.precision = parse_digits(&fmt);
                        }
                }

                if (*fmt == 'h') {
                        flags.length = 'h';
                        fmt++;
                        if (*fmt == 'h') {
                                flags.length = 'H';
                                fmt++;
                        }
                } else if (*fmt == 'l') {
                        flags.length = 'l';
                        fmt++;
                        if (*fmt == 'l') {
                                flags.length = 'L';
                                fmt++;
                        }
                } else if (*fmt == 'z') {
                        flags.length = 'z';
                        fmt++;
                }

                char spec = *fmt++;
                uint64_t uvalue = 0;
                int64_t svalue = 0;
                char sign = 0;

                switch (spec) {
                        case 'd':
                        case 'i':
                                if (flags.length == 'H') {
                                        svalue = (int8_t)va_arg(args, int);
                                } else if (flags.length == 'h') {
                                        svalue = (int16_t)va_arg(args, int);
                                } else if (flags.length == 'L') {
                                        svalue = va_arg(args, long long);
                                } else if (flags.length == 'l') {
                                        svalue = va_arg(args, long);
                                } else if (flags.length == 'z') {
                                        svalue = (int64_t)va_arg(args, size_t);
                                } else {
                                        svalue = va_arg(args, int);
                                }
                                uvalue = abs_s64(svalue, &sign);
                                format_number(uvalue, sign, 10, &flags);
                                break;

                        case 'u':
                                if (flags.length == 'H') {
                                        uvalue = (uint8_t)va_arg(args, unsigned int);
                                } else if (flags.length == 'h') {
                                        uvalue = (uint16_t)va_arg(args, unsigned int);
                                } else if (flags.length == 'L') {
                                        uvalue = va_arg(args, unsigned long long);
                                } else if (flags.length == 'l') {
                                        uvalue = va_arg(args, unsigned long);
                                } else if (flags.length == 'z') {
                                        uvalue = va_arg(args, size_t);
                                } else {
                                        uvalue = va_arg(args, unsigned int);
                                }
                                format_number(uvalue, 0, 10, &flags);
                                break;

                        case 'x':
                        case 'X':
                                flags.uppercase = (spec == 'X');
                                if (flags.length == 'H') {
                                        uvalue = (uint8_t)va_arg(args, unsigned int);
                                } else if (flags.length == 'h') {
                                        uvalue = (uint16_t)va_arg(args, unsigned int);
                                } else if (flags.length == 'L') {
                                        uvalue = va_arg(args, unsigned long long);
                                } else if (flags.length == 'l') {
                                        uvalue = va_arg(args, unsigned long);
                                } else if (flags.length == 'z') {
                                        uvalue = va_arg(args, size_t);
                                } else {
                                        uvalue = va_arg(args, unsigned int);
                                }
                                format_number(uvalue, 0, 16, &flags);
                                break;

                        case 'o':
                                if (flags.length == 'H') {
                                        uvalue = (uint8_t)va_arg(args, unsigned int);
                                } else if (flags.length == 'h') {
                                        uvalue = (uint16_t)va_arg(args, unsigned int);
                                } else if (flags.length == 'L') {
                                        uvalue = va_arg(args, unsigned long long);
                                } else if (flags.length == 'l') {
                                        uvalue = va_arg(args, unsigned long);
                                } else if (flags.length == 'z') {
                                        uvalue = va_arg(args, size_t);
                                } else {
                                        uvalue = va_arg(args, unsigned int);
                                }
                                format_number(uvalue, 0, 8, &flags);
                                break;

                        case 'c':
                                if (flags.length == 'l') {
                                        format_char((int)va_arg(args, long), &flags);
                                } else {
                                        format_char(va_arg(args, int), &flags);
                                }
                                break;

                        case 's':
                                if (flags.length == 'l') {
                                        format_string((const char*)va_arg(args, long), &flags);
                                } else {
                                        format_string(va_arg(args, const char*), &flags);
                                }
                                break;

                        case 'p':
                                format_pointer((uintptr_t)va_arg(args, void*), &flags);
                                break;

                        default:
                                vga_putchar('%');
                                if (flags.left) {
                                        vga_putchar('-');
                                }
                                if (flags.zero_pad) {
                                        vga_putchar('0');
                                }
                                if (flags.alt) {
                                        vga_putchar('#');
                                }
                                vga_putchar(spec);
                                break;
                }
        }
}

void kprintf(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        kvprintf(fmt, args);
        va_end(args);
}

void vga_setcolorbase(vga_color_t color) {
        current_color = (unsigned char)color | (VGA_COLOR_BLACK << 4);
}

void vga_init(void) {
        cursor_x = 0;
        cursor_y = 0;
        vga_setcolorbase(0x8f);

        outb(0x3D4, 0x0A);          
        uint8_t oldA = inb(0x3D5); 
        outb(0x3D5, (oldA & 0x20) | (0 & 0x1F)); 

        outb(0x3D4, 0x0B);          
        uint8_t oldB = inb(0x3D5); 
        outb(0x3D5, (oldB & 0x60) | (15 & 0x1F)); 

        for (int x = 0; x < VGA_WIDTH; x++) {
                for (int y = 0; y < VGA_HEIGHT; y++) {
                        VGA_MEMORY[y * VGA_WIDTH + x] = ' ' | (0x8f << 8);
                }
        }
        vga_update_hardware_cursor();
}

void vga_get_cursor(int* x, int* y) {
        *x = cursor_x;
        *y = cursor_y;
}

void vga_set_cursor(int x, int y) {
        cursor_x = x;
        cursor_y = y;
        if (cursor_x < 0) {
                cursor_x = 0;
        }
        if (cursor_y < 0) {
                cursor_y = 0;
        }
        if (cursor_y >= VGA_HEIGHT) {
                cursor_y = VGA_HEIGHT - 1;
        }
        if (cursor_x >= VGA_WIDTH) {
                cursor_x = VGA_WIDTH - 1;
        }
        vga_update_hardware_cursor();
}

void vga_putchar(char c) {
        if (c == '\n') {
                cursor_x = 0;
                cursor_y++;
        } else if (c == '\r') {
                cursor_x = 0;
        } else if (c == '\t') {
                cursor_x = (cursor_x + 8) & ~7;
        } else if (c == '\b') {
                if (cursor_x == 0 && cursor_y == 0) {
                        return;
                }
                if (cursor_x > 0) {
                        cursor_x--;
                } else {
                        cursor_y--;
                        cursor_x = VGA_WIDTH - 1;
                }
                VGA_MEMORY[cursor_y * VGA_WIDTH + cursor_x] = ' ' | (current_color << 8);
                vga_update_hardware_cursor();
                return;
        } else {
                int index = cursor_y * VGA_WIDTH + cursor_x;
                VGA_MEMORY[index] = c | (current_color << 8);
                cursor_x++;
        }

        if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
        }

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
