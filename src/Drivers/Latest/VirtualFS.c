#include <stddef.h>
#include <stdint.h>

#include "../../kernel_utils.h"
#include "../DriverState.h"
#include "TerminalDriver.h"
#include "VirtualFS.h"

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
    char name[FS_MAX_NAME];
    char content[FS_MAX_CONTENT];
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

static struct vfs_node vfs_nodes[VFS_NODE_CAPACITY];
static int vfs_root_index;
struct fs_stats fs_current_stats;

enum { VFS_DISK_IMAGE_SECTORS = 160u };
static uint8_t vfs_disk_image[VFS_DISK_IMAGE_SECTORS * FS_DISK_BLOCK_SIZE];

static void vfs_stats_refresh(void) {
    uint32_t used_nodes = 0u;
    uint32_t used_names = 0u;
    uint32_t used_data_bytes = 0u;

    for (size_t i = 0; i < VFS_NODE_CAPACITY; i++) {
        if (!vfs_nodes[i].in_use) {
            continue;
        }
        used_nodes++;
        used_names++;
        if (vfs_nodes[i].type != VFS_NODE_DIR) {
            used_data_bytes += (uint32_t)vfs_nodes[i].content_length;
        }
    }

    fs_current_stats.total_blocks = FS_TOTAL_BLOCKS;
    fs_current_stats.meta_blocks = FS_META_BLOCKS;
    fs_current_stats.data_blocks = FS_DATA_BLOCKS;
    fs_current_stats.used_nodes = used_nodes;
    fs_current_stats.used_names = used_names;
    fs_current_stats.used_data_bytes = used_data_bytes;
    fs_current_stats.free_nodes = (uint32_t)(VFS_NODE_CAPACITY - used_nodes);
    fs_current_stats.dirty_nodes = 1u;
}

static size_t vfs_copy_token(char* output, size_t capacity, const char* start) {
    size_t length = 0;
    while (start[length] != '\0' && start[length] != '/' && length + 1 < capacity) {
        output[length] = start[length];
        length++;
    }
    output[length] = '\0';
    return length;
}

static int vfs_find_free_node(void) {
    for (int i = 0; i < (int)VFS_NODE_CAPACITY; i++) {
        if (!vfs_nodes[i].in_use) {
            return i;
        }
    }

    return -1;
}

static int vfs_find_child(int parent_index, const char* name) {
    int child = vfs_nodes[parent_index].first_child_index;
    while (child >= 0) {
        if (vfs_nodes[child].in_use && kernel_streq(vfs_nodes[child].name, name)) {
            return child;
        }
        child = vfs_nodes[child].next_sibling_index;
    }

    return -1;
}

static int vfs_create_node(const char* name, uint8_t type, int parent_index) {
    if (name[0] == '\0') {
        return -1;
    }

    int free_index = vfs_find_free_node();
    if (free_index < 0) {
        return -1;
    }

    size_t i = 0;
    while (name[i] != '\0' && i + 1 < VFS_NAME_CAPACITY) {
        vfs_nodes[free_index].name[i] = name[i];
        i++;
    }
    vfs_nodes[free_index].name[i] = '\0';
    vfs_nodes[free_index].type = type;
    vfs_nodes[free_index].in_use = 1;
    vfs_nodes[free_index].executable = (type == VFS_NODE_EXECUTABLE) ? 1u : 0u;
    vfs_nodes[free_index].parent_index = parent_index;
    vfs_nodes[free_index].first_child_index = -1;
    vfs_nodes[free_index].next_sibling_index = vfs_nodes[parent_index].first_child_index;
    vfs_nodes[parent_index].first_child_index = free_index;
    vfs_nodes[free_index].content_length = 0;
    vfs_nodes[free_index].content[0] = '\0';
    return free_index;
}

static int vfs_resolve_parent(const char* path, char* leaf_name, size_t leaf_capacity) {
    char normalized[VFS_NAME_CAPACITY * 4];
    if (vfs_normalize_path(path, normalized, sizeof(normalized)) != 0) {
        return -1;
    }

    if (kernel_streq(normalized, "/")) {
        leaf_name[0] = '\0';
        return vfs_root_index;
    }

    int current = vfs_root_index;
    const char* cursor = normalized + 1;
    while (*cursor != '\0') {
        char part[VFS_NAME_CAPACITY];
        size_t part_length = vfs_copy_token(part, sizeof(part), cursor);
        const char* next = cursor + part_length;
        while (*next == '/') {
            next++;
        }

        if (*next == '\0') {
            size_t i = 0;
            while (part[i] != '\0' && i + 1 < leaf_capacity) {
                leaf_name[i] = part[i];
                i++;
            }
            leaf_name[i] = '\0';
            return current;
        }

        int child = vfs_find_child(current, part);
        if (child < 0 || vfs_nodes[child].type != VFS_NODE_DIR) {
            return -1;
        }
        current = child;
        cursor = next;
    }

    return -1;
}

