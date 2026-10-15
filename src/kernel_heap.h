#ifndef KERNEL_HEAP_H
#define KERNEL_HEAP_H

#include <stddef.h>

enum { KERNEL_HEAP_CAPACITY = 64u * 1024u };

void kheap_init(void);
void* kheap_alloc(size_t size);
int kheap_free(void* pointer);
size_t kheap_capacity(void);
size_t kheap_free_bytes(void);

#endif
