#include <stddef.h>
#include <stdint.h>

#include "Drivers/DriverState.h"
#include "Drivers/Legacy/TerminalDriver.h"
#include "Drivers/Latest/VirtualFS.h"
#include "kernel_utils.h"
#include "yBash.h"

extern const char* const KERNEL_PATCH_VERSION;
extern const char* const KERNEL_PATCH_LABEL;

static uint8_t keyboard_shift_held;
static uint8_t keyboard_extended_prefix;
static uint8_t mouse_render_enabled;
static int32_t mouse_cursor_x;
static int32_t mouse_cursor_y;
static char command_buffer[256];
static size_t command_length;

static const int8_t MOUSE_CURSOR_SHAPE[][2] = {
    {0, 0}, {1, 0},
    {0, 1}, {1, 1}, {2, 1}, 
    {0, 2}, {1, 2}, {2, 2}, {3, 2},
    {0, 3}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, 
    {0, 4}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {5, 4},
    {0, 5}, {1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, 
    {0, 6}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {7, 6},
    {0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}, {8, 7},
    {0, 8}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 8}, {7, 8}, {8, 8}, {9, 8},
    {0, 9}, {1, 9}, {2, 9}, {3, 9}, {4, 9}, {5, 9}, {6, 9}, {7, 9}, {8, 9}, {9, 9},
    {0, 10}, {1, 10}, {2, 10}, {3, 10}, {4, 10}, {5, 10},
    {0, 11}, {1, 11}, {2, 11}, {3, 11}, {4, 11}, {5, 11}, 
    {0, 12}, {1, 12}, {2, 12}, {3, 12}, {4, 12}, {5, 12}, {6, 12}, 
    {0, 13}, {1, 13}, {2, 13},          {4, 13}, {5, 13}, {6, 13}, 
    {0, 14}, {1, 14},                            {5, 14}, {6, 14}, {7, 14}, 
    {0, 15},                                     {5, 15}, {6, 15}, {7, 15}, 
    {0, 16},                                              {6, 16}, {7, 16}, {8, 16},
                                                          {6, 17}, {7, 17}, {8, 17}, {9, 17},
                                                                   {7, 18}, {8, 18}, {9, 18},
                                                                   {7, 19}, {8, 19}, {9, 19},
                                                                            {8, 20}
};

static void ybash_mouse_cursor_plot(int32_t base_x, int32_t base_y, uint8_t draw) {
    if (!framebuffer_video_enabled || framebuffer_width == 0 || framebuffer_height == 0) {
        return;
    }

    for (size_t i = 0; i < (sizeof(MOUSE_CURSOR_SHAPE) / sizeof(MOUSE_CURSOR_SHAPE[0])); i++) {
        int32_t x = base_x + (int32_t)MOUSE_CURSOR_SHAPE[i][0];
        int32_t y = base_y + (int32_t)MOUSE_CURSOR_SHAPE[i][1];
        if (x < 0 || y < 0 || x >= (int32_t)framebuffer_width || y >= (int32_t)framebuffer_height) {
            continue;
        }

        if (draw) {
            (void)terminal_draw_dot((uint32_t)x, (uint32_t)y);
        } else {
            (void)terminal_clear_dot((uint32_t)x, (uint32_t)y);
        }
    }
}

static void ybash_prompt(void) {
    terminal_write("> ");
}

