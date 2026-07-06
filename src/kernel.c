#include <stddef.h>
#include <stdint.h>

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;

static volatile uint16_t* const VGA_MEMORY = (uint16_t*)0xB8000;

static size_t terminal_row;
static size_t terminal_col;
static uint8_t terminal_color;

static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
    return (uint8_t)(fg | (bg << 4));
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | (uint16_t)color << 8;
}

static inline uint8_t port_read_u8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static inline void port_write_u8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "dN"(port));
}

enum { DRIVER_STATUS_CAPACITY = 7 };

static const uint16_t PORT_VGA_STATUS = 0x03DA;
static const uint16_t PORT_PS2_STATUS = 0x0064;
static const uint16_t PORT_PS2_DATA = 0x0060;
static const uint16_t PORT_PIT_CHANNEL0 = 0x0040;
static const uint16_t PORT_PIT_COMMAND = 0x0043;
static const uint16_t PORT_COM1 = 0x03F8;
static const uint16_t PORT_CMOS_INDEX = 0x0070;
static const uint16_t PORT_CMOS_DATA = 0x0071;
static const uint16_t PORT_KEYBOARD_COMMAND = 0x0064;
static const uint8_t CMOS_REG_STATUS_A = 0x0A;
static const uint8_t CMOS_NMI_DISABLE = 0x80;
static const uint16_t PORT_ATA_PRIMARY_BASE = 0x01F0;
static const uint16_t PORT_ATA_PRIMARY_STATUS = 0x01F7;
static const uint16_t PORT_NOT_APPLICABLE = 0xFFFF;
static const size_t SERIAL_TRANSMIT_MAX_RETRIES = 8192u;
static const uint8_t FRAMEBUFFER_REQUIRED_BPP = 32u;
static const char* const KERNEL_PATCH_VERSION = "26.4.1";
static const char* const KERNEL_PATCH_LABEL = "[Patch 26.4.1 - Bootable Universal]";

static uint8_t terminal_vga_enabled = 1;
static uint8_t serial_console_enabled;
static uint8_t framebuffer_video_enabled;
static uint8_t vga_text_available;
static uint8_t driver_status_overflowed;
static uint8_t framebuffer_reject_non_32bpp;

static volatile uint8_t* framebuffer_memory;
static uint16_t framebuffer_width;
static uint16_t framebuffer_height;
static uint16_t framebuffer_pitch;
static uint8_t framebuffer_bpp;

struct multiboot_info;

static const struct multiboot_info* boot_mbi;
static uint64_t kernel_poll_ticks;

struct vbe_mode_info {
    uint16_t attributes;
    uint8_t win_a;
    uint8_t win_b;
    uint16_t granularity;
    uint16_t winsize;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t real_fct_ptr;
    uint16_t pitch;
    uint16_t xres;
    uint16_t yres;
    uint8_t wchar;
    uint8_t ychar;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t rsv_mask;
    uint8_t rsv_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
    uint32_t offscreen_mem_off;
    uint16_t offscreen_mem_size;
} __attribute__((packed));

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
};

static uint8_t status_is_present(uint8_t status) {
    return status != 0xFFu;
}

static uint8_t detect_vga_text_available(void) {
    uint8_t samples_all_ff = 1;
    for (size_t i = 0; i < 4; i++) {
        if (port_read_u8(PORT_VGA_STATUS) != 0xFFu) {
            samples_all_ff = 0;
            break;
        }
    }

    return (uint8_t)!samples_all_ff;
}

static void framebuffer_clear(uint32_t color24) {
    if (!framebuffer_video_enabled || framebuffer_bpp != FRAMEBUFFER_REQUIRED_BPP) {
        return;
    }

    uint32_t color = color24 & 0x00FFFFFFu;
    for (uint16_t y = 0; y < framebuffer_height; y++) {
        volatile uint32_t* row = (volatile uint32_t*)(framebuffer_memory + ((size_t)y * framebuffer_pitch));
        for (uint16_t x = 0; x < framebuffer_width; x++) {
            row[x] = color;
        }
    }
}

static void video_try_initialize(const struct multiboot_info* mbi) {
    framebuffer_video_enabled = 0;
    framebuffer_memory = 0;
    framebuffer_width = 0;
    framebuffer_height = 0;
    framebuffer_pitch = 0;
    framebuffer_bpp = 0;
    framebuffer_reject_non_32bpp = 0;

    if ((mbi->flags & (1u << 11)) == 0 || mbi->vbe_mode_info == 0) {
        return;
    }

    const struct vbe_mode_info* mode_info = (const struct vbe_mode_info*)(uintptr_t)mbi->vbe_mode_info;
    if (mode_info->framebuffer == 0 || mode_info->xres == 0 || mode_info->yres == 0) {
        return;
    }

    if (mode_info->bpp != FRAMEBUFFER_REQUIRED_BPP) {
        framebuffer_reject_non_32bpp = 1;
        return;
    }

    framebuffer_memory = (volatile uint8_t*)(uintptr_t)mode_info->framebuffer;
    framebuffer_width = mode_info->xres;
    framebuffer_height = mode_info->yres;
    framebuffer_pitch = mode_info->pitch;
    framebuffer_bpp = mode_info->bpp;
    framebuffer_video_enabled = 1;
    framebuffer_clear(0x000000u);
}

