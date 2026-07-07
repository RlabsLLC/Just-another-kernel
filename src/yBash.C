#include <stddef.h>
#include <stdint.h>

#include "kernel_utils.h"
#include "yBash.h"

enum { DRIVER_STATUS_CAPACITY = 7 };
enum { YBASH_ENTRY_CAPACITY = 16 };
enum { YBASH_NAME_CAPACITY = 64 };
enum { YBASH_CONTENT_CAPACITY = 1024 };

static const uint16_t PORT_PS2_STATUS = 0x0064;
static const uint16_t PORT_KEYBOARD_COMMAND = 0x0064;

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

struct driver_status {
    const char* name;
    uint16_t io_base;
    uint8_t ready;
};

struct ybash_entry {
    char name[YBASH_NAME_CAPACITY];
    char content[YBASH_CONTENT_CAPACITY];
    size_t content_length;
    uint8_t in_use;
    uint8_t is_folder;
};

extern const char* const KERNEL_PATCH_VERSION;
extern const char* const KERNEL_PATCH_LABEL;
extern uint8_t serial_console_enabled;
extern uint8_t framebuffer_video_enabled;
extern uint8_t framebuffer_reject_non_32bpp;
extern uint8_t vga_text_available;
extern volatile uint8_t* framebuffer_memory;
extern uint16_t framebuffer_width;
extern uint16_t framebuffer_height;
extern uint16_t framebuffer_pitch;
extern uint8_t framebuffer_bpp;
extern const struct multiboot_info* boot_mbi;
extern uint32_t kernel_poll_ticks;
extern struct driver_status driver_statuses[DRIVER_STATUS_CAPACITY];
extern size_t driver_status_count;
extern uint8_t driver_status_overflowed;

static uint8_t keyboard_shift_held;
static uint8_t keyboard_extended_prefix;
static uint8_t ybash_capture_mode;
static int ybash_capture_index;
static char command_buffer[256];
static size_t command_length;
static struct ybash_entry ybash_entries[YBASH_ENTRY_CAPACITY];
static size_t ybash_entry_count;

static inline uint8_t port_read_u8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static inline void port_write_u8(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "dN"(port));
}

static void ybash_prompt(void) {
    terminal_write("yBash> ");
}

static void ybash_restart_state(void) {
    command_length = 0;
    keyboard_shift_held = 0;
    keyboard_extended_prefix = 0;
    ybash_capture_mode = 0;
    ybash_capture_index = -1;
}

static void ybash_fs_reset(void) {
    for (size_t i = 0; i < YBASH_ENTRY_CAPACITY; i++) {
        ybash_entries[i].in_use = 0;
        ybash_entries[i].is_folder = 0;
        ybash_entries[i].content_length = 0;
        ybash_entries[i].name[0] = '\0';
        ybash_entries[i].content[0] = '\0';
    }

    ybash_entry_count = 0;
}

static const char* ybash_skip_spaces(const char* text) {
    while (*text == ' ') {
        text++;
    }

    return text;
}

static int ybash_find_entry(const char* name) {
    for (size_t i = 0; i < YBASH_ENTRY_CAPACITY; i++) {
        if (ybash_entries[i].in_use && kernel_streq(ybash_entries[i].name, name)) {
            return (int)i;
        }
    }

    return -1;
}

static void ybash_list_entries(void) {
    if (ybash_entry_count == 0) {
        terminal_write("No virtual entries.\n");
        return;
    }

    for (size_t i = 0; i < YBASH_ENTRY_CAPACITY; i++) {
        if (!ybash_entries[i].in_use) {
            continue;
        }

        terminal_write(ybash_entries[i].is_folder ? "[fs] " : "[fl] ");
        terminal_write(ybash_entries[i].name);
        terminal_putchar('\n');
    }
}

static int ybash_add_entry(const char* name, uint8_t is_folder) {
    if (name[0] == '\0') {
        terminal_write("usage: mk -fl|mk -fs <name>\n");
        return -1;
    }

    if (ybash_find_entry(name) >= 0) {
        terminal_write("entry already exists: ");
        terminal_write(name);
        terminal_putchar('\n');
        return -1;
    }

    for (size_t i = 0; i < YBASH_ENTRY_CAPACITY; i++) {
        if (ybash_entries[i].in_use) {
            continue;
        }

        size_t j = 0;
        while (name[j] != '\0' && j + 1 < YBASH_NAME_CAPACITY) {
            ybash_entries[i].name[j] = name[j];
            j++;
        }
        ybash_entries[i].name[j] = '\0';
        ybash_entries[i].in_use = 1;
        ybash_entries[i].is_folder = is_folder;
        ybash_entries[i].content_length = 0;
        ybash_entries[i].content[0] = '\0';
        ybash_entry_count++;

        terminal_write(is_folder ? "created virtual fs node: " : "created virtual file: ");
        terminal_write(ybash_entries[i].name);
        terminal_putchar('\n');
        return (int)i;
    }

    terminal_write("yBash storage full.\n");
    return -1;
}

