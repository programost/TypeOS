#ifndef PAGING_H
#define PAGING_H

#include "types.h"
#include "multiboot.h"

#define PAGE_SIZE           4096ULL
#define PAGE_MASK           (~(PAGE_SIZE - 1ULL))

#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITABLE       (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_HUGE           (1ULL << 7)
#define PAGE_NO_EXECUTE     (1ULL << 63)

/* Above the 256 MiB boot identity map (128 x 2 MiB huge pages). */
#define KERNEL_HEAP_START   0x10000000ULL
#define KERNEL_HEAP_LIMIT   0x50000000ULL

void paging_init(struct multiboot_info* mb2_info);
void* paging_alloc_page(void);
void paging_free_page(void* page);
int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void paging_unmap_page(uint64_t virt);
uint64_t paging_virt_to_phys(uint64_t virt);
int paging_copy_from_phys (void *dest, uint64_t src_phys, size_t len);

#endif
