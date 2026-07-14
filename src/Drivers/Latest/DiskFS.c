#include <stddef.h>
#include <stdint.h>

#include "../../kernel_utils.h"
#include "../DriverState.h"
#include "DiskFS.h"
#include "TerminalDriver.h"
#include "VirtualFS.h"

enum { FS_NAME_AREA_BYTES = 8u * 1024u * 1024u };

struct fs_disk_superblock {
    uint32_t signature;
    uint32_t version;
    uint32_t total_blocks;
    uint32_t meta_blocks;
    uint32_t data_blocks;
    uint32_t root_node_index;
    uint32_t node_count;
    uint32_t name_count;
    uint32_t dirty_generation;
    uint32_t reserved[7];
} __attribute__((packed));

struct fs_disk_node {
    uint8_t in_use;
    uint8_t kind;
    uint8_t executable;
    uint8_t reserved0;
    int32_t parent_index;
    int32_t first_child_index;
    int32_t next_sibling_index;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t content_offset;
    uint32_t content_length;
    uint32_t content_capacity;
    uint8_t reserved1[28];
} __attribute__((packed));

struct fs_disk_name_entry {
    uint32_t node_index;
    uint32_t parent_index;
    uint32_t name_offset;
    uint32_t name_length;
    uint8_t kind;
    uint8_t flags;
    uint16_t reserved;
} __attribute__((packed));

static uint8_t fs_loaded_from_disk;
static uint8_t fs_mounted;
static struct fs_sync_issue fs_sync_issues[24];
static size_t fs_sync_issue_count;

static int fs_is_name_indexed(int node_index, const struct fs_disk_name_entry* names, size_t name_count) {
    for (size_t i = 0; i < name_count; i++) {
        if ((int)names[i].node_index == node_index) {
            return 1;
        }
    }
    return 0;
}

void diskfs_init(void) {
    fs_loaded_from_disk = 0u;
    fs_mounted = 0u;
    fs_sync_issue_count = 0;
    fs_current_stats.total_blocks = FS_TOTAL_BLOCKS;
    fs_current_stats.meta_blocks = FS_META_BLOCKS;
    fs_current_stats.data_blocks = FS_DATA_BLOCKS;
}

int diskfs_mount_or_format(void) {
    struct fs_disk_superblock superblock;
    struct fs_disk_node nodes[FS_MAX_NODES];
    struct fs_disk_name_entry names[FS_MAX_NODES];

    if (vfs_disk_read_image(&superblock, nodes, FS_MAX_NODES, names, FS_MAX_NODES) != 0 ||
        superblock.signature != FS_DISK_SIGNATURE ||
        superblock.version != FS_VERSION) {
        vfs_init();
        if (vfs_disk_write_image() != 0) {
            terminal_write("fs mount: failed to format disk image\n");
            return -1;
        }
        fs_loaded_from_disk = 0u;
        fs_mounted = 1u;
        fs_current_stats.loaded_from_disk = 0u;
        fs_current_stats.mounted = 1u;
        return 1;
    }

    vfs_disk_import(nodes, FS_MAX_NODES);
    fs_loaded_from_disk = 1u;
    fs_mounted = 1u;
    fs_current_stats.loaded_from_disk = 1u;
    fs_current_stats.mounted = 1u;
    return 0;
}