static void ybash_append_content_char(int index, char c) {
    if (index < 0 || (size_t)index >= YBASH_ENTRY_CAPACITY) {
        return;
    }

    if ((size_t)index >= YBASH_ENTRY_CAPACITY || !ybash_entries[index].in_use || ybash_entries[index].is_folder) {
        return;
    }

    if (ybash_entries[index].content_length + 1 >= YBASH_CONTENT_CAPACITY) {
        terminal_write("\nfile content limit reached\n");
        return;
    }

    ybash_entries[index].content[ybash_entries[index].content_length++] = c;
    ybash_entries[index].content[ybash_entries[index].content_length] = '\0';
}

static void ybash_finish_capture(void) {
    if (!ybash_capture_mode) {
        return;
    }

    terminal_write("\nfile saved\n");
    ybash_capture_mode = 0;
    ybash_capture_index = -1;
    ybash_prompt();
}

static void ybash_remove_entry(const char* name) {
    int index = ybash_find_entry(name);
    if (index < 0) {
        terminal_write("not found: ");
        terminal_write(name);
        terminal_putchar('\n');
        return;
    }

    if (ybash_capture_mode && ybash_capture_index == index) {
        ybash_capture_mode = 0;
        ybash_capture_index = -1;
    }

    ybash_entries[index].in_use = 0;
    ybash_entries[index].is_folder = 0;
    ybash_entries[index].name[0] = '\0';
    if (ybash_entry_count > 0) {
        ybash_entry_count--;
    }

    terminal_write("removed: ");
    terminal_write(name);
    terminal_putchar('\n');
}

static void ybash_describe_entry(const char* name) {
    int index = ybash_find_entry(name);
    if (index < 0) {
        terminal_write("not found: ");
        terminal_write(name);
        terminal_putchar('\n');
        return;
    }

    if (ybash_entries[index].is_folder) {
        terminal_write("virtual fs node: ");
        terminal_write(ybash_entries[index].name);
        terminal_putchar('\n');
        return;
    }

    terminal_write("virtual file: ");
    terminal_write(ybash_entries[index].name);
    terminal_putchar('\n');
    if (ybash_entries[index].content_length == 0) {
        terminal_write("(empty)\n");
    } else {
        terminal_write(ybash_entries[index].content);
        terminal_putchar('\n');
    }
}

