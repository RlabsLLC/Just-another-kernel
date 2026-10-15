#include "gui.h"

#include <stddef.h>

#include "Drivers/DriverState.h"
#include "kernel_utils.h"

static const uint32_t GUI_DESKTOP_COLOR = 0x18324Au;
static const uint32_t GUI_WINDOW_BORDER_COLOR = 0xB7C7D8u;
static const uint32_t GUI_WINDOW_COLOR = 0xEAF0F6u;
static const uint32_t GUI_TITLE_COLOR = 0x254A68u;
static const uint32_t GUI_TEXT_COLOR = 0x14212Bu;
static const uint32_t GUI_BUTTON_COLOR = 0xCFDCE8u;

static int gui_text_length(const char* text, size_t* length_out) {
    if (text == 0 || length_out == 0) {
        return -1;
    }
    size_t length = 0;
    while (text[length] != '\0') {
        if (length >= 64u) {
            return -1;
        }
        length++;
    }
    *length_out = length;
    return 0;
}

int gui_draw_window(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* title) {
    size_t title_length = 0;
    if (!framebuffer_video_enabled || gui_text_length(title, &title_length) != 0 ||
        width < 24u || height < 28u || title_length > (width - 16u) / 8u ||
        width - 1u > (uint32_t)-1 - x || height - 1u > (uint32_t)-1 - y) {
        return -1;
    }
    if (terminal_fill_rect(x, y, width, height, GUI_WINDOW_BORDER_COLOR) != 0 ||
        terminal_fill_rect(x + 2u, y + 20u, width - 4u, height - 22u, GUI_WINDOW_COLOR) != 0 ||
        terminal_fill_rect(x + 2u, y + 2u, width - 4u, 16u, GUI_TITLE_COLOR) != 0 ||
        terminal_draw_text_at(x + 8u, y + 2u, title, 0xFFFFFFFFu, GUI_TITLE_COLOR) != 0) {
        return -1;
    }
    return 0;
}

int gui_draw_button(uint32_t x, uint32_t y, uint32_t width, uint32_t height, const char* label) {
    size_t length = 0;
    if (!framebuffer_video_enabled || gui_text_length(label, &length) != 0 ||
        width < 12u || height < 20u || length > (width - 4u) / 8u ||
        width - 1u > (uint32_t)-1 - x ||
        height - 1u > (uint32_t)-1 - y) {
        return -1;
    }
    if (terminal_fill_rect(x, y, width, height, GUI_WINDOW_BORDER_COLOR) != 0 ||
        terminal_fill_rect(x + 2u, y + 2u, width - 4u, height - 4u, GUI_BUTTON_COLOR) != 0) {
        return -1;
    }
    uint32_t text_width = (uint32_t)length * 8u;
    uint32_t text_x = x + (width > text_width ? (width - text_width) / 2u : 2u);
    uint32_t text_y = y + (height - 16u) / 2u;
    return terminal_draw_text_at(text_x, text_y, label, GUI_TEXT_COLOR, GUI_BUTTON_COLOR);
}

int gui_draw_label(uint32_t x, uint32_t y, const char* text) {
    if (!framebuffer_video_enabled || text == 0) {
        return -1;
    }
    return terminal_draw_text_at(x, y, text, GUI_TEXT_COLOR, GUI_WINDOW_COLOR);
}

int gui_draw_demo(void) {
    if (!framebuffer_video_enabled || framebuffer_width < 240u || framebuffer_height < 180u) {
        return -1;
    }

    uint32_t window_width = 220u;
    uint32_t window_height = 140u;
    uint32_t x = (framebuffer_width - window_width) / 2u;
    uint32_t y = (framebuffer_height - window_height) / 2u;
    if (terminal_fill_rect(0u, 0u, framebuffer_width, framebuffer_height, GUI_DESKTOP_COLOR) != 0 ||
        gui_draw_window(x, y, window_width, window_height, "JAK Desktop") != 0 ||
        gui_draw_label(x + 12u, y + 36u, "Framebuffer GUI prototype") != 0 ||
        gui_draw_button(x + 12u, y + 76u, 104u, 28u, "Demo button") != 0) {
        return -1;
    }
    return 0;
}
