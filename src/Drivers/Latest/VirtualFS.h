#ifndef VIRTUAL_FS_H
#define VIRTUAL_FS_H

#include <stddef.h>
#include <stdint.h>

enum { VFS_NAME_CAPACITY = 64 };
enum { VFS_CONTENT_CAPACITY = 1024 };
enum { VFS_NODE_CAPACITY = 64 };

enum vfs_node_type {
    VFS_NODE_DIR = 0,
    VFS_NODE_FILE = 1,
    VFS_NODE_EXECUTABLE = 2
};

struct vfs_node {
    char name[VFS_NAME_CAPACITY];
    uint8_t type;
    uint8_t in_use;
    uint8_t executable;
    uint8_t reserved;
    int parent_index;
    int first_child_index;
    int next_sibling_index;
    size_t content_length;
    char content[VFS_CONTENT_CAPACITY];
};

void vfs_init(void);
int vfs_disk_read_image(void* superblock_out, void* nodes_out, size_t nodes_capacity, void* names_out, size_t names_capacity);
int vfs_disk_write_image(void);
int vfs_disk_write_image_with_layout(const void* superblock, const void* nodes, size_t node_count, const void* names, size_t name_count);
int vfs_disk_export(void* superblock_out, void* nodes_out, size_t nodes_capacity, size_t* node_count_out, void* names_out, size_t names_capacity, size_t* name_count_out);
void vfs_disk_import(const void* nodes, size_t node_count);
int vfs_lookup(const char* path);
int vfs_create_dir(const char* path);
int vfs_create_file(const char* path);
int vfs_write(const char* path, const char* content);
const char* vfs_read(const char* path);
size_t vfs_list_children(const char* path, int* node_indexes, size_t capacity);
int vfs_remove(const char* path);
int vfs_mark_executable(const char* path);
const struct vfs_node* vfs_get_node(int index);
const char* vfs_node_type_name(uint8_t type);
int vfs_normalize_path(const char* path, char* output, size_t output_capacity);

#endif