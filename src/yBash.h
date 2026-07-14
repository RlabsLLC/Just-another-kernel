#ifndef YBASH_H
#define YBASH_H

#include <stdint.h>

#include "Drivers/DriverState.h"

uint8_t keyboard_poll_scancode(void);
void yBash_start(uint32_t magic, const struct multiboot_info* mbi);

#endif