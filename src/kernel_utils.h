#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stdint.h>

void terminal_putchar(char c);
void terminal_write(const char* data);
void terminal_initialize(void);

int kernel_streq(const char* a, const char* b);
int kernel_starts_with(const char* text, const char* prefix);
void terminal_write_uint(uint32_t value);
void terminal_write_hex(uint32_t value);

#endif