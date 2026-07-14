#include <stddef.h>
#include <stdint.h>

#include "Drivers/DriverState.h"
#include "Drivers/Legacy/TerminalDriver.h"
#include "Drivers/Latest/VirtualFS.h"
#include "kernel_utils.h"
#include "yBash.h"

const char* const KERNEL_PATCH_VERSION = "26.5.2";
const char* const KERNEL_PATCH_LABEL = "[Patch 26.5.2]";

static void print_driver_statuses(void) {
    for (size_t i = 0; i < driver_status_count; i++) {
        terminal_write("- ");
        terminal_write(driver_statuses[i].name);
        terminal_write(" @ ");
        terminal_write_hex(driver_statuses[i].io_base);
        terminal_write(": ");
        terminal_write(driver_statuses[i].ready ? "ready" : "unavailable (skipped)");
        terminal_putchar('\n');
    }

    if (driver_status_overflowed) {
        terminal_write("- Warning: driver list truncated due to capacity limit.\n");
    }
}

static void print_boot_info(uint32_t magic, const struct multiboot_info* mbi) {
    terminal_write("Multiboot magic: ");
    terminal_write_hex(magic);
    terminal_putchar('\n');

    if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        terminal_write("Multiboot2 info @ ");
        terminal_write_hex((uint32_t)(uintptr_t)mbi);
        terminal_putchar('\n');

        if (boot_mb2_mem_available) {
            terminal_write("Lower memory (KB): ");
            terminal_write_uint(boot_mb2_mem_lower);
            terminal_putchar('\n');
            terminal_write("Upper memory (KB): ");
            terminal_write_uint(boot_mb2_mem_upper);
            terminal_putchar('\n');
        } else {
            terminal_write("Memory fields unavailable.\n");
        }
    } else if (magic != MULTIBOOT1_BOOTLOADER_MAGIC) {
        terminal_write("Invalid Multiboot magic.\n");
        return;
    } else {
        terminal_write("Multiboot info @ ");
        terminal_write_hex((uint32_t)(uintptr_t)mbi);
        terminal_putchar('\n');

        if ((mbi->flags & (1u << 0)) != 0) {
            terminal_write("Lower memory (KB): ");
            terminal_write_uint(mbi->mem_lower);
            terminal_putchar('\n');
            terminal_write("Upper memory (KB): ");
            terminal_write_uint(mbi->mem_upper);
            terminal_putchar('\n');
        } else {
            terminal_write("Memory fields unavailable.\n");
        }
    }

    if (framebuffer_video_enabled) {
        terminal_write("Video driver: framebuffer ");
        terminal_write_uint(framebuffer_width);
        terminal_write("x");
        terminal_write_uint(framebuffer_height);
        terminal_write("x");
        terminal_write_uint(framebuffer_bpp);
        terminal_putchar('\n');
    } else if (framebuffer_reject_non_32bpp) {
        terminal_write("Video driver: unsupported pixel format; using VGA text / serial fallback\n");
    } else if (framebuffer_reject_reason != FB_REJECT_NONE) {
        terminal_write("Video driver: framebuffer unavailable (reason ");
        terminal_write_uint(framebuffer_reject_reason);
        terminal_write("); using VGA text / serial fallback\n");
    } else {
        terminal_write("Video driver: VGA text / serial fallback\n");
    }
}

void kernel_main(uint32_t magic, uint32_t multiboot_info_addr) {
    const struct multiboot_info* mbi = (const struct multiboot_info*)(uintptr_t)multiboot_info_addr;

    boot_mbi = (magic == MULTIBOOT1_BOOTLOADER_MAGIC) ? mbi : 0;
    kernel_poll_ticks = 0;

    serial_try_initialize();
    video_try_initialize(magic, multiboot_info_addr);
    terminal_initialize();

    terminal_write("Yet/Just Another Kernel\n");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
    terminal_write("A minimal, custom kernel built by Ring Inc.\n");
    print_boot_info(magic, mbi);
    terminal_write("Probing drivers...\n");
    detect_drivers();
    print_driver_statuses();
    terminal_write("Keyboard input mode: polling.\n");

    vfs_init();
    terminal_write("Storage drivers disabled; using RAM VFS only, due to corrupting storage drivers.\n");

    yBash_start(magic, mbi);
}