static void ybash_command_drivers(void) {
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

static void ybash_command_help(void) {
    terminal_write("Commands: help, clear, echo <text>, drivers, version, about, mem, video, serial, uptime, halt, reboot, bash, ls, cat <name>, touch <name>, mkdir <name>, mk -fl <name>, mk -fs <name>, rm <name>, rm -fs, restart\n");
    terminal_write("mk -fl capture mode: Enter saves, Shift+Enter inserts newline\n");
}

static void ybash_command_about(void) {
    terminal_write("yBash: original shell layer for the kernel\n");
    terminal_write("Patch: ");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
}

static void ybash_command_mem(void) {
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

static void ybash_command_video(void) {
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

    if (vga_text_available) {
        terminal_write("Video: VGA text mode\n");
        return;
    }

    terminal_write("Video: serial console fallback\n");
}

static void ybash_command_serial(void) {
    terminal_write("Serial COM1: ");
    terminal_write(serial_console_enabled ? "enabled" : "disabled");
    terminal_putchar('\n');
}

static void ybash_command_uptime(void) {
    terminal_write("Kernel poll ticks: ");
    terminal_write_uint(kernel_poll_ticks);
    terminal_putchar('\n');
}

static void ybash_command_halt(void) {
    terminal_write("CPU halted. Reset/Restart to continue.\n");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

static void ybash_command_reboot(void) {
    terminal_write("Rebooting...\n");

    for (size_t spin = 0; spin < 100000; spin++) {
        if ((port_read_u8(PORT_PS2_STATUS) & 0x02u) == 0) {
            break;
        }
    }

    port_write_u8(PORT_KEYBOARD_COMMAND, 0xFE);
    terminal_write("Reboot command sent (controller may ignore in some VMs).\n");
}

static void ybash_command_bash(void) {
    terminal_write("GNU Bash is a host userland shell, not a freestanding kernel module.\n");
    terminal_write("Use host Bash mode: ./run-kernel.sh --bash\n");
}

static void ybash_command_clear(void) {
    terminal_initialize();
    ybash_fs_reset();
}

static void ybash_execute_command(void) {
    command_buffer[command_length] = '\0';

    if (command_length == 0) {
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "help")) {
        ybash_command_help();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "clear")) {
        ybash_command_clear();
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "echo ")) {
        terminal_write(command_buffer + 5);
        terminal_putchar('\n');
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "drivers")) {
        ybash_command_drivers();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "version")) {
        terminal_write("Version ");
        terminal_write(KERNEL_PATCH_VERSION);
        terminal_write(" ");
        terminal_write(KERNEL_PATCH_LABEL);
        terminal_putchar('\n');
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "about")) {
        ybash_command_about();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "mem")) {
        ybash_command_mem();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "video")) {
        ybash_command_video();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "serial")) {
        ybash_command_serial();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "uptime")) {
        ybash_command_uptime();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "halt")) {
        ybash_command_halt();
        return;
    }

    if (kernel_streq(command_buffer, "reboot")) {
        ybash_command_reboot();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "bash")) {
        ybash_command_bash();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "ls")) {
        ybash_list_entries();
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "cat ")) {
        ybash_describe_entry(ybash_skip_spaces(command_buffer + 4));
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "touch ")) {
        (void)ybash_add_entry(ybash_skip_spaces(command_buffer + 6), 0);
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "mkdir ")) {
        (void)ybash_add_entry(ybash_skip_spaces(command_buffer + 6), 1);
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "mk -fl ")) {
        int file_index = ybash_add_entry(ybash_skip_spaces(command_buffer + 7), 0);
        if (file_index >= 0) {
            ybash_capture_mode = 1;
            ybash_capture_index = file_index;
            terminal_write("enter file contents (Shift+Enter = newline, Enter = save)\n");
            terminal_write("content> ");
        } else {
            ybash_prompt();
        }
        return;
    }

    if (kernel_starts_with(command_buffer, "mk -fs ")) {
        (void)ybash_add_entry(ybash_skip_spaces(command_buffer + 7), 1);
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "rm -fs")) {
        ybash_fs_reset();
        terminal_write("virtual fs cleared\n");
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "rm ")) {
        ybash_remove_entry(ybash_skip_spaces(command_buffer + 3));
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "restart")) {
        ybash_command_clear();
        terminal_write("yBash restarted.\n");
        ybash_prompt();
        return;
    }

    terminal_write("Unknown command: ");
    terminal_write(command_buffer);
    terminal_putchar('\n');
    ybash_prompt();
}

static void ybash_handle_char(char c) {
    if (ybash_capture_mode) {
        if (c == '\n') {
            ybash_finish_capture();
            return;
        }

        if (c == '\b') {
            if (ybash_capture_index >= 0) {
                struct ybash_entry* entry = &ybash_entries[ybash_capture_index];
                if (entry->content_length > 0) {
                    entry->content_length--;
                    entry->content[entry->content_length] = '\0';
                    terminal_write("\b \b");
                }
            }
            return;
        }

        if (c >= 32 && c <= 126) {
            ybash_append_content_char(ybash_capture_index, c);
            terminal_putchar(c);
        }
        return;
    }

    if (c == '\n') {
        terminal_putchar('\n');
        ybash_execute_command();
        command_length = 0;
        return;
    }

    if (c == '\b') {
        if (command_length == 0) {
            return;
        }
        command_length--;
        terminal_write("\b \b");
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

static void ybash_handle_scancode(uint8_t scancode) {
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

    if (ybash_capture_mode && scancode == 0x1C) {
        if (keyboard_shift_held) {
            ybash_append_content_char(ybash_capture_index, '\n');
            terminal_putchar('\n');
            terminal_write("content> ");
            return;
        }

        ybash_handle_char('\n');
        return;
    }

    char c = keyboard_shift_held ? scancode_map_shift[scancode] : scancode_map[scancode];
    if (c == 0) {
        return;
    }

    ybash_handle_char(c);
}

void yBash_start(uint32_t magic, const struct multiboot_info* mbi) {
    (void)magic;
    (void)mbi;

    ybash_fs_reset();
    ybash_restart_state();
    terminal_write("yBash ready. Type help for commands.\n");
    ybash_prompt();

    for (;;) {
        kernel_poll_ticks++;
        uint8_t scancode = keyboard_poll_scancode();
        if (scancode != 0) {
            ybash_handle_scancode(scancode);
        }

        __asm__ volatile ("pause");
    }
}