#ifndef VSH_H
#define VSH_H

#include <stdint.h>

#include "Drivers/DriverState.h"

uint8_t keyboard_poll_scancode(void);
void vsh_start(uint32_t magic, const struct multiboot_info* mbi);

#endif