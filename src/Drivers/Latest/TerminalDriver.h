#ifndef TERMINAL_DRIVER_H
#define TERMINAL_DRIVER_H

#include <stdint.h>

void serial_try_initialize(void);
uint8_t serial_poll_char(char* out_char);
void video_try_initialize(uint32_t boot_magic, uint32_t boot_info_addr);
void detect_drivers(void);
uint8_t keyboard_poll_scancode(void);
void platform_request_reboot(void);
void terminal_boot_delay_ticks(uint32_t ticks);
uint8_t disk_try_initialize(void);
uint8_t disk_read_sectors(uint32_t lba, uint8_t count, void* buffer);
uint8_t disk_write_sectors(uint32_t lba, uint8_t count, const void* buffer);

#endif