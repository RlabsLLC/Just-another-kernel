#ifndef RTCI_H
#define RTCI_H

#include <stddef.h>

int rtci_init(void);
int rtci_run_path(const char* path);
void rtci_print_startup_ok(const char* script_name);
size_t rtci_list_services(int* node_indexes, size_t capacity);

#endif