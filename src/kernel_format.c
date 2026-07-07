#include "kernel_utils.h"

#include <stddef.h>

void terminal_putchar(char c);
void terminal_write(const char* data);

void terminal_write_uint(uint32_t value) {
    char buffer[10];
    size_t i = 0;

    if (value == 0) {
        terminal_putchar('0');
        return;
    }

    while (value > 0) {
        buffer[i++] = (char)('0' + (value % 10));
        value /= 10;
    }

    while (i > 0) {
        terminal_putchar(buffer[--i]);
    }
}

void terminal_write_hex(uint32_t value) {
    const char* digits = "0123456789ABCDEF";
    terminal_write("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        terminal_putchar(digits[(value >> shift) & 0xF]);
    }
}