static void ybash_restart_state(void) {
    command_length = 0;
    keyboard_shift_held = 0;
    keyboard_extended_prefix = 0;
    mouse_render_enabled = 0;
    mouse_cursor_x = 0;
    mouse_cursor_y = 0;
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

static void ybash_command_palette(void) {
    uint8_t original_fg = terminal_get_fg_color();
    uint8_t original_bg = terminal_get_bg_color();

    for (uint8_t i = 0; i < 16; i++) {
        terminal_set_color(i, original_bg);
        terminal_write_uint(i);
        terminal_write(": ");
        terminal_write(terminal_color_name(i));
        terminal_putchar('\n');
    }

    terminal_set_color(original_fg, original_bg);
}

static void ybash_command_uname(void) {
    terminal_write("Yet/Just Another Kernel ");
    terminal_write(KERNEL_PATCH_VERSION);
    terminal_write(" ");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
}

static const char* ybash_skip_spaces(const char* text) {
    while (*text == ' ') {
        text++;
    }

    return text;
}

static void ybash_command_color(const char* args) {
    char fg_text[32];
    char bg_text[32];
    size_t fg_len = 0;
    size_t bg_len = 0;
    const char* cursor = ybash_skip_spaces(args);

    while (*cursor != '\0' && *cursor != ' ' && fg_len + 1 < sizeof(fg_text)) {
        fg_text[fg_len++] = *cursor++;
    }
    fg_text[fg_len] = '\0';

    cursor = ybash_skip_spaces(cursor);
    while (*cursor != '\0' && *cursor != ' ' && bg_len + 1 < sizeof(bg_text)) {
        bg_text[bg_len++] = *cursor++;
    }
    bg_text[bg_len] = '\0';

    if (fg_len == 0) {
        terminal_write("usage: color <fg> [bg]\n");
        return;
    }

    int fg = terminal_parse_color(fg_text);
    int bg = (bg_len == 0) ? terminal_get_bg_color() : terminal_parse_color(bg_text);
    if (fg < 0 || bg < 0) {
        terminal_write("invalid color. use palette for supported names/indices\n");
        return;
    }

    terminal_set_color((uint8_t)fg, (uint8_t)bg);
    terminal_write("Color set: fg=");
    terminal_write(terminal_color_name((uint8_t)fg));
    terminal_write(", bg=");
    terminal_write(terminal_color_name((uint8_t)bg));
    terminal_putchar('\n');
}

static void ybash_command_stat(const char* name) {
    int index = vfs_lookup(name);
    if (index < 0) {
        terminal_write("not found: ");
        terminal_write(name);
        terminal_putchar('\n');
        return;
    }

    const struct vfs_node* node = vfs_get_node(index);
    if (node == 0) {
        terminal_write("not found: ");
        terminal_write(name);
        terminal_putchar('\n');
        return;
    }

    terminal_write("name: ");
    terminal_write(node->name);
    terminal_putchar('\n');
    terminal_write("type: ");
    terminal_write(vfs_node_type_name(node->type));
    terminal_putchar('\n');

    if (node->type != VFS_NODE_DIR) {
        terminal_write("size: ");
        terminal_write_uint((uint32_t)node->content_length);
        terminal_write(" bytes\n");
    }
}

static void ybash_command_write(const char* args) {
    char name[VFS_NAME_CAPACITY];
    size_t name_len = 0;
    const char* cursor = ybash_skip_spaces(args);

    while (*cursor != '\0' && *cursor != ' ' && name_len + 1 < sizeof(name)) {
        name[name_len++] = *cursor++;
    }
    name[name_len] = '\0';

    cursor = ybash_skip_spaces(cursor);
    if (name_len == 0 || *cursor == '\0') {
        terminal_write("usage: write <path> <text>\n");
        return;
    }

    int index = vfs_lookup(name);
    if (index < 0) {
        terminal_write("not found: ");
        terminal_write(name);
        terminal_putchar('\n');
        return;
    }

    const struct vfs_node* node = vfs_get_node(index);
    if (node == 0 || node->type == VFS_NODE_DIR) {
        terminal_write("cannot write to folder: ");
        terminal_write(name);
        terminal_putchar('\n');
        return;
    }

    (void)vfs_write(name, cursor);

    terminal_write("updated: ");
    terminal_write(name);
    terminal_putchar('\n');
}

static int ybash_parse_u32(const char** cursor, uint32_t* value_out) {
    uint32_t value = 0;
    uint8_t saw_digit = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        saw_digit = 1;
        value = (value * 10u) + (uint32_t)(**cursor - '0');
        (*cursor)++;
    }
    if (!saw_digit) {
        return -1;
    }
    *value_out = value;
    return 0;
}

