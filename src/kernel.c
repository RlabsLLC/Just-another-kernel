#include <stddef.h>
#include <stdint.h>

#include "kernel_utils.h"
#include "yBash.h"

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static const size_t FRAMEBUFFER_CHAR_WIDTH = 8;
static const size_t FRAMEBUFFER_CHAR_HEIGHT = 16;
static const uint32_t TERMINAL_FG_COLOR = 0x00FFFFFFu;
static const uint32_t TERMINAL_BG_COLOR = 0x00000000u;

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
static const uint16_t PORT_VGA_MISC_WRITE = 0x03C2;
static const uint16_t PORT_VGA_SEQ_INDEX = 0x03C4;
static const uint16_t PORT_VGA_SEQ_DATA = 0x03C5;
static const uint16_t PORT_VGA_CRTC_INDEX = 0x03D4;
static const uint16_t PORT_VGA_CRTC_DATA = 0x03D5;
static const uint16_t PORT_VGA_GC_INDEX = 0x03CE;
static const uint16_t PORT_VGA_GC_DATA = 0x03CF;
static const uint16_t PORT_VGA_AC_INDEX = 0x03C0;
static const uint16_t PORT_VGA_AC_WRITE = 0x03C0;
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
const char* const KERNEL_PATCH_VERSION = "26.5.1";
const char* const KERNEL_PATCH_LABEL = "[Patch 26.5.1 - Bootable Universal]";

uint8_t terminal_vga_enabled = 1;
uint8_t serial_console_enabled;
uint8_t framebuffer_video_enabled;
uint8_t vga_text_available;
uint8_t driver_status_overflowed;
uint8_t framebuffer_reject_non_32bpp;
uint8_t framebuffer_reject_reason;

volatile uint8_t* framebuffer_memory;
uint16_t framebuffer_width;
uint16_t framebuffer_height;
uint16_t framebuffer_pitch;
uint8_t framebuffer_bpp;
size_t framebuffer_text_columns;
size_t framebuffer_text_rows;

struct multiboot_info;

enum {
    MULTIBOOT1_BOOTLOADER_MAGIC = 0x2BADB002u,
    MULTIBOOT2_BOOTLOADER_MAGIC = 0x36D76289u,
    MULTIBOOT2_TAG_END = 0u,
    MULTIBOOT2_TAG_BASIC_MEMINFO = 4u,
    MULTIBOOT2_TAG_FRAMEBUFFER = 8u
};

enum {
    FB_REJECT_NONE = 0u,
    FB_REJECT_UNSUPPORTED_BPP = 1u,
    FB_REJECT_UNSUPPORTED_TYPE = 2u,
    FB_REJECT_BAD_GEOMETRY = 3u,
    FB_REJECT_BAD_PITCH = 4u,
    FB_REJECT_MALFORMED_MB2 = 5u
};

const struct multiboot_info* boot_mbi;
uint32_t kernel_poll_ticks;
uint8_t boot_mb2_mem_available;
uint32_t boot_mb2_mem_lower;
uint32_t boot_mb2_mem_upper;

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

struct multiboot2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct multiboot2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct multiboot2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed));

struct multiboot2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved;
} __attribute__((packed));

static void framebuffer_put_pixel(uint16_t x, uint16_t y, uint32_t color24);
static void framebuffer_clear(uint32_t color24);
static void terminal_backspace(void);

