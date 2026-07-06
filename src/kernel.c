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
    terminal_write("> ");

    for (;;) {
        __asm__ volatile ("hlt");
    }
}