static void ybash_command_draw_dot(const char* args) {
    const char* cursor = ybash_skip_spaces(args);
    uint32_t x = 0;
    uint32_t y = 0;

    if (ybash_parse_u32(&cursor, &x) != 0 || *cursor != ',') {
        terminal_write("usage: draw-dot x,y\n");
        return;
    }
    cursor++;
    if (ybash_parse_u32(&cursor, &y) != 0 || *ybash_skip_spaces(cursor) != '\0') {
        terminal_write("usage: draw-dot x,y\n");
        return;
    }

    if (terminal_draw_dot(x, y) != 0) {
        terminal_write("draw-dot failed: framebuffer unavailable or out of range\n");
        return;
    }
}

static void ybash_command_mouse_on(void) {
    if (!mouse_is_available()) {
        terminal_write("mouse unavailable\n");
        return;
    }
    if (!framebuffer_video_enabled) {
        terminal_write("mouse rendering requires framebuffer video\n");
        return;
    }

    mouse_cursor_x = (int32_t)(framebuffer_width / 2u);
    mouse_cursor_y = (int32_t)(framebuffer_height / 2u);
    mouse_render_enabled = 1u;
    ybash_mouse_cursor_plot(mouse_cursor_x, mouse_cursor_y, 1u);
    terminal_write("mouse rendering enabled\n");
}

static void ybash_command_mouse_off(void) {
    if (mouse_render_enabled) {
        ybash_mouse_cursor_plot(mouse_cursor_x, mouse_cursor_y, 0u);
    }
    mouse_render_enabled = 0u;
    terminal_write("mouse rendering disabled\n");
}