static uint8_t multiboot2_parse_framebuffer(uint32_t mb2_info_addr) {
    const struct multiboot2_info* info = (const struct multiboot2_info*)(uintptr_t)mb2_info_addr;
    const uint8_t* start = (const uint8_t*)(uintptr_t)mb2_info_addr;
    if (info->total_size < 16u) {
        framebuffer_reject_reason = FB_REJECT_MALFORMED_MB2;
        return 0;
    }

    const uint8_t* end = start + info->total_size;
    const struct multiboot2_tag* tag = (const struct multiboot2_tag*)(start + 8);

    boot_mb2_mem_available = 0;
    boot_mb2_mem_lower = 0;
    boot_mb2_mem_upper = 0;

    while ((const uint8_t*)tag + sizeof(*tag) <= end && tag->size >= sizeof(*tag)) {
        if (tag->type == MULTIBOOT2_TAG_END) {
            break;
        }

        if (tag->type == MULTIBOOT2_TAG_BASIC_MEMINFO && tag->size >= sizeof(struct multiboot2_tag_basic_meminfo)) {
            const struct multiboot2_tag_basic_meminfo* mem = (const struct multiboot2_tag_basic_meminfo*)tag;
            boot_mb2_mem_available = 1;
            boot_mb2_mem_lower = mem->mem_lower;
            boot_mb2_mem_upper = mem->mem_upper;
        }

        if (tag->type == MULTIBOOT2_TAG_FRAMEBUFFER && tag->size >= sizeof(struct multiboot2_tag_framebuffer)) {
            const struct multiboot2_tag_framebuffer* fb = (const struct multiboot2_tag_framebuffer*)tag;
            if (fb->framebuffer_addr == 0 || fb->framebuffer_width == 0 || fb->framebuffer_height == 0) {
                framebuffer_reject_reason = FB_REJECT_BAD_GEOMETRY;
            } else if (fb->framebuffer_type != 0u && fb->framebuffer_type != 1u) {
                framebuffer_reject_reason = FB_REJECT_UNSUPPORTED_TYPE;
            } else if (fb->framebuffer_bpp != 16u && fb->framebuffer_bpp != 24u && fb->framebuffer_bpp != 32u) {
                framebuffer_reject_reason = FB_REJECT_UNSUPPORTED_BPP;
            } else {
                uint32_t bytes_per_pixel = (uint32_t)(fb->framebuffer_bpp / 8u);
                if (bytes_per_pixel == 0u || fb->framebuffer_pitch < fb->framebuffer_width * bytes_per_pixel) {
                    framebuffer_reject_reason = FB_REJECT_BAD_PITCH;
                } else {
                    framebuffer_memory = (volatile uint8_t*)(uintptr_t)fb->framebuffer_addr;
                    framebuffer_width = (uint16_t)fb->framebuffer_width;
                    framebuffer_height = (uint16_t)fb->framebuffer_height;
                    framebuffer_pitch = (uint16_t)fb->framebuffer_pitch;
                    framebuffer_bpp = fb->framebuffer_bpp;
                    framebuffer_video_enabled = 1;
                    framebuffer_text_columns = framebuffer_width / FRAMEBUFFER_CHAR_WIDTH;
                    framebuffer_text_rows = framebuffer_height / FRAMEBUFFER_CHAR_HEIGHT;
                    framebuffer_clear(0x000000u);
                    return 1;
                }
            }

            framebuffer_reject_non_32bpp = (framebuffer_reject_reason == FB_REJECT_UNSUPPORTED_BPP);
        }

        uint32_t step = (tag->size + 7u) & ~7u;
        tag = (const struct multiboot2_tag*)((const uint8_t*)tag + step);
    }

    return 0;
}

static const uint8_t VGA_TEXTMODE_REGS[] = {
    0x67,
    0x03, 0x00, 0x03, 0x00, 0x02,
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
    0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x10, 0x0E, 0x00, 0x00, 0x05, 0x0F,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
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

    if (samples_all_ff) {
        return 0;
    }

    /* Verify CRTC cursor register write/read round-trip. */
    port_write_u8(PORT_VGA_CRTC_INDEX, 0x0F);
    uint8_t cursor_lo_original = port_read_u8(PORT_VGA_CRTC_DATA);
    uint8_t cursor_lo_test = (uint8_t)(cursor_lo_original ^ 0x5Au);
    port_write_u8(PORT_VGA_CRTC_DATA, cursor_lo_test);

    port_write_u8(PORT_VGA_CRTC_INDEX, 0x0F);
    uint8_t cursor_lo_read = port_read_u8(PORT_VGA_CRTC_DATA);

    port_write_u8(PORT_VGA_CRTC_INDEX, 0x0F);
    port_write_u8(PORT_VGA_CRTC_DATA, cursor_lo_original);

    if (cursor_lo_read != cursor_lo_test) {
        return 0;
    }

    uint16_t original = VGA_MEMORY[0];
    uint16_t sentinel = (uint16_t)(original ^ 0x5A5Au);
    VGA_MEMORY[0] = sentinel;
    uint16_t read_back = VGA_MEMORY[0];
    VGA_MEMORY[0] = original;

    return (uint8_t)(read_back == sentinel);
}

