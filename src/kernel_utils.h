#ifndef KERNEL_UTILS_H
#define KERNEL_UTILS_H

#include <stdint.h>

void terminal_putchar(char c);
void terminal_write(const char* data);
void terminal_initialize(void);
void terminal_clear_screen(void);
int terminal_draw_dot(uint32_t x, uint32_t y);
int terminal_clear_dot(uint32_t x, uint32_t y);
void terminal_set_color(uint8_t fg, uint8_t bg);
void terminal_reset_color(void);
uint8_t terminal_get_fg_color(void);
uint8_t terminal_get_bg_color(void);
const char* terminal_color_name(uint8_t index);
int terminal_parse_color(const char* text);

int kernel_streq(const char* a, const char* b);
int kernel_starts_with(const char* text, const char* prefix);
void terminal_write_uint(uint32_t value);
void terminal_write_hex(uint32_t value);

const char* kernel_skip_spaces(const char* text);

#endif