void vfs_init(void) {
    for (int i = 0; i < (int)VFS_NODE_CAPACITY; i++) {
        vfs_nodes[i].in_use = 0;
        vfs_nodes[i].name[0] = '\0';
        vfs_nodes[i].content[0] = '\0';
        vfs_nodes[i].content_length = 0;
        vfs_nodes[i].first_child_index = -1;
        vfs_nodes[i].next_sibling_index = -1;
        vfs_nodes[i].parent_index = -1;
        vfs_nodes[i].type = VFS_NODE_DIR;
        vfs_nodes[i].executable = 0;
    }

    vfs_root_index = 0;
    vfs_nodes[vfs_root_index].in_use = 1;
    vfs_nodes[vfs_root_index].type = VFS_NODE_DIR;
    vfs_nodes[vfs_root_index].name[0] = '/';
    vfs_nodes[vfs_root_index].name[1] = '\0';
    vfs_nodes[vfs_root_index].parent_index = -1;
    vfs_nodes[vfs_root_index].first_child_index = -1;
    vfs_nodes[vfs_root_index].next_sibling_index = -1;
    vfs_stats_refresh();
    fs_current_stats.loaded_from_disk = 0u;
    fs_current_stats.mounted = 1u;
}

int vfs_normalize_path(const char* path, char* output, size_t output_capacity) {
    if (path == 0 || output == 0 || output_capacity < 2) {
        return -1;
    }

    size_t out = 0;
    size_t i = 0;
    if (path[0] != '/') {
        output[out++] = '/';
    }

    while (path[i] != '\0') {
        if (path[i] == '/') {
            while (path[i] == '/') {
                i++;
            }
            if (path[i] == '\0') {
                break;
            }
            if (out > 0 && output[out - 1] != '/') {
                if (out + 1 >= output_capacity) {
                    return -1;
                }
                output[out++] = '/';
            }
            continue;
        }

        if (out + 1 >= output_capacity) {
            return -1;
        }
        output[out++] = path[i++];
    }

    if (out == 0) {
        output[out++] = '/';
    }

    if (out > 1 && output[out - 1] == '/') {
        out--;
    }

    output[out] = '\0';
    return 0;
}

int vfs_lookup(const char* path) {
    char leaf[VFS_NAME_CAPACITY];
    int parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
    if (parent < 0) {
        return -1;
    }

    if (leaf[0] == '\0') {
        return parent;
    }

    return vfs_find_child(parent, leaf);
}

const struct vfs_node* vfs_get_node(int index) {
    if (index < 0 || index >= (int)VFS_NODE_CAPACITY || !vfs_nodes[index].in_use) {
        return 0;
    }

    return &vfs_nodes[index];
}

int vfs_create_dir(const char* path) {
    char leaf[VFS_NAME_CAPACITY];
    int parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
    if (parent < 0 || leaf[0] == '\0') {
        return -1;
    }

    if (vfs_find_child(parent, leaf) >= 0) {
        return -1;
    }

    int result = vfs_create_node(leaf, VFS_NODE_DIR, parent);
    vfs_stats_refresh();
    return result;
}

int vfs_create_file(const char* path) {
    char leaf[VFS_NAME_CAPACITY];
    int parent = vfs_resolve_parent(path, leaf, sizeof(leaf));
    if (parent < 0 || leaf[0] == '\0') {
        return -1;
    }

    int existing = vfs_find_child(parent, leaf);
    if (existing >= 0) {
        if (vfs_nodes[existing].type == VFS_NODE_DIR) {
            return -1;
        }
        return existing;
    }

    int result = vfs_create_node(leaf, VFS_NODE_FILE, parent);
    vfs_stats_refresh();
    return result;
}

int vfs_mark_executable(const char* path) {
    int index = vfs_lookup(path);
    if (index < 0) {
        index = vfs_create_file(path);
    }

    if (index < 0 || vfs_nodes[index].type == VFS_NODE_DIR) {
        return -1;
    }

    vfs_nodes[index].type = VFS_NODE_EXECUTABLE;
    vfs_nodes[index].executable = 1u;
    vfs_stats_refresh();
    return index;
}

