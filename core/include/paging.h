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

/* Boot identity map: 128 x 2 MiB huge pages (0 .. 256 MiB). */
#define IDENTITY_MAP_LIMIT  0x10000000ULL

/* Physical RAM reserved for fixed-address ET_EXEC binaries (identity mapped). */
#define USER_IMAGE_PHYS_START   0x00400000ULL
#define USER_IMAGE_PHYS_END     0x00800000ULL

/* Identity 4 KiB map for page tables and fixed ET_EXEC binaries (boot huge pages cover 0-4 MiB). */
#define IDENTITY_4K_START       0x00400000ULL
#define IDENTITY_4K_END         0x04000000ULL

/* Above the 256 MiB boot identity map (128 x 2 MiB huge pages). */
#define KERNEL_HEAP_START   0x10000000ULL
#define KERNEL_HEAP_LIMIT   0x50000000ULL

void paging_init(struct multiboot_info* mb2_info);
void* paging_alloc_page(void);
void paging_free_page(void* page);
int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void paging_unmap_page(uint64_t virt);
uint64_t paging_virt_to_phys(uint64_t virt);
uint64_t paging_pte_value (uint64_t virt);
int paging_copy_from_phys (void *dest, uint64_t src_phys, size_t len);
int paging_map_user_range (uint64_t virt, size_t size, uint64_t flags);
int paging_map_user_page (uint64_t virt, uint64_t phys, uint64_t flags);
void paging_flush_tlb (void);
void paging_split_huge_range (uint64_t virt_start, uint64_t virt_end);
int paging_promote_user_identity (uint64_t virt, uint64_t flags);
uint64_t paging_current_cr3 (void);
void paging_switch_cr3 (uint64_t cr3);
uint64_t paging_clone_current_address_space (int copy_user);
int paging_write_phys (uint64_t phys, const void *src, size_t len);
int paging_clear_phys (uint64_t phys, size_t len);

#endif
