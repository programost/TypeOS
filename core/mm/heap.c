#include "include/heap.h"
#include "include/paging.h"
#include "include/types.h"

#define HEAP_MIN_EXPAND   (64 * 1024)
#define HEAP_ALIGN        16

typedef struct heap_block {
        size_t size;
        bool free;
        struct heap_block* next;
        struct heap_block* prev;
} heap_block_t;

static heap_block_t* heap_head = NULL;
static uint64_t heap_current = KERNEL_HEAP_START;
static uint64_t heap_mapped_end = KERNEL_HEAP_START;

static size_t align_up(size_t value, size_t align) {
        return (value + align - 1U) & ~(align - 1U);
}

static int heap_map_more(size_t bytes) {
        uint64_t target = heap_mapped_end + bytes;
        if (target > KERNEL_HEAP_LIMIT) {
                target = KERNEL_HEAP_LIMIT;
        }

        while (heap_mapped_end < target) {
                void* frame = paging_alloc_page();
                if (!frame) {
                        return -1;
                }
                if (paging_map_page(heap_mapped_end, (uint64_t)(uintptr_t)frame,
                                                        PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                        paging_free_page(frame);
                        return -1;
                }
                heap_mapped_end += PAGE_SIZE;
        }

        return 0;
}

static heap_block_t* split_block(heap_block_t* block, size_t size) {
        if (block->size < size + sizeof(heap_block_t) + HEAP_ALIGN) {
                return block;
        }

        heap_block_t* new_block = (heap_block_t*)((uint8_t*)block + sizeof(heap_block_t) + size);
        new_block->size = block->size - size - sizeof(heap_block_t);
        new_block->free = true;
        new_block->next = block->next;
        new_block->prev = block;

        if (new_block->next) {
                new_block->next->prev = new_block;
        }

        block->size = size;
        block->next = new_block;

        return block;
}

static void coalesce(heap_block_t* block) {
        if (block->next && block->next->free) {
                block->size += sizeof(heap_block_t) + block->next->size;
                block->next = block->next->next;
                if (block->next) {
                        block->next->prev = block;
                }
        }

        if (block->prev && block->prev->free) {
                block->prev->size += sizeof(heap_block_t) + block->size;
                block->prev->next = block->next;
                if (block->next) {
                        block->next->prev = block->prev;
                }
        }
}

void heap_init(void) {
        heap_head = NULL;
        heap_current = KERNEL_HEAP_START;
        heap_mapped_end = KERNEL_HEAP_START;

        if (heap_map_more(HEAP_MIN_EXPAND) != 0) {
                return;
        }

        heap_head = (heap_block_t*)heap_current;
        heap_head->size = HEAP_MIN_EXPAND - sizeof(heap_block_t);
        heap_head->free = true;
        heap_head->next = NULL;
        heap_head->prev = NULL;
        heap_current += HEAP_MIN_EXPAND;
}

void* kmalloc(size_t size) {
        if (size == 0) {
                return NULL;
        }

        size = align_up(size, HEAP_ALIGN);

        for (heap_block_t* block = heap_head; block; block = block->next) {
                if (!block->free || block->size < size) {
                        continue;
                }

                block->free = false;
                return (uint8_t*)split_block(block, size) + sizeof(heap_block_t);
        }

        size_t need = size + sizeof(heap_block_t);
        size_t expand = align_up(need, PAGE_SIZE);
        if (expand < HEAP_MIN_EXPAND) {
                expand = HEAP_MIN_EXPAND;
        }

        if (heap_map_more(expand) != 0) {
                return NULL;
        }

        heap_block_t* block = (heap_block_t*)heap_current;
        block->size = expand - sizeof(heap_block_t);
        block->free = false;
        block->next = NULL;
        block->prev = NULL;

        if (heap_head) {
                heap_block_t* tail = heap_head;
                while (tail->next) {
                        tail = tail->next;
                }
                tail->next = block;
                block->prev = tail;
        } else {
                heap_head = block;
        }

        heap_current += expand;
        return (uint8_t*)split_block(block, size) + sizeof(heap_block_t);
}

void kfree(void* ptr) {
        if (!ptr) {
                return;
        }

        heap_block_t* block = (heap_block_t*)((uint8_t*)ptr - sizeof(heap_block_t));
        block->free = true;
        coalesce(block);
}
