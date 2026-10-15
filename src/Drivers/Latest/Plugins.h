#ifndef PLUGINS_H
#define PLUGINS_H

#include <stddef.h>

typedef int (*plugin_handler)(const char* path);

int plugins_init(void);
int plugin_register(const char* path, plugin_handler handler);
int plugin_run_path(const char* path);
void plugins_print_startup_ok(const char* plugin_name);
size_t plugins_list(int* node_indexes, size_t capacity);

#endif