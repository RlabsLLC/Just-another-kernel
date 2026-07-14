#ifndef DRIVER_STATE_H
#define DRIVER_STATE_H

#include <stddef.h>
#include <stdint.h>

enum { DRIVER_STATUS_CAPACITY = 8 };

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

enum {
    FS_DISK_SIGNATURE = 0x4A414B46u,
    FS_VERSION = 1u,
    FS_MAX_NODES = 64u,
    FS_MAX_NAME = 64u,
    FS_MAX_CONTENT = 1024u,
    FS_DISK_BLOCK_SIZE = 512u,
    FS_TOTAL_BLOCKS = 131072u,
    FS_META_BLOCKS = 10486u,
    FS_DATA_BLOCKS = FS_TOTAL_BLOCKS - FS_META_BLOCKS
};

enum fs_node_kind {
    FS_NODE_FREE = 0,
    FS_NODE_DIR = 1,
    FS_NODE_FILE = 2,
    FS_NODE_EXECUTABLE = 3
};

enum fs_sync_issue_kind {
    FS_SYNC_ISSUE_ORPHAN = 1,
    FS_SYNC_ISSUE_UNUSED_NAME = 2,
    FS_SYNC_ISSUE_STALE_INDEX = 3
};

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

struct fs_stats {
    uint32_t total_blocks;
    uint32_t meta_blocks;
    uint32_t data_blocks;
    uint32_t used_nodes;
    uint32_t used_names;
    uint32_t used_data_bytes;
    uint32_t free_nodes;
    uint32_t dirty_nodes;
    uint32_t sync_warnings;
    uint8_t loaded_from_disk;
    uint8_t mounted;
};

struct fs_sync_issue {
    uint8_t kind;
    int node_index;
    int parent_index;
};

extern uint8_t terminal_vga_enabled;
extern uint8_t serial_console_enabled;
extern uint8_t framebuffer_video_enabled;
extern uint8_t vga_text_available;
extern uint8_t driver_status_overflowed;
extern uint8_t framebuffer_reject_non_32bpp;
extern uint8_t framebuffer_reject_reason;
extern uint8_t ps2_keyboard_available;
extern uint8_t ps2_mouse_available;

extern volatile uint8_t* framebuffer_memory;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint32_t framebuffer_pitch;
extern uint8_t framebuffer_bpp;
extern size_t framebuffer_text_columns;
extern size_t framebuffer_text_rows;

extern const struct multiboot_info* boot_mbi;
extern uint32_t kernel_poll_ticks;
extern uint8_t boot_mb2_mem_available;
extern uint32_t boot_mb2_mem_lower;
extern uint32_t boot_mb2_mem_upper;

extern struct driver_status driver_statuses[DRIVER_STATUS_CAPACITY];
extern size_t driver_status_count;

extern struct fs_stats fs_current_stats;

#endif