int vfs_write(const char* path, const char* content) {
    if (content == 0) {
        content = "";
    }

    int index = vfs_create_file(path);
    if (index < 0) {
        return -1;
    }

    size_t i = 0;
    while (content[i] != '\0' && i + 1 < VFS_CONTENT_CAPACITY) {
        vfs_nodes[index].content[i] = content[i];
        i++;
    }
    vfs_nodes[index].content[i] = '\0';
    vfs_nodes[index].content_length = i;
    vfs_stats_refresh();
    return index;
}

const char* vfs_read(const char* path) {
    int index = vfs_lookup(path);
    if (index < 0 || vfs_nodes[index].type == VFS_NODE_DIR) {
        return 0;
    }

    return vfs_nodes[index].content;
}

size_t vfs_list_children(const char* path, int* node_indexes, size_t capacity) {
    int index = vfs_lookup(path);
    if (index < 0 || vfs_nodes[index].type != VFS_NODE_DIR) {
        return 0;
    }

    size_t count = 0;
    int child = vfs_nodes[index].first_child_index;
    while (child >= 0) {
        if (node_indexes != 0 && count < capacity) {
            node_indexes[count] = child;
        }
        count++;
        child = vfs_nodes[child].next_sibling_index;
    }

    return count;
}

static void vfs_detach_from_parent(int index) {
    int parent = vfs_nodes[index].parent_index;
    if (parent < 0) {
        return;
    }

    int* link = &vfs_nodes[parent].first_child_index;
    while (*link >= 0) {
        if (*link == index) {
            *link = vfs_nodes[index].next_sibling_index;
            return;
        }
        link = &vfs_nodes[*link].next_sibling_index;
    }
}

static void vfs_clear_subtree(int index) {
    int child = vfs_nodes[index].first_child_index;
    while (child >= 0) {
        int next = vfs_nodes[child].next_sibling_index;
        vfs_clear_subtree(child);
        child = next;
    }

    vfs_nodes[index].in_use = 0;
    vfs_nodes[index].name[0] = '\0';
    vfs_nodes[index].content[0] = '\0';
    vfs_nodes[index].content_length = 0;
    vfs_nodes[index].first_child_index = -1;
    vfs_nodes[index].next_sibling_index = -1;
    vfs_nodes[index].parent_index = -1;
    vfs_nodes[index].type = VFS_NODE_DIR;
    vfs_nodes[index].executable = 0;
}

int vfs_remove(const char* path) {
    int index = vfs_lookup(path);
    if (index <= 0) {
        return -1;
    }

    vfs_detach_from_parent(index);
    vfs_clear_subtree(index);
    vfs_stats_refresh();
    return 0;
}

