#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "types.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STAT_PORT 0x64

#define LSHIFT_MAKE  0x2A
#define LSHIFT_BREAK 0xAA
#define RSHIFT_MAKE  0x36
#define RSHIFT_BREAK 0xB6

#define KEY_LEFT  0x100
#define KEY_RIGHT 0x101
#define KEY_UP    0x102
#define KEY_DOWN  0x103

void keyboard_init(void);
void keyboard_poll(void);
void keyboard_handler(void);
int keyboard_getchar(void);
int keyboard_haschar(void);
char* get_string(void);

#endif