static void serial_try_initialize(void) {
    port_write_u8(PORT_COM1 + 1, 0x00);
    port_write_u8(PORT_COM1 + 3, 0x80);
    port_write_u8(PORT_COM1 + 0, 0x03);
    port_write_u8(PORT_COM1 + 1, 0x00);
    port_write_u8(PORT_COM1 + 3, 0x03);
    port_write_u8(PORT_COM1 + 2, 0xC7);
    port_write_u8(PORT_COM1 + 4, 0x0B);
    serial_console_enabled = (port_read_u8(PORT_COM1 + 5) != 0xFFu);
}

static void serial_putchar(char c) {
    if (!serial_console_enabled) {
        return;
    }

    for (size_t spin = 0; spin < SERIAL_TRANSMIT_MAX_RETRIES; spin++) {
        if ((port_read_u8(PORT_COM1 + 5) & 0x20u) != 0) {
            port_write_u8(PORT_COM1 + 0, (uint8_t)c);
            return;
        }
    }
}

static void terminal_clear_row(size_t row) {
    if (!terminal_vga_enabled) {
        return;
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[row * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
}

static void terminal_scroll(void) {
    if (!terminal_vga_enabled) {
        return;
    }

    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            VGA_MEMORY[(y - 1) * VGA_WIDTH + x] = VGA_MEMORY[y * VGA_WIDTH + x];
        }
    }
    terminal_clear_row(VGA_HEIGHT - 1);
}

static void terminal_initialize(void) {
    terminal_row = 0;
    terminal_col = 0;
    terminal_color = vga_entry_color(15, 0);
    vga_text_available = detect_vga_text_available();
    terminal_vga_enabled = !framebuffer_video_enabled && vga_text_available;

    if (terminal_vga_enabled) {
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            terminal_clear_row(y);
        }
    }
}

static void terminal_putchar(char c) {
    if (serial_console_enabled) {
        if (c == '\n') {
            serial_putchar('\r');
        }
        serial_putchar(c);
    }

    if (!terminal_vga_enabled) {
        return;
    }

    if (c == '\n') {
        terminal_col = 0;
        terminal_row++;
        if (terminal_row >= VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
        return;
    }

    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_col] = vga_entry((unsigned char)c, terminal_color);
    terminal_col++;

    if (terminal_col >= VGA_WIDTH) {
        terminal_col = 0;
        terminal_row++;
        if (terminal_row >= VGA_HEIGHT) {
            terminal_scroll();
            terminal_row = VGA_HEIGHT - 1;
        }
    }
}

static void terminal_write(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
    }
}