static int32_t ybash_clamp_i32(int32_t value, int32_t min_value, int32_t max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static void ybash_mouse_render_tick(void) {
    if (!mouse_render_enabled || !framebuffer_video_enabled || framebuffer_width == 0 || framebuffer_height == 0) {
        return;
    }

    int8_t dx = 0;
    int8_t dy = 0;
    uint8_t buttons = 0;
    while (mouse_poll_packet(&dx, &dy, &buttons)) {
        (void)buttons;
        ybash_mouse_cursor_plot(mouse_cursor_x, mouse_cursor_y, 0u);

        int32_t max_x = (int32_t)framebuffer_width - 1;
        int32_t max_y = (int32_t)framebuffer_height - 1;
        mouse_cursor_x = ybash_clamp_i32(mouse_cursor_x + (int32_t)dx, 0, max_x);
        mouse_cursor_y = ybash_clamp_i32(mouse_cursor_y - (int32_t)dy, 0, max_y);

        ybash_mouse_cursor_plot(mouse_cursor_x, mouse_cursor_y, 1u);
    }
}

static void ybash_command_help(void) {
    terminal_write("Commands: help, clear, echo <text>, drivers, version, about, uname, mem, video, serial, uptime, halt, reboot, bash, color <fg> [bg], palette, draw-dot x,y, mouse on, mouse off, ls [path], cat <path>, stat <path>, touch <path>, mkdir <path>, write <path> <text>, rm <path>, mk -fs <path>, restart\n");
}

static void ybash_command_about(void) {
    terminal_write("yBash: original shell layer for the kernel\n");
    terminal_write("Patch: ");
    terminal_write(KERNEL_PATCH_LABEL);
    terminal_putchar('\n');
}

static void ybash_command_mem(void) {
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

static void ybash_command_video(void) {
    if (framebuffer_video_enabled) {
        terminal_write("Video: framebuffer ");
        terminal_write_uint(framebuffer_width);
        terminal_write("x");
        terminal_write_uint(framebuffer_height);
        terminal_write("x");
        terminal_write_uint(framebuffer_bpp);
        terminal_putchar('\n');
        return;
    }

    if (framebuffer_reject_non_32bpp) {
        terminal_write("Video: unsupported pixel format, using VGA text/serial fallback\n");
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

    platform_request_reboot();
    terminal_write("Reboot command sent (controller may ignore in some VMs).\n");
}

static void ybash_command_bash(void) {
    terminal_write("GNU Bash is a host userland shell, not a freestanding kernel module.\n");
    terminal_write("Use host Bash mode: ./run-kernel.sh --bash\n");
}

static void ybash_command_clear(void) {
    terminal_clear_screen();
    // RTCI reinitialization removed
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

    if (kernel_streq(command_buffer, "uname")) {
        ybash_command_uname();
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

    if (kernel_streq(command_buffer, "palette")) {
        ybash_command_palette();
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "color ")) {
        ybash_command_color(command_buffer + 6);
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "draw-dot ")) {
        ybash_command_draw_dot(command_buffer + 9);
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "mouse on")) {
        ybash_command_mouse_on();
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "mouse off")) {
        ybash_command_mouse_off();
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "ls")) {
        const char* path = ybash_skip_spaces(command_buffer + 2);
        if (*path == '\0') {
            path = "/";
        }
        int node_indexes[VFS_NODE_CAPACITY];
        size_t count = vfs_list_children(path, node_indexes, VFS_NODE_CAPACITY);
        if (count == 0) {
            terminal_write("No virtual entries.\n");
        } else {
            for (size_t i = 0; i < count && i < VFS_NODE_CAPACITY; i++) {
                const struct vfs_node* node = vfs_get_node(node_indexes[i]);
                if (node == 0) {
                    continue;
                }
                terminal_write(node->type == VFS_NODE_DIR ? "[fs] " : (node->type == VFS_NODE_EXECUTABLE ? "[rt] " : "[fl] "));
                terminal_write(node->name);
                terminal_putchar('\n');
            }
        }
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "cat ")) {
        const char* path = ybash_skip_spaces(command_buffer + 4);
        const char* content = vfs_read(path);
        if (content == 0) {
            terminal_write("not found: ");
            terminal_write(path);
            terminal_putchar('\n');
        } else {
            terminal_write(content);
            terminal_putchar('\n');
        }
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "stat ")) {
        ybash_command_stat(ybash_skip_spaces(command_buffer + 5));
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "touch ")) {
        const char* path = ybash_skip_spaces(command_buffer + 6);
        (void)vfs_create_file(path);
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "mkdir ")) {
        const char* path = ybash_skip_spaces(command_buffer + 6);
        (void)vfs_create_dir(path);
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "write ")) {
        ybash_command_write(command_buffer + 6);
        ybash_prompt();
        return;
    }
    
    if (kernel_starts_with(command_buffer, "mk -fs ")) {
        //const char* path = ybash_skip_spaces(command_buffer + 7);
        //(void)vfs_create_dir(path); 
        terminal_write("storage drivers are disabled right now\n");
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "rm -fs")) {
        terminal_write("storage drivers are disabled right now\n");
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "rm ")) {
        const char* path = ybash_skip_spaces(command_buffer + 3);
        if (vfs_remove(path) != 0) {
            terminal_write("not found: ");
            terminal_write(path);
            terminal_putchar('\n');
        } else {
            terminal_write("removed: ");
            terminal_write(path);
            terminal_putchar('\n');
        }
        ybash_prompt();
        return;
    }

    if (kernel_starts_with(command_buffer, "rtci ")) {
        terminal_write("RTCI is disabled while storage drivers are offline\n");
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "services")) {
        terminal_write("RTCI services are disabled\n");
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "fs")) {
        terminal_write("storage drivers are disabled\n");
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "fs sync")) {
        terminal_write("storage drivers are disabled\n");
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "fs flash")) {
        terminal_write("storage drivers are disabled\n");
        ybash_prompt();
        return;
    }

    if (kernel_streq(command_buffer, "fs info")) {
        terminal_write("storage drivers are disabled\n");
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

    if (scancode >= 128u) {
        return;
    }

    if ((scancode & 0x80u) != 0) {
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

    ybash_restart_state();
    terminal_write("yBash ready. Type help for commands.\n");
    ybash_prompt();

    for (;;) {
        kernel_poll_ticks++;

        char serial_char;
        if (serial_poll_char(&serial_char)) {
            if (serial_char == '\r') {
                ybash_handle_char('\n');
            } else if (serial_char == 0x7Fu) {
                ybash_handle_char('\b');
            } else {
                ybash_handle_char(serial_char);
            }
        }

        uint8_t scancode = keyboard_poll_scancode();
        if (scancode != 0) {
            ybash_handle_scancode(scancode);
        }

        ybash_mouse_render_tick();

        __asm__ volatile ("pause");
    }
}