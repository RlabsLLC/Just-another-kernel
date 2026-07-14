#include <stddef.h>

#include "../../kernel_utils.h"
#include "RTCI.h"
#include "VirtualFS.h"

typedef int (*rtci_service_handler)(const char* path);

struct rtci_service_entry {
    const char* path;
    rtci_service_handler handler;
};

static struct rtci_service_entry rtci_services[8];
static size_t rtci_service_count;

static int rtci_bootstrap_service(const char* path) {
    (void)path;
    terminal_write("RTCI service executed.\n");
    return 0;
}

int rtci_init(void) {
    rtci_service_count = 0;

    if (vfs_create_dir("/bin") < 0) return -1;
    if (vfs_create_dir("/etc") < 0) return -1;
    if (vfs_create_dir("/dev") < 0) return -1;
    if (vfs_create_dir("/home") < 0) return -1;
    if (vfs_create_dir("/tmp") < 0) return -1;
    if (vfs_create_dir("/proc") < 0) return -1;
    if (vfs_create_file("/etc/hostname") < 0) return -1;
    if (vfs_write("/etc/hostname", "jak") < 0) return -1;
    if (vfs_create_file("/bin/yBash.C") < 0) return -1;
    if (vfs_mark_executable("/bin/yBash.C") < 0) return -1;

    rtci_services[rtci_service_count++] = (struct rtci_service_entry){ "/bin/yBash.C", rtci_bootstrap_service };
    return 0;
}

static int rtci_match_service(const char* path, rtci_service_handler* handler_out) {
    for (size_t i = 0; i < rtci_service_count; i++) {
        if (kernel_streq(rtci_services[i].path, path)) {
            *handler_out = rtci_services[i].handler;
            return 0;
        }
    }

    return -1;
}

int rtci_run_path(const char* path) {
    int index = vfs_lookup(path);
    if (index < 0) {
        terminal_write("RTCI: not found: ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    const struct vfs_node* node = vfs_get_node(index);
    if (node == 0) {
        terminal_write("RTCI: invalid entry\n");
        return -1;
    }

    if (node->type == VFS_NODE_DIR) {
        terminal_write("RTCI: cannot run directory: ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    if (!node->executable && !kernel_starts_with(path, "/bin/") && !kernel_starts_with(path, "bin/")) {
        terminal_write("RTCI: not executable: ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    rtci_service_handler handler = 0;
    if (rtci_match_service(path, &handler) != 0) {
        terminal_write("RTCI: no handler registered for ");
        terminal_write(path);
        terminal_putchar('\n');
        return -1;
    }

    return handler(path);
}

void rtci_print_startup_ok(const char* script_name) {
    uint8_t fg = terminal_get_fg_color();
    uint8_t bg = terminal_get_bg_color();
    terminal_set_color(10u, bg);
    terminal_write("[ OK ] RTCI - Load ");
    terminal_write(script_name);
    terminal_putchar('\n');
    terminal_set_color(fg, bg);
}

size_t rtci_list_services(int* node_indexes, size_t capacity) {
    size_t count = 0;
    for (size_t i = 0; i < rtci_service_count; i++) {
        if (node_indexes != 0 && count < capacity) {
            node_indexes[count] = vfs_lookup(rtci_services[i].path);
        }
        count++;
    }

    return count;
}