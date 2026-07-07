#ifndef YBASH_H
#define YBASH_H

#include <stdint.h>

struct multiboot_info;

uint8_t keyboard_poll_scancode(void);
void yBash_start(uint32_t magic, const struct multiboot_info* mbi);

#endif