static void terminal_write_uint(uint32_t value) {
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

static void terminal_write_uint64(uint64_t value) {
    char buffer[20];
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

static void terminal_write_hex(uint32_t value) {
    const char* digits = "0123456789ABCDEF";
    terminal_write("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        terminal_putchar(digits[(value >> shift) & 0xF]);
    }
}

static uint8_t keyboard_shift_held;
static uint8_t keyboard_extended_prefix;

static char command_buffer[256];
static size_t command_length;

struct driver_status {
    const char* name;
    uint16_t io_base;
    uint8_t ready;
};

static struct driver_status driver_statuses[DRIVER_STATUS_CAPACITY];
static size_t driver_status_count;

static void driver_status_push(const char* name, uint16_t io_base, uint8_t ready) {
    if (driver_status_count >= DRIVER_STATUS_CAPACITY) {
        driver_status_overflowed = 1;
        return;
    }

    driver_statuses[driver_status_count++] = (struct driver_status){ name, io_base, ready };
}

static const char scancode_map[128] = {
    [0x01] = 27,
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`', [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm', [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static const char scancode_map_shift[128] = {
    [0x01] = 27,
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+', [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q', [0x11] = 'W', [0x12] = 'E', [0x13] = 'R', [0x14] = 'T',
    [0x15] = 'Y', [0x16] = 'U', [0x17] = 'I', [0x18] = 'O', [0x19] = 'P',
    [0x1A] = '{', [0x1B] = '}', [0x1C] = '\n',
    [0x1E] = 'A', [0x1F] = 'S', [0x20] = 'D', [0x21] = 'F', [0x22] = 'G',
    [0x23] = 'H', [0x24] = 'J', [0x25] = 'K', [0x26] = 'L',
    [0x27] = ':', [0x28] = '"', [0x29] = '~', [0x2B] = '|',
    [0x2C] = 'Z', [0x2D] = 'X', [0x2E] = 'C', [0x2F] = 'V', [0x30] = 'B',
    [0x31] = 'N', [0x32] = 'M', [0x33] = '<', [0x34] = '>', [0x35] = '?',
    [0x39] = ' '
};

static void terminal_backspace(void) {
    if (!terminal_vga_enabled) {
        return;
    }

    if (terminal_col == 0) {
        if (terminal_row == 0) {
            return;
        }
        terminal_row--;
        terminal_col = VGA_WIDTH;
    }

    terminal_col--;
    VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_col] = vga_entry(' ', terminal_color);
}

static int streq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == b[i];
}

static int starts_with(const char* text, const char* prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (text[i] != prefix[i]) {
            return 0;
        }
        i++;
    }
    return 1;
}

static void cli_prompt(void) {
    terminal_write("> ");
}

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

static void cli_command_help(void) {
    terminal_write("Commands: help, clear, echo <text>, drivers, version, about, mem, video, serial, uptime, halt, reboot, bash\n");
}

static void cli_command_about(void) {
    terminal_write("Kernel: Custom Minimal C Kernel\n");
    terminal_write("Patch: ");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
    terminal_write("Architecture: i386 freestanding (Multiboot)\n");
}

static void cli_command_mem(void) {
    if (boot_mbi == 0 || (boot_mbi->flags & (1u << 0)) == 0) {
        terminal_write("Memory info unavailable from Multiboot.\n");
        return;
    }

    terminal_write("Lower memory (KB): ");
    terminal_write_uint(boot_mbi->mem_lower);
    terminal_putchar('\n');
    terminal_write("Upper memory (KB): ");
    terminal_write_uint(boot_mbi->mem_upper);
    terminal_putchar('\n');
}

static void cli_command_video(void) {
    if (framebuffer_video_enabled) {
        terminal_write("Video: VBE framebuffer ");
        terminal_write_uint(framebuffer_width);
        terminal_write("x");
        terminal_write_uint(framebuffer_height);
        terminal_write("x");
        terminal_write_uint(framebuffer_bpp);
        terminal_putchar('\n');
        return;
    }

    if (framebuffer_reject_non_32bpp) {
        terminal_write("Video: unsupported VBE bpp, using VGA text/serial fallback\n");
        return;
    }

    if (terminal_vga_enabled) {
        terminal_write("Video: VGA text mode\n");
        return;
    }

    terminal_write("Video: serial console fallback\n");
}

static void cli_command_serial(void) {
    terminal_write("Serial COM1: ");
    terminal_write(serial_console_enabled ? "enabled" : "disabled");
    terminal_putchar('\n');
}

static void cli_command_uptime(void) {
    terminal_write("Kernel poll ticks: ");
    terminal_write_uint64(kernel_poll_ticks);
    terminal_putchar('\n');
}

static void cli_command_halt(void) {
    terminal_write("CPU halted. Reset VM to continue.\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void cli_command_reboot(void) {
    terminal_write("Rebooting...\n");

    for (size_t spin = 0; spin < 100000; spin++) {
        if ((port_read_u8(PORT_PS2_STATUS) & 0x02u) == 0) {
            break;
        }
    }

    port_write_u8(PORT_KEYBOARD_COMMAND, 0xFE);
    terminal_write("Reboot command sent (controller may ignore in some VMs).\n");
}

static void cli_command_bash(void) {
    terminal_write("GNU Bash cannot run directly in this freestanding kernel.\n");
    terminal_write("Use host GNU Bash mode: ./run-kernel.sh --bash\n");
}

static void cli_execute_command(void) {
    command_buffer[command_length] = '\0';

    if (command_length == 0) {
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "help")) {
        cli_command_help();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "clear")) {
        terminal_initialize();
        cli_prompt();
        return;
    }

    if (starts_with(command_buffer, "echo ")) {
        terminal_write(command_buffer + 5);
        terminal_putchar('\n');
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "drivers")) {
        print_driver_statuses();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "version")) {
        terminal_write("Version ");
        terminal_write(KERNEL_PATCH_VERSION);
        terminal_write(" ");
        terminal_write(KERNEL_PATCH_LABEL);
        terminal_putchar('\n');
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "about")) {
        cli_command_about();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "mem")) {
        cli_command_mem();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "video")) {
        cli_command_video();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "serial")) {
        cli_command_serial();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "uptime")) {
        cli_command_uptime();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "halt")) {
        cli_command_halt();
        return;
    }

    if (streq(command_buffer, "reboot")) {
        cli_command_reboot();
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "bash")) {
        cli_command_bash();
        cli_prompt();
        return;
    }

    terminal_write("Unknown command: ");
    terminal_write(command_buffer);
    terminal_putchar('\n');
    cli_prompt();
}

static void cli_handle_char(char c) {
    if (c == '\n') {
        terminal_putchar('\n');
        cli_execute_command();
        command_length = 0;
        return;
    }

    if (c == '\b') {
        if (command_length == 0) {
            return;
        }
        command_length--;
        terminal_backspace();
        return;
    }

    if (c < 32 || c > 126) {
        return;
    }

    if (command_length >= sizeof(command_buffer) - 1) {
        return;
    }

    command_buffer[command_length++] = c;
    terminal_putchar(c);
}

static uint8_t keyboard_poll_scancode(void) {
    if ((port_read_u8(PORT_PS2_STATUS) & 0x01u) == 0) {
        return 0;
    }

    return port_read_u8(PORT_PS2_DATA);
}

static void keyboard_handle_scancode(uint8_t scancode) {
    if (scancode == 0xE0) {
        keyboard_extended_prefix = 1;
        return;
    }

    if (keyboard_extended_prefix) {
        keyboard_extended_prefix = 0;
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) {
        keyboard_shift_held = 1;
        return;
    }

    if (scancode == 0xAA || scancode == 0xB6) {
        keyboard_shift_held = 0;
        return;
    }

    if ((scancode & 0x80u) != 0) {
        return;
    }

    char c = keyboard_shift_held ? scancode_map_shift[scancode] : scancode_map[scancode];

    if (c == 0) {
        return;
    }

    cli_handle_char(c);
}

static void print_boot_info(uint32_t magic, const struct multiboot_info* mbi) {
    terminal_write("Multiboot magic: ");
    terminal_write_hex(magic);
    terminal_putchar('\n');

    if (magic != 0x2BADB002) {
        terminal_write("Invalid Multiboot magic.\n");
        return;
    }

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

    if (framebuffer_video_enabled) {
        terminal_write("Video driver: VBE framebuffer ");
        terminal_write_uint(framebuffer_width);
        terminal_write("x");
        terminal_write_uint(framebuffer_height);
        terminal_write("x");
        terminal_write_uint(framebuffer_bpp);
        terminal_putchar('\n');
    } else if (framebuffer_reject_non_32bpp) {
        terminal_write("Video driver: unsupported VBE bpp; using VGA text / serial fallback\n");
    } else {
        terminal_write("Video driver: VGA text / serial fallback\n");
    }
}

static void detect_drivers(void) {
    uint8_t status;
    driver_status_count = 0;
    driver_status_overflowed = 0;

    driver_status_push("VGA text", PORT_VGA_STATUS, vga_text_available);

    driver_status_push("VBE framebuffer", PORT_NOT_APPLICABLE, framebuffer_video_enabled);

    status = port_read_u8(PORT_PS2_STATUS);
    driver_status_push("PS/2 keyboard", PORT_PS2_STATUS, status_is_present(status));

    status = port_read_u8(PORT_PIT_COMMAND);
    driver_status_push("PIT timer", PORT_PIT_CHANNEL0, status_is_present(status));

    status = port_read_u8(PORT_COM1 + 5);
    driver_status_push("Serial COM1", PORT_COM1, status_is_present(status));

    port_write_u8(PORT_CMOS_INDEX, (uint8_t)(CMOS_NMI_DISABLE | CMOS_REG_STATUS_A));
    status = port_read_u8(PORT_CMOS_DATA);
    driver_status_push("CMOS RTC", PORT_CMOS_INDEX, status_is_present(status));
    port_write_u8(PORT_CMOS_INDEX, CMOS_REG_STATUS_A);

    status = port_read_u8(PORT_ATA_PRIMARY_STATUS);
    driver_status_push("ATA primary", PORT_ATA_PRIMARY_BASE, status_is_present(status));
}

void kernel_main(uint32_t magic, uint32_t multiboot_info_addr) {
    const struct multiboot_info* mbi = (const struct multiboot_info*)(uintptr_t)multiboot_info_addr;

    boot_mbi = mbi;
    kernel_poll_ticks = 0;

    serial_try_initialize();
    video_try_initialize(mbi);
    terminal_initialize();
    terminal_write("Custom C kernel booted.\n");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
    terminal_write("Basic terminal ready.\n");
    print_boot_info(magic, mbi);
    terminal_write("Probing drivers...\n");
    detect_drivers();
    print_driver_statuses();
    terminal_write("Keyboard input mode: polling.\n");
    cli_prompt();

    for (;;) {
        kernel_poll_ticks++;
        uint8_t scancode = keyboard_poll_scancode();
        if (scancode != 0) {
            keyboard_handle_scancode(scancode);
        }

        __asm__ volatile ("pause");
    }
}
