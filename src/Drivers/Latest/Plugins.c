#include <stddef.h>

#include "../../kernel_utils.h"
#include "Plugins.h"
#include "VirtualFS.h"

struct plugin_entry {
    const char* path;
    plugin_handler handler;
};

enum { PLUGIN_CAPACITY = 16 };

static struct plugin_entry plugins[PLUGIN_CAPACITY];
static size_t plugin_count;

static int plugin_bootstrap_handler(const char* path) {
    (void)path;
    terminal_write("Built-in vSH plugin handler executed.\n");
    return 0;
}

int plugin_register(const char* path, plugin_handler handler) {
    if (path == 0 || handler == 0 || path[0] != '/' || plugin_count >= PLUGIN_CAPACITY) {
        return -1;
    }
    for (size_t i = 0; i < plugin_count; i++) {
        if (kernel_streq(plugins[i].path, path)) {
            return -1;
        }
    }
    plugins[plugin_count++] = (struct plugin_entry){ path, handler };
    return 0;
}

static int plugin_ensure_directory(const char* path) {
    int index = vfs_lookup(path);
    if (index >= 0) {
        const struct vfs_node* node = vfs_get_node(index);
        return node != 0 && node->type == VFS_NODE_DIR ? 0 : -1;
    }
    return vfs_create_dir(path) < 0 ? -1 : 0;
}

int plugins_init(void) {
    plugin_count = 0;
    if (plugin_ensure_directory("/bin") != 0 ||
        plugin_ensure_directory("/etc") != 0 ||
        plugin_ensure_directory("/dev") != 0 ||
        plugin_ensure_directory("/home") != 0 ||
        plugin_ensure_directory("/tmp") != 0 ||
        plugin_ensure_directory("/proc") != 0) {
        return -1;
    }
    if (vfs_create_file("/etc/hostname") < 0 && vfs_lookup("/etc/hostname") < 0) {
        return -1;
    }
    if (vfs_write("/etc/hostname", "jak") < 0 ||
        vfs_create_file("/bin/vsh") < 0 ||
        vfs_mark_executable("/bin/vsh") < 0 ||
        plugin_register("/bin/vsh", plugin_bootstrap_handler) != 0) {
        return -1;
    }
    return 0;
}

static int plugin_match_path(const char* path, plugin_handler* handler_out) {
    for (size_t i = 0; i < plugin_count; i++) {
        if (kernel_streq(plugins[i].path, path)) {
            *handler_out = plugins[i].handler;
            return 0;
        }
    }

    return -1;
}

int plugin_run_path(const char* path) {
    if (path == 0 || path[0] == '\0') {
        terminal_write("plugin: missing path\n");
        return -1;
    }
    int index = vfs_lookup(path);
    if (index < 0) {
        terminal_write("plugin: not found: ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    const struct vfs_node* node = vfs_get_node(index);
    if (node == 0) {
        terminal_write("plugin: invalid entry\n");
        return -1;
    }

    if (node->type == VFS_NODE_DIR) {
        terminal_write("plugin: cannot run directory: ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    if (!node->executable) {
        terminal_write("plugin: not executable: ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    plugin_handler handler = 0;
    if (plugin_match_path(path, &handler) != 0) {
        terminal_write("plugin: no handler registered for ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    return handler(path);
}

void plugins_print_startup_ok(const char* plugin_name) {
    uint8_t fg = terminal_get_fg_color();
    uint8_t bg = terminal_get_bg_color();
    terminal_set_color(10u, bg);
    terminal_write("[ OK ] Plugin - Load ");
    terminal_write(plugin_name);
    terminal_putchar('\n');
    terminal_set_color(fg, bg);
}

size_t plugins_list(int* node_indexes, size_t capacity) {
    size_t count = 0;
    for (size_t i = 0; i < plugin_count; i++) {
        if (node_indexes != 0 && count < capacity) {
            node_indexes[count] = vfs_lookup(plugins[i].path);
        }
        count++;
    }

    return count;
}