static void vga_write_registers(const uint8_t* regs) {
    port_write_u8(PORT_VGA_MISC_WRITE, *regs++);

    for (uint8_t i = 0; i < 5; i++) {
        port_write_u8(PORT_VGA_SEQ_INDEX, i);
        port_write_u8(PORT_VGA_SEQ_DATA, *regs++);
    }

    port_write_u8(PORT_VGA_CRTC_INDEX, 0x03);
    port_write_u8(PORT_VGA_CRTC_DATA, (uint8_t)(port_read_u8(PORT_VGA_CRTC_DATA) | 0x80u));
    port_write_u8(PORT_VGA_CRTC_INDEX, 0x11);
    port_write_u8(PORT_VGA_CRTC_DATA, (uint8_t)(port_read_u8(PORT_VGA_CRTC_DATA) & 0x7Fu));

    for (uint8_t i = 0; i < 25; i++) {
        port_write_u8(PORT_VGA_CRTC_INDEX, i);
        port_write_u8(PORT_VGA_CRTC_DATA, *regs++);
    }

    for (uint8_t i = 0; i < 9; i++) {
        port_write_u8(PORT_VGA_GC_INDEX, i);
        port_write_u8(PORT_VGA_GC_DATA, *regs++);
    }

    for (uint8_t i = 0; i < 21; i++) {
        (void)port_read_u8(PORT_VGA_STATUS);
        port_write_u8(PORT_VGA_AC_INDEX, i);
        port_write_u8(PORT_VGA_AC_WRITE, *regs++);
    }

    (void)port_read_u8(PORT_VGA_STATUS);
    port_write_u8(PORT_VGA_AC_INDEX, 0x20);
}

static void vga_force_text_mode(void) {
    vga_write_registers(VGA_TEXTMODE_REGS);
}

static void framebuffer_clear(uint32_t color24) {
    if (!framebuffer_video_enabled) {
        return;
    }

    for (uint16_t y = 0; y < framebuffer_height; y++) {
        for (uint16_t x = 0; x < framebuffer_width; x++) {
            framebuffer_put_pixel(x, y, color24);
        }
    }
}

static void framebuffer_put_pixel(uint16_t x, uint16_t y, uint32_t color24) {
    if (!framebuffer_video_enabled || x >= framebuffer_width || y >= framebuffer_height) {
        return;
    }

    volatile uint8_t* pixel = framebuffer_memory + ((size_t)y * framebuffer_pitch) + (size_t)x * (framebuffer_bpp / 8u);
    uint32_t color = color24 & 0x00FFFFFFu;

    if (framebuffer_bpp == 32u) {
        *((volatile uint32_t*)pixel) = color;
    } else if (framebuffer_bpp == 24u) {
        pixel[0] = (uint8_t)(color & 0xFFu);
        pixel[1] = (uint8_t)((color >> 8) & 0xFFu);
        pixel[2] = (uint8_t)((color >> 16) & 0xFFu);
    } else if (framebuffer_bpp == 16u) {
        uint8_t r = (uint8_t)((color >> 16) & 0xFFu);
        uint8_t g = (uint8_t)((color >> 8) & 0xFFu);
        uint8_t b = (uint8_t)(color & 0xFFu);
        uint16_t color565 = (uint16_t)(((uint16_t)(r >> 3) << 11)
            | ((uint16_t)(g >> 2) << 5)
            | ((uint16_t)(b >> 3)));
        *((volatile uint16_t*)pixel) = color565;
    }
}

static void framebuffer_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t color24) {
    if (!framebuffer_video_enabled || w == 0 || h == 0) {
        return;
    }

    for (uint16_t iy = 0; iy < h; iy++) {
        uint16_t py = (uint16_t)(y + iy);
        if (py >= framebuffer_height) {
            break;
        }

        for (uint16_t ix = 0; ix < w; ix++) {
            uint16_t px = (uint16_t)(x + ix);
            if (px >= framebuffer_width) {
                break;
            }

            framebuffer_put_pixel(px, py, color24);
        }
    }
}

