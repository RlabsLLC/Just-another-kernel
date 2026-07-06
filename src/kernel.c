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

static void terminal_clear_row(size_t row) {
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[row * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
}

static void terminal_scroll(void) {
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

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        terminal_clear_row(y);
    }
}

static void terminal_putchar(char c) {
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

static void terminal_write_hex(uint32_t value) {
    const char* digits = "0123456789ABCDEF";
    terminal_write("0x");

    for (int shift = 28; shift >= 0; shift -= 4) {
        terminal_putchar(digits[(value >> shift) & 0xF]);
    }
}

static inline uint8_t port_read_u8(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "dN"(port));
    return value;
}

static uint8_t keyboard_shift_held;
static uint8_t keyboard_extended_prefix;

static char command_buffer[256];
static size_t command_length;

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

static void cli_execute_command(void) {
    command_buffer[command_length] = '\0';

    if (command_length == 0) {
        cli_prompt();
        return;
    }

    if (streq(command_buffer, "help")) {
        terminal_write("Commands: help, clear, echo <text>\n");
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
    if ((port_read_u8(0x64) & 0x01u) == 0) {
        return 0;
    }

    return port_read_u8(0x60);
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

struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

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
}

void kernel_main(uint32_t magic, uint32_t multiboot_info_addr) {
    const struct multiboot_info* mbi = (const struct multiboot_info*)(uintptr_t)multiboot_info_addr;

    terminal_initialize();
    terminal_write("Custom C kernel booted.\n");
    terminal_write("Basic terminal ready.\n");
    print_boot_info(magic, mbi);
    terminal_write("Keyboard ready (polling).\n");
    cli_prompt();

    for (;;) {
        uint8_t scancode = keyboard_poll_scancode();
        if (scancode != 0) {
            keyboard_handle_scancode(scancode);
        }

        __asm__ volatile ("pause");
    }
}
