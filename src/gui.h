#ifndef GUI_H
#define GUI_H

#include <stdint.h>

int gui_draw_window(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* title);
int gui_draw_button(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* label);
int gui_draw_label(uint32_t x, uint32_t y, const char* text);
int gui_draw_demo(void);

#endif
