#include "kernel_heap.h"

#include <stdint.h>

typedef union heap_block heap_block;

union heap_block {
    struct {
        size_t size;
        uint8_t free;
        heap_block* next;
    } fields;
    uint64_t alignment;
};

static uint8_t kernel_heap[KERNEL_HEAP_CAPACITY] __attribute__((aligned(8)));
static heap_block* heap_first_block;
static uint8_t heap_initialized;

void kheap_init(void) {
    heap_first_block = (heap_block*)kernel_heap;
    heap_first_block->fields.size = sizeof(kernel_heap) - sizeof(heap_block);
    heap_first_block->fields.free = 1u;
    heap_first_block->fields.next = 0;
    heap_initialized = 1u;
}

void* kheap_alloc(size_t size) {
    if (!heap_initialized) {
        kheap_init();
    }
    if (size == 0 || size > (size_t)-1 - 7u) {
        return 0;
    }
    size = (size + 7u) & ~(size_t)7u;

    for (heap_block* block = heap_first_block; block != 0; block = block->fields.next) {
        if (!block->fields.free || block->fields.size < size) {
            continue;
        }
        if (block->fields.size - size >= sizeof(heap_block) + 8u) {
            heap_block* remainder = (heap_block*)((uint8_t*)(block + 1) + size);
            remainder->fields.size = block->fields.size - size - sizeof(heap_block);
            remainder->fields.free = 1u;
            remainder->fields.next = block->fields.next;
            block->fields.next = remainder;
            block->fields.size = size;
        }
        block->fields.free = 0u;
        return block + 1;
    }
    return 0;
}

int kheap_free(void* pointer) {
    if (pointer == 0 || !heap_initialized) {
        return -1;
    }

    heap_block* previous = 0;
    for (heap_block* block = heap_first_block; block != 0; block = block->fields.next) {
        if ((void*)(block + 1) == pointer) {
            if (block->fields.free) {
                return -1;
            }
            block->fields.free = 1u;
            if (block->fields.next != 0 && block->fields.next->fields.free) {
                block->fields.size += sizeof(heap_block) + block->fields.next->fields.size;
                block->fields.next = block->fields.next->fields.next;
            }
            if (previous != 0 && previous->fields.free) {
                previous->fields.size += sizeof(heap_block) + block->fields.size;
                previous->fields.next = block->fields.next;
            }
            return 0;
        }
        previous = block;
    }
    return -1;
}

size_t kheap_capacity(void) {
    return sizeof(kernel_heap);
}

size_t kheap_free_bytes(void) {
    if (!heap_initialized) {
        return sizeof(kernel_heap) - sizeof(heap_block);
    }
    size_t available = 0;
    for (heap_block* block = heap_first_block; block != 0; block = block->fields.next) {
        if (block->fields.free) {
            available += block->fields.size;
        }
    }
    return available;
}