static uint8_t glyph_row_5x7(char c, uint8_t row) {
    if (row > 6) {
        return 0;
    }

    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    switch (c) {
        case 'A': { static const uint8_t p[7] = {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return p[row]; }
        case 'B': { static const uint8_t p[7] = {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}; return p[row]; }
        case 'C': { static const uint8_t p[7] = {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return p[row]; }
        case 'D': { static const uint8_t p[7] = {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return p[row]; }
        case 'E': { static const uint8_t p[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return p[row]; }
        case 'F': { static const uint8_t p[7] = {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return p[row]; }
        case 'G': { static const uint8_t p[7] = {0x0E,0x11,0x10,0x10,0x13,0x11,0x0E}; return p[row]; }
        case 'H': { static const uint8_t p[7] = {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return p[row]; }
        case 'I': { static const uint8_t p[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}; return p[row]; }
        case 'J': { static const uint8_t p[7] = {0x01,0x01,0x01,0x01,0x11,0x11,0x0E}; return p[row]; }
        case 'K': { static const uint8_t p[7] = {0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return p[row]; }
        case 'L': { static const uint8_t p[7] = {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return p[row]; }
        case 'M': { static const uint8_t p[7] = {0x11,0x1B,0x15,0x11,0x11,0x11,0x11}; return p[row]; }
        case 'N': { static const uint8_t p[7] = {0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return p[row]; }
        case 'O': { static const uint8_t p[7] = {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return p[row]; }
        case 'P': { static const uint8_t p[7] = {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return p[row]; }
        case 'Q': { static const uint8_t p[7] = {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}; return p[row]; }
        case 'R': { static const uint8_t p[7] = {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return p[row]; }
        case 'S': { static const uint8_t p[7] = {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return p[row]; }
        case 'T': { static const uint8_t p[7] = {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return p[row]; }
        case 'U': { static const uint8_t p[7] = {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return p[row]; }
        case 'V': { static const uint8_t p[7] = {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}; return p[row]; }
        case 'W': { static const uint8_t p[7] = {0x11,0x11,0x11,0x11,0x15,0x1B,0x11}; return p[row]; }
        case 'X': { static const uint8_t p[7] = {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}; return p[row]; }
        case 'Y': { static const uint8_t p[7] = {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return p[row]; }
        case 'Z': { static const uint8_t p[7] = {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return p[row]; }

        case '0': { static const uint8_t p[7] = {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}; return p[row]; }
        case '1': { static const uint8_t p[7] = {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return p[row]; }
        case '2': { static const uint8_t p[7] = {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}; return p[row]; }
        case '3': { static const uint8_t p[7] = {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E}; return p[row]; }
        case '4': { static const uint8_t p[7] = {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return p[row]; }
        case '5': { static const uint8_t p[7] = {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E}; return p[row]; }
        case '6': { static const uint8_t p[7] = {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}; return p[row]; }
        case '7': { static const uint8_t p[7] = {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return p[row]; }
        case '8': { static const uint8_t p[7] = {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return p[row]; }
        case '9': { static const uint8_t p[7] = {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E}; return p[row]; }

        case ' ': return 0x00;
        case '.': return row == 6 ? 0x04 : 0x00;
        case ',': return row == 5 ? 0x04 : (row == 6 ? 0x08 : 0x00);
        case ':': return (row == 2 || row == 5) ? 0x04 : 0x00;
        case ';': return row == 2 ? 0x04 : (row == 5 ? 0x04 : (row == 6 ? 0x08 : 0x00));
        case '!': return (row <= 4 || row == 6) ? 0x04 : 0x00;
        case '?': { static const uint8_t p[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}; return p[row]; }
        case '-': return row == 3 ? 0x0E : 0x00;
        case '_': return row == 6 ? 0x1F : 0x00;
        case '/': return row == 0 ? 0x01 : row == 1 ? 0x02 : row == 2 ? 0x04 : row == 3 ? 0x08 : row == 4 ? 0x10 : 0x00;
        case '\\': return row == 0 ? 0x10 : row == 1 ? 0x08 : row == 2 ? 0x04 : row == 3 ? 0x02 : row == 4 ? 0x01 : 0x00;
        case '(': return row == 0 ? 0x02 : row == 1 ? 0x04 : row == 2 || row == 3 || row == 4 ? 0x08 : row == 5 ? 0x04 : row == 6 ? 0x02 : 0x00;
        case ')': return row == 0 ? 0x08 : row == 1 ? 0x04 : row == 2 || row == 3 || row == 4 ? 0x02 : row == 5 ? 0x04 : row == 6 ? 0x08 : 0x00;
        case '[': return row == 0 || row == 6 ? 0x0E : 0x08;
        case ']': return row == 0 || row == 6 ? 0x0E : 0x02;
        case '<': return row == 0 ? 0x02 : row == 1 ? 0x04 : row == 2 ? 0x08 : row == 3 ? 0x10 : row == 4 ? 0x08 : row == 5 ? 0x04 : row == 6 ? 0x02 : 0x00;
        case '>': return row == 0 ? 0x08 : row == 1 ? 0x04 : row == 2 ? 0x02 : row == 3 ? 0x01 : row == 4 ? 0x02 : row == 5 ? 0x04 : row == 6 ? 0x08 : 0x00;
        case '=': return row == 2 || row == 4 ? 0x0E : 0x00;
        case '+': return row == 1 || row == 2 || row == 4 || row == 5 ? 0x04 : row == 3 ? 0x1F : 0x00;
        case '*': return row == 1 ? 0x15 : row == 2 ? 0x0E : row == 3 ? 0x04 : 0x00;
        case '"': return row <= 1 ? 0x0A : 0x00;
        case '\'': return row <= 1 ? 0x04 : 0x00;
        default: {
            static const uint8_t p[7] = {0x0E,0x11,0x01,0x02,0x04,0x00,0x04};
            return p[row];
        }
    }
}

static void framebuffer_draw_glyph(char c, size_t col, size_t row, uint32_t fg, uint32_t bg) {
    if (!framebuffer_video_enabled) {
        return;
    }

    uint16_t x0 = (uint16_t)(col * FRAMEBUFFER_CHAR_WIDTH);
    uint16_t y0 = (uint16_t)(row * FRAMEBUFFER_CHAR_HEIGHT);
    framebuffer_fill_rect(x0, y0, (uint16_t)FRAMEBUFFER_CHAR_WIDTH, (uint16_t)FRAMEBUFFER_CHAR_HEIGHT, bg);

    for (uint8_t r = 0; r < 7; r++) {
        uint8_t bits = glyph_row_5x7(c, r);
        uint16_t py = (uint16_t)(y0 + 1 + (r * 2));

        for (uint8_t dy = 0; dy < 2; dy++) {
            for (uint8_t x = 0; x < 5; x++) {
                if ((bits & (1u << (4 - x))) != 0) {
                    framebuffer_put_pixel((uint16_t)(x0 + 1 + x), (uint16_t)(py + dy), fg);
                }
            }
        }
    }
}

static void framebuffer_scroll_terminal(void) {
    if (!framebuffer_video_enabled || framebuffer_height <= FRAMEBUFFER_CHAR_HEIGHT) {
        return;
    }

    size_t bytes_per_row = framebuffer_pitch;
    uint16_t rows_to_copy = (uint16_t)(framebuffer_height - FRAMEBUFFER_CHAR_HEIGHT);

    for (uint16_t y = 0; y < rows_to_copy; y++) {
        volatile uint8_t* dst = framebuffer_memory + ((size_t)y * framebuffer_pitch);
        volatile uint8_t* src = framebuffer_memory + ((size_t)(y + FRAMEBUFFER_CHAR_HEIGHT) * framebuffer_pitch);
        for (size_t x = 0; x < bytes_per_row; x++) {
            dst[x] = src[x];
        }
    }

    framebuffer_fill_rect(0, (uint16_t)rows_to_copy, framebuffer_width, (uint16_t)FRAMEBUFFER_CHAR_HEIGHT, TERMINAL_BG_COLOR);
}

static void video_try_initialize(uint32_t boot_magic, uint32_t boot_info_addr) {
    framebuffer_video_enabled = 0;
    framebuffer_memory = 0;
    framebuffer_width = 0;
    framebuffer_height = 0;
    framebuffer_pitch = 0;
    framebuffer_bpp = 0;
    framebuffer_reject_non_32bpp = 0;
    framebuffer_reject_reason = FB_REJECT_NONE;
    boot_mb2_mem_available = 0;
    boot_mb2_mem_lower = 0;
    boot_mb2_mem_upper = 0;

    if (boot_magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        (void)multiboot2_parse_framebuffer(boot_info_addr);
        return;
    }

    if (boot_magic != MULTIBOOT1_BOOTLOADER_MAGIC) {
        return;
    }

    const struct multiboot_info* mbi = (const struct multiboot_info*)(uintptr_t)boot_info_addr;

    if ((mbi->flags & (1u << 11)) == 0 || mbi->vbe_mode_info == 0) {
        return;
    }

    const struct vbe_mode_info* mode_info = (const struct vbe_mode_info*)(uintptr_t)mbi->vbe_mode_info;
    if (mode_info->framebuffer == 0 || mode_info->xres == 0 || mode_info->yres == 0) {
        return;
    }

    if (mode_info->bpp != 16u && mode_info->bpp != 24u && mode_info->bpp != 32u) {
        framebuffer_reject_non_32bpp = 1;
        framebuffer_reject_reason = FB_REJECT_UNSUPPORTED_BPP;
        return;
    }

    framebuffer_memory = (volatile uint8_t*)(uintptr_t)mode_info->framebuffer;
    framebuffer_width = mode_info->xres;
    framebuffer_height = mode_info->yres;
    framebuffer_pitch = mode_info->pitch;
    framebuffer_bpp = mode_info->bpp;
    framebuffer_video_enabled = 1;
    framebuffer_text_columns = framebuffer_width / FRAMEBUFFER_CHAR_WIDTH;
    framebuffer_text_rows = framebuffer_height / FRAMEBUFFER_CHAR_HEIGHT;
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
    if (framebuffer_video_enabled) {
        if (row < framebuffer_text_rows) {
            framebuffer_fill_rect(0, (uint16_t)(row * FRAMEBUFFER_CHAR_HEIGHT), framebuffer_width, (uint16_t)FRAMEBUFFER_CHAR_HEIGHT, TERMINAL_BG_COLOR);
        }
        return;
    }

    if (!terminal_vga_enabled) {
        return;
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        VGA_MEMORY[row * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
}

static void terminal_scroll(void) {
    if (framebuffer_video_enabled) {
        framebuffer_scroll_terminal();
        return;
    }

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

void terminal_initialize(void) {
    terminal_row = 0;
    terminal_col = 0;
    terminal_color = vga_entry_color(15, 0);
    vga_text_available = detect_vga_text_available();
    terminal_vga_enabled = !framebuffer_video_enabled && vga_text_available;

    if (framebuffer_video_enabled) {
        framebuffer_clear(TERMINAL_BG_COLOR);
        return;
    }

    if (terminal_vga_enabled) {
        vga_force_text_mode();
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            terminal_clear_row(y);
        }
    }
}

void terminal_putchar(char c) {
    if (serial_console_enabled) {
        if (c == '\n') {
            serial_putchar('\r');
        } else if (c == '\b') {
            serial_putchar('\b');
            serial_putchar(' ');
            serial_putchar('\b');
        } else {
            serial_putchar(c);
        }
    }

    if (!terminal_vga_enabled && !framebuffer_video_enabled) {
        return;
    }

    size_t width = terminal_vga_enabled ? VGA_WIDTH : framebuffer_text_columns;
    size_t height = terminal_vga_enabled ? VGA_HEIGHT : framebuffer_text_rows;

    if (width == 0 || height == 0) {
        return;
    }

    if (c == '\b') {
        terminal_backspace();
        return;
    }

    if (c == '\n') {
        terminal_col = 0;
        terminal_row++;
        if (terminal_row >= height) {
            terminal_scroll();
            terminal_row = height - 1;
        }
        return;
    }

    if (terminal_vga_enabled) {
        VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_col] = vga_entry((unsigned char)c, terminal_color);
    } else {
        framebuffer_draw_glyph(c, terminal_col, terminal_row, TERMINAL_FG_COLOR, TERMINAL_BG_COLOR);
    }

    terminal_col++;

    if (terminal_col >= width) {
        terminal_col = 0;
        terminal_row++;
        if (terminal_row >= height) {
            terminal_scroll();
            terminal_row = height - 1;
        }
    }
}

void terminal_write(const char* data) {
    for (size_t i = 0; data[i] != '\0'; i++) {
        terminal_putchar(data[i]);
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

struct driver_status driver_statuses[DRIVER_STATUS_CAPACITY];
size_t driver_status_count;

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
    if (!terminal_vga_enabled && !framebuffer_video_enabled) {
        return;
    }

    size_t width = terminal_vga_enabled ? VGA_WIDTH : framebuffer_text_columns;
    if (width == 0) {
        return;
    }

    if (terminal_col == 0) {
        if (terminal_row == 0) {
            return;
        }
        terminal_row--;
        terminal_col = width;
    }

    terminal_col--;
    if (terminal_vga_enabled) {
        VGA_MEMORY[terminal_row * VGA_WIDTH + terminal_col] = vga_entry(' ', terminal_color);
    } else {
        framebuffer_draw_glyph(' ', terminal_col, terminal_row, TERMINAL_FG_COLOR, TERMINAL_BG_COLOR);
    }
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
    terminal_write("Yet Another Kernel\n");
    terminal_write("Patch: ");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
}

static void cli_command_mem(void) {
    if (boot_mbi != 0 && (boot_mbi->flags & (1u << 0)) != 0) {
        terminal_write("Lower memory (KB): ");
        terminal_write_uint(boot_mbi->mem_lower);
        terminal_putchar('\n');
        terminal_write("Upper memory (KB): ");
        terminal_write_uint(boot_mbi->mem_upper);
        terminal_putchar('\n');
        return;
    }

    if (boot_mb2_mem_available) {
        terminal_write("Lower memory (KB): ");
        terminal_write_uint(boot_mb2_mem_lower);
        terminal_putchar('\n');
        terminal_write("Upper memory (KB): ");
        terminal_write_uint(boot_mb2_mem_upper);
        terminal_putchar('\n');
        return;
    }

    if (boot_mbi == 0) {
        terminal_write("Memory info unavailable from bootloader.\n");
        return;
    }

    if ((boot_mbi->flags & (1u << 0)) == 0) {
        terminal_write("Memory info unavailable from Multiboot.\n");
        return;
    }
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
        terminal_write("Video: unsupported VBE pixel format, using VGA text/serial fallback\n");
        return;
    }

    if (framebuffer_reject_reason != FB_REJECT_NONE) {
        terminal_write("Video: framebuffer unavailable (reason ");
        terminal_write_uint(framebuffer_reject_reason);
        terminal_write("), using VGA text/serial fallback\n");
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
    terminal_write_uint(kernel_poll_ticks);
    terminal_putchar('\n');
}

static void cli_command_halt(void) {
    terminal_write("CPU halted. Reset/Restart to continue.\n");
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

    if (kernel_streq(command_buffer, "help")) {
        cli_command_help();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "clear")) {
        terminal_initialize();
        cli_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "echo ")) {
        terminal_write(command_buffer + 5);
        terminal_putchar('\n');
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "drivers")) {
        print_driver_statuses();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "version")) {
        terminal_write("Version ");
        terminal_write(KERNEL_PATCH_VERSION);
        terminal_write(" ");
        terminal_write(KERNEL_PATCH_LABEL);
        terminal_putchar('\n');
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "about")) {
        cli_command_about();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "mem")) {
        cli_command_mem();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "video")) {
        cli_command_video();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "serial")) {
        cli_command_serial();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "uptime")) {
        cli_command_uptime();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "halt")) {
        cli_command_halt();
        return;
    }

    if (kernel_streq(command_buffer, "reboot")) {
        cli_command_reboot();
        cli_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "bash")) {
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

uint8_t keyboard_poll_scancode(void) {
    if ((port_read_u8(PORT_PS2_STATUS) & 0x01u) == 0) {
        return 0;
    }

    return port_read_u8(PORT_PS2_DATA);
}

static void __attribute__((unused)) keyboard_handle_scancode(uint8_t scancode) {
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
        terminal_write("Video driver: VBE framebuffer ");
        terminal_write_uint(framebuffer_width);
        terminal_write("x");
        terminal_write_uint(framebuffer_height);
        terminal_write("x");
        terminal_write_uint(framebuffer_bpp);
        terminal_putchar('\n');
    } else if (framebuffer_reject_non_32bpp) {
        terminal_write("Video driver: unsupported VBE pixel format; using VGA text / serial fallback\n");
    } else if (framebuffer_reject_reason != FB_REJECT_NONE) {
        terminal_write("Video driver: framebuffer unavailable (reason ");
        terminal_write_uint(framebuffer_reject_reason);
        terminal_write("); using VGA text / serial fallback\n");
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

    boot_mbi = (magic == MULTIBOOT1_BOOTLOADER_MAGIC) ? mbi : 0;
    kernel_poll_ticks = 0;

    serial_try_initialize();
    video_try_initialize(magic, multiboot_info_addr);
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

    yBash_start(magic, mbi);
}