int vfs_disk_export(void* superblock_out, void* nodes_out, size_t nodes_capacity, size_t* node_count_out, void* names_out, size_t names_capacity, size_t* name_count_out) {
    if (superblock_out == 0 || nodes_out == 0 || node_count_out == 0 || names_out == 0 || name_count_out == 0) {
        return -1;
    }

    struct fs_disk_superblock* superblock = (struct fs_disk_superblock*)superblock_out;
    struct fs_disk_node* nodes = (struct fs_disk_node*)nodes_out;
    struct fs_disk_name_entry* names = (struct fs_disk_name_entry*)names_out;
    size_t node_count = 0;
    size_t name_count = 0;

    superblock->signature = FS_DISK_SIGNATURE;
    superblock->version = FS_VERSION;
    superblock->total_blocks = FS_TOTAL_BLOCKS;
    superblock->meta_blocks = FS_META_BLOCKS;
    superblock->data_blocks = FS_DATA_BLOCKS;
    superblock->root_node_index = (uint32_t)vfs_root_index;

    for (size_t i = 0; i < VFS_NODE_CAPACITY && node_count < nodes_capacity && name_count < names_capacity; i++) {
        if (!vfs_nodes[i].in_use) {
            continue;
        }

        nodes[node_count].in_use = 1u;
        nodes[node_count].kind = vfs_nodes[i].type;
        nodes[node_count].executable = vfs_nodes[i].executable;
        nodes[node_count].reserved0 = 0u;
        nodes[node_count].parent_index = vfs_nodes[i].parent_index;
        nodes[node_count].first_child_index = vfs_nodes[i].first_child_index;
        nodes[node_count].next_sibling_index = vfs_nodes[i].next_sibling_index;
        nodes[node_count].name_offset = (uint32_t)(name_count * FS_MAX_NAME);
        nodes[node_count].name_length = 0u;
        nodes[node_count].content_offset = 0u;
        nodes[node_count].content_length = (uint32_t)vfs_nodes[i].content_length;
        nodes[node_count].content_capacity = FS_MAX_CONTENT;
        for (size_t j = 0; j < FS_MAX_NAME; j++) {
            nodes[node_count].name[j] = '\0';
        }
        for (size_t j = 0; j < FS_MAX_NAME - 1 && vfs_nodes[i].name[j] != '\0'; j++) {
            nodes[node_count].name[j] = vfs_nodes[i].name[j];
            nodes[node_count].name_length++;
        }
        for (size_t j = 0; j < FS_MAX_NAME; j++) {
            ((char*)names)[name_count * FS_MAX_NAME + j] = '\0';
        }
        for (size_t j = 0; j < FS_MAX_NAME - 1 && vfs_nodes[i].name[j] != '\0'; j++) {
            ((char*)names)[name_count * FS_MAX_NAME + j] = vfs_nodes[i].name[j];
        }
        names[name_count].node_index = (uint32_t)node_count;
        names[name_count].parent_index = (uint32_t)vfs_nodes[i].parent_index;
        names[name_count].name_offset = nodes[node_count].name_offset;
        names[name_count].name_length = nodes[node_count].name_length;
        names[name_count].kind = vfs_nodes[i].type;
        names[name_count].flags = vfs_nodes[i].executable;
        names[name_count].reserved = 0u;

        for (size_t j = 0; j < FS_MAX_CONTENT; j++) {
            nodes[node_count].content[j] = '\0';
        }
        for (size_t j = 0; j < vfs_nodes[i].content_length && j + 1 < FS_MAX_CONTENT; j++) {
            nodes[node_count].content[j] = vfs_nodes[i].content[j];
        }

        node_count++;
        name_count++;
    }

    superblock->node_count = (uint32_t)node_count;
    superblock->name_count = (uint32_t)name_count;
    superblock->dirty_generation = fs_current_stats.dirty_nodes;
    *node_count_out = node_count;
    *name_count_out = name_count;
    return 0;
}

int vfs_disk_write_image_with_layout(const void* superblock, const void* nodes, size_t node_count, const void* names, size_t name_count) {
    const uint8_t* superblock_bytes = (const uint8_t*)superblock;
    const uint8_t* node_bytes = (const uint8_t*)nodes;
    const uint8_t* name_bytes = (const uint8_t*)names;
    size_t superblock_size = sizeof(struct fs_disk_superblock);
    size_t node_size = sizeof(struct fs_disk_node);
    size_t name_size = sizeof(struct fs_disk_name_entry);
    size_t offset = 0;

    for (size_t i = 0; i < sizeof(vfs_disk_image); i++) {
        vfs_disk_image[i] = 0;
    }

    if (superblock_size > sizeof(vfs_disk_image)) {
        return -1;
    }

    for (size_t i = 0; i < superblock_size; i++) {
        vfs_disk_image[offset++] = superblock_bytes[i];
    }

    size_t node_bytes_total = node_count * node_size;
    if (offset + node_bytes_total >= sizeof(vfs_disk_image)) {
        return -1;
    }
    for (size_t i = 0; i < node_bytes_total; i++) {
        vfs_disk_image[offset++] = node_bytes[i];
    }

    size_t name_bytes_total = name_count * name_size;
    if (offset + name_bytes_total >= sizeof(vfs_disk_image)) {
        return -1;
    }
    for (size_t i = 0; i < name_bytes_total; i++) {
        vfs_disk_image[offset++] = name_bytes[i];
    }

    if (!disk_write_sectors(0u, (uint8_t)(sizeof(vfs_disk_image) / FS_DISK_BLOCK_SIZE), vfs_disk_image)) {
        return -1;
    }

    return 0;
}

int vfs_disk_write_image(void) {
    struct fs_disk_superblock superblock;
    struct fs_disk_node nodes[VFS_NODE_CAPACITY];
    struct fs_disk_name_entry names[VFS_NODE_CAPACITY];
    size_t node_count = 0;
    size_t name_count = 0;

    if (vfs_disk_export(&superblock, nodes, VFS_NODE_CAPACITY, &node_count, names, VFS_NODE_CAPACITY, &name_count) != 0) {
        return -1;
    }

    return vfs_disk_write_image_with_layout(&superblock, nodes, node_count, names, name_count);
}