int diskfs_sync(void) {
    struct fs_disk_superblock superblock;
    struct fs_disk_node nodes[FS_MAX_NODES];
    struct fs_disk_name_entry names[FS_MAX_NODES];
    size_t node_count = 0;
    size_t name_count = 0;

    if (vfs_disk_export(&superblock, nodes, FS_MAX_NODES, &node_count, names, FS_MAX_NODES, &name_count) != 0) {
        terminal_write("fs sync: export failed\n");
        return -1;
    }

    fs_sync_issue_count = 0;
    for (size_t i = 0; i < node_count && fs_sync_issue_count < sizeof(fs_sync_issues) / sizeof(fs_sync_issues[0]); i++) {
        if (nodes[i].in_use && nodes[i].kind != FS_NODE_FREE && !fs_is_name_indexed((int)i, names, name_count)) {
            fs_sync_issues[fs_sync_issue_count++] = (struct fs_sync_issue){ FS_SYNC_ISSUE_UNUSED_NAME, (int)i, nodes[i].parent_index };
        }
    }

    for (size_t i = 0; i < name_count && fs_sync_issue_count < sizeof(fs_sync_issues) / sizeof(fs_sync_issues[0]); i++) {
        if ((int)names[i].node_index < 0 || (size_t)names[i].node_index >= node_count || !nodes[names[i].node_index].in_use) {
            fs_sync_issues[fs_sync_issue_count++] = (struct fs_sync_issue){ FS_SYNC_ISSUE_ORPHAN, (int)names[i].node_index, (int)names[i].parent_index };
        }
    }

    if (vfs_disk_write_image_with_layout(&superblock, nodes, node_count, names, name_count) != 0) {
        terminal_write("fs sync: write-back failed\n");
        return -1;
    }

    fs_current_stats.sync_warnings = (uint32_t)fs_sync_issue_count;
    terminal_write("fs sync: scanned disk and reconciled metadata; warnings: ");
    terminal_write_uint((uint32_t)fs_sync_issue_count);
    terminal_putchar('\n');
    return 0;
}

int diskfs_flash(void) {
    vfs_init();
    if (vfs_disk_write_image() != 0) {
        terminal_write("fs flash: disk write failed\n");
        return -1;
    }
    terminal_write("fs flash: filesystem reset and repopulated in RAM\n");
    return 0;
}

int diskfs_bootstrap_with_fallback(uint8_t force_ram_vfs, uint32_t timeout_ticks) {
    if (force_ram_vfs != 0) {
        vfs_init();
        fs_loaded_from_disk = 0u;
        fs_mounted = 1u;
        fs_current_stats.loaded_from_disk = 0u;
        fs_current_stats.mounted = 1u;
        terminal_write("fs bootstrap: forced RAM VFS fallback\n");
        return 1;
    }

    terminal_write("fs bootstrap: probing persistent disk layer\n");
    terminal_boot_delay_ticks(timeout_ticks);
    if (diskfs_mount_or_format() < 0) {
        vfs_init();
        terminal_write("fs bootstrap: persistent layer timed out or failed, using RAM VFS\n");
        return 1;
    }

    return 0;
}

void diskfs_print_info(void) {
    fs_current_stats.total_blocks = FS_TOTAL_BLOCKS;
    fs_current_stats.meta_blocks = FS_META_BLOCKS;
    fs_current_stats.data_blocks = FS_DATA_BLOCKS;
    fs_current_stats.loaded_from_disk = fs_loaded_from_disk;
    fs_current_stats.mounted = fs_mounted;

    terminal_write("fs info:\n");
    terminal_write("  total blocks: ");
    terminal_write_uint(fs_current_stats.total_blocks);
    terminal_putchar('\n');
    terminal_write("  meta blocks (92% OS): ");
    terminal_write_uint(fs_current_stats.meta_blocks);
    terminal_putchar('\n');
    terminal_write("  name/data blocks (8% index area): ");
    terminal_write_uint(fs_current_stats.data_blocks);
    terminal_putchar('\n');
    terminal_write("  mounted: ");
    terminal_write(fs_current_stats.mounted ? "yes" : "no");
    terminal_putchar('\n');
    terminal_write("  loaded from disk: ");
    terminal_write(fs_current_stats.loaded_from_disk ? "yes" : "no");
    terminal_putchar('\n');
    terminal_write("  used nodes: ");
    terminal_write_uint(fs_current_stats.used_nodes);
    terminal_putchar('\n');
    terminal_write("  used data bytes: ");
    terminal_write_uint(fs_current_stats.used_data_bytes);
    terminal_putchar('\n');
    terminal_write("  dirty nodes: ");
    terminal_write_uint(fs_current_stats.dirty_nodes);
    terminal_putchar('\n');
    terminal_write("  sync warnings: ");
    terminal_write_uint(fs_current_stats.sync_warnings);
    terminal_putchar('\n');
}