int vfs_disk_read_image(void* superblock_out, void* nodes_out, size_t nodes_capacity, void* names_out, size_t names_capacity) {
    if (superblock_out == 0 || nodes_out == 0 || names_out == 0) {
        return -1;
    }

    if (!disk_read_sectors(0u, (uint8_t)(sizeof(vfs_disk_image) / FS_DISK_BLOCK_SIZE), vfs_disk_image)) {
        return -1;
    }

    const struct fs_disk_superblock* superblock = (const struct fs_disk_superblock*)vfs_disk_image;
    size_t offset = sizeof(struct fs_disk_superblock);
    size_t node_size = sizeof(struct fs_disk_node);
    size_t name_size = sizeof(struct fs_disk_name_entry);
    struct fs_disk_node* nodes = (struct fs_disk_node*)nodes_out;
    struct fs_disk_name_entry* names = (struct fs_disk_name_entry*)names_out;

    if (offset + nodes_capacity * node_size >= sizeof(vfs_disk_image)) {
        return -1;
    }

    *(struct fs_disk_superblock*)superblock_out = *superblock;

    size_t node_count = superblock->node_count;
    if (node_count > nodes_capacity) {
        node_count = nodes_capacity;
    }

    for (size_t i = 0; i < node_count; i++) {
        const struct fs_disk_node* disk_node = (const struct fs_disk_node*)(vfs_disk_image + offset + i * node_size);
        nodes[i] = *disk_node;
    }

    offset += superblock->node_count * node_size;
    if (offset >= sizeof(vfs_disk_image)) {
        return -1;
    }

    size_t name_count = superblock->name_count;
    if (name_count > names_capacity) {
        name_count = names_capacity;
    }

    for (size_t i = 0; i < name_count; i++) {
        const struct fs_disk_name_entry* disk_name = (const struct fs_disk_name_entry*)(vfs_disk_image + offset + i * name_size);
        names[i] = *disk_name;
    }

    return 0;
}

void vfs_disk_import(const void* nodes, size_t node_count) {
    const struct fs_disk_node* disk_nodes = (const struct fs_disk_node*)nodes;
    for (size_t i = 0; i < VFS_NODE_CAPACITY; i++) {
        vfs_nodes[i].in_use = 0;
        vfs_nodes[i].name[0] = '\0';
        vfs_nodes[i].content[0] = '\0';
        vfs_nodes[i].content_length = 0;
        vfs_nodes[i].first_child_index = -1;
        vfs_nodes[i].next_sibling_index = -1;
        vfs_nodes[i].parent_index = -1;
        vfs_nodes[i].type = VFS_NODE_DIR;
        vfs_nodes[i].executable = 0;
    }

    vfs_root_index = 0;
    if (node_count == 0) {
        vfs_init();
        return;
    }

    for (size_t i = 0; i < node_count && i < VFS_NODE_CAPACITY; i++) {
        vfs_nodes[i].in_use = disk_nodes[i].in_use;
        vfs_nodes[i].type = (disk_nodes[i].kind == FS_NODE_EXECUTABLE) ? VFS_NODE_EXECUTABLE : (disk_nodes[i].kind == FS_NODE_FILE ? VFS_NODE_FILE : VFS_NODE_DIR);
        vfs_nodes[i].executable = disk_nodes[i].executable;
        vfs_nodes[i].parent_index = disk_nodes[i].parent_index;
        vfs_nodes[i].first_child_index = disk_nodes[i].first_child_index;
        vfs_nodes[i].next_sibling_index = disk_nodes[i].next_sibling_index;
        vfs_nodes[i].content_length = disk_nodes[i].content_length;
        size_t name_length = 0;
        while (name_length + 1 < VFS_NAME_CAPACITY && disk_nodes[i].name[name_length] != '\0') {
            vfs_nodes[i].name[name_length] = disk_nodes[i].name[name_length];
            name_length++;
        }
        vfs_nodes[i].name[name_length] = '\0';
        size_t content_length = disk_nodes[i].content_length;
        if (content_length >= VFS_CONTENT_CAPACITY) {
            content_length = VFS_CONTENT_CAPACITY - 1;
        }
        for (size_t j = 0; j < content_length; j++) {
            vfs_nodes[i].content[j] = disk_nodes[i].content[j];
        }
        vfs_nodes[i].content[content_length] = '\0';
    }

    if (!vfs_nodes[0].in_use) {
        vfs_init();
    }
    vfs_stats_refresh();
}

const char* vfs_node_type_name(uint8_t type) {
    switch (type) {
        case VFS_NODE_DIR: return "directory";
        case VFS_NODE_FILE: return "file";
        case VFS_NODE_EXECUTABLE: return "executable";
        default: return "unknown";
    }
}