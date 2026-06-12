#include "include/paging.h"
#include "include/types.h"
#include "include/string.h"

#define PTE_ADDR_MASK   0x000FFFFFFFFFF000ULL
#define PML4_INDEX(v)   (((v) >> 39) & 0x1FFULL)
#define PDP_INDEX(v)    (((v) >> 30) & 0x1FFULL)
#define PD_INDEX(v)     (((v) >> 21) & 0x1FFULL)
#define PT_INDEX(v)     (((v) >> 12) & 0x1FFULL)

#define MAX_PHYS_MEM        (2048ULL * 1024ULL * 1024ULL)
#define KERNEL_KMAP_WINDOW  0x05000000ULL
#define IDENTITY_MAP_LIMIT  0x10000000ULL
#define MAX_FRAMES      (MAX_PHYS_MEM / PAGE_SIZE)
#define BITMAP_BYTES    (MAX_FRAMES / 8U)

extern char pml4_table[];
extern char pdp_table[];
extern char pd_table[];
extern char kernel_end[];
extern char stack_bottom[];
extern char stack_top[];

static uint8_t frame_bitmap[BITMAP_BYTES];
static size_t total_frames = 0;

static uint64_t* pml4 = (uint64_t*)0;
static uint64_t* pdp0 = (uint64_t*)0;
static uint64_t* pd0 = (uint64_t*)0;

static void memset8(void* dst, uint8_t value, size_t count) {
        uint8_t* p = (uint8_t*)dst;
        for (size_t i = 0; i < count; i++) {
                p[i] = value;
        }
}

static void bitmap_set(size_t frame) {
        if (frame >= MAX_FRAMES) {
                return;
        }
        frame_bitmap[frame / 8U] |= (uint8_t)(1U << (frame % 8U));
}

static void bitmap_clear(size_t frame) {
        if (frame >= MAX_FRAMES) {
                return;
        }
        frame_bitmap[frame / 8U] &= (uint8_t)~(1U << (frame % 8U));
}

static bool bitmap_test(size_t frame) {
        if (frame >= MAX_FRAMES) {
                return true;
        }
        return (frame_bitmap[frame / 8U] >> (frame % 8U)) & 1U;
}

static void reserve_frames(uint64_t start, uint64_t end) {
        uint64_t aligned_start = start & PAGE_MASK;
        uint64_t aligned_end = (end + PAGE_SIZE - 1ULL) & PAGE_MASK;

        for (uint64_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
                size_t frame = (size_t)(addr / PAGE_SIZE);
                if (frame < MAX_FRAMES) {
                        bitmap_set(frame);
                }
        }
}

static void* paging_alloc_page_noreserve(void) {
        for (size_t i = 0; i < total_frames; i++) {
                if (!bitmap_test(i)) {
                        bitmap_set(i);
                        return (void*)(i * PAGE_SIZE);
                }
        }
        return NULL;
}

/*
 * Page table pages must live in the identity-mapped region so we can edit
 * them through virtual addresses before a full phys/virt map exists.
 */
static uint64_t*
paging_alloc_table (void)
{
        uintptr_t kernel_end_addr = (uintptr_t) &kernel_end;
        uintptr_t stack_top_addr = (uintptr_t) &stack_top;

        for (size_t i = 0; i < total_frames; i++) {
                uintptr_t addr = (uintptr_t) (i * PAGE_SIZE);

                if (addr >= IDENTITY_MAP_LIMIT) {
                        break;
                }

                if (addr >= kernel_end_addr && addr < stack_top_addr) {
                        continue;
                }

                if (!bitmap_test (i)) {
                        void *frame;

                        bitmap_set (i);
                        frame = (void *) addr;
                        memset8 (frame, 0, PAGE_SIZE);
                        return (uint64_t *) frame;
                }
        }

        return NULL;
}

static void tlb_flush(uint64_t virt) {
        __asm__ volatile("invlpg (%0)" ::"r"(virt) : "memory");
}

static uint64_t*
table_entry_virt (uint64_t entry)
{
        return (uint64_t *) (uintptr_t) (entry & PTE_ADDR_MASK);
}

static uint64_t*
get_or_create_table (uint64_t *table, size_t index, uint64_t flags)
{
        if (table[index] & PAGE_PRESENT) {
                if (table[index] & PAGE_HUGE) {
                        return NULL;
                }
                return table_entry_virt (table[index]);
        }

        {
                uint64_t *child = paging_alloc_table ();

                if (child == NULL) {
                        return NULL;
                }

                table[index] = ((uint64_t) (uintptr_t) child) | flags;
                return child;
        }
}

void paging_init(struct multiboot_info* mb2_info) {
        pml4 = (uint64_t*)pml4_table;
        pdp0 = (uint64_t*)pdp_table;
        pd0 = (uint64_t*)pd_table;

        memset8(frame_bitmap, 0xFF, BITMAP_BYTES);
        total_frames = 0;

        struct multiboot_tag_mmap* mmap = multiboot_find_mmap (mb2_info);

        if (mmap != NULL && mmap->entry_size >= sizeof (struct multiboot_mmap_entry)) {
                uint8_t* entry = (uint8_t*) mmap + sizeof (struct multiboot_tag_mmap);
                uint8_t* mmap_end = (uint8_t*) mmap + mmap->size;

                while (entry + mmap->entry_size <= mmap_end) {
                        struct multiboot_mmap_entry* e = (struct multiboot_mmap_entry*) entry;

                        if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                                uint64_t region_end = e->base_addr + e->length;

                                if (e->base_addr < MAX_PHYS_MEM) {
                                        if (region_end > MAX_PHYS_MEM) {
                                                region_end = MAX_PHYS_MEM;
                                        }

                                        for (uint64_t addr = e->base_addr; addr < region_end; addr += PAGE_SIZE) {
                                                size_t frame = (size_t) (addr / PAGE_SIZE);

                                                if (frame >= MAX_FRAMES) {
                                                        break;
                                                }
                                                if (frame + 1 > total_frames) {
                                                        total_frames = frame + 1;
                                                }
                                                bitmap_clear (frame);
                                        }
                                }
                        }

                        entry += mmap->entry_size;
                }
        }

        if (total_frames == 0) {
                total_frames = MAX_FRAMES;
                memset8(frame_bitmap, 0, BITMAP_BYTES);
        }

        reserve_frames (0, MAX_PHYS_MEM < (uint64_t) (uintptr_t) &kernel_end
                ? MAX_PHYS_MEM
                : (uint64_t) (uintptr_t) &kernel_end);
        reserve_frames ((uint64_t) (uintptr_t) &stack_bottom,
                                        (uint64_t) (uintptr_t) &stack_top);

        if (mb2_info != NULL) {
                unsigned module_count = multiboot_module_count (mb2_info);
                unsigned i;

                for (i = 0; i < module_count; i++) {
                        struct multiboot_tag_module* mod = multiboot_module_at (mb2_info, i);

                        if (mod != NULL) {
                                reserve_frames (mod->mod_start, mod->mod_end);
                        }
                }
        }
}

void* paging_alloc_page(void) {
        return paging_alloc_page_noreserve();
}

void paging_free_page(void* page) {
        if (!page) {
                return;
        }
        size_t frame = (size_t)((uint64_t)(uintptr_t)page / PAGE_SIZE);
        if (frame < total_frames) {
                bitmap_clear(frame);
        }
}

int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
        if ((virt & (PAGE_SIZE - 1ULL)) || (phys & (PAGE_SIZE - 1ULL))) {
                return -1;
        }

        uint64_t* pdpt = get_or_create_table(pml4, PML4_INDEX(virt), PAGE_PRESENT | PAGE_WRITABLE);
        if (!pdpt) {
                return -1;
        }

        uint64_t* pd = get_or_create_table(pdpt, PDP_INDEX(virt), PAGE_PRESENT | PAGE_WRITABLE);
        if (!pd) {
                return -1;
        }

        if (pd[PD_INDEX(virt)] & PAGE_HUGE) {
                return -1;
        }

        uint64_t* pt = get_or_create_table(pd, PD_INDEX(virt), PAGE_PRESENT | PAGE_WRITABLE);
        if (!pt) {
                return -1;
        }

        pt[PT_INDEX(virt)] = (phys & PTE_ADDR_MASK) | flags;
        tlb_flush(virt);
        return 0;
}

void paging_unmap_page(uint64_t virt) {
        if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) {
                return;
        }

        uint64_t* pdpt = (uint64_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
        if (!(pdpt[PDP_INDEX(virt)] & PAGE_PRESENT)) {
                return;
        }

        uint64_t* pd = (uint64_t*)(pdpt[PDP_INDEX(virt)] & PTE_ADDR_MASK);
        if (!(pd[PD_INDEX(virt)] & PAGE_PRESENT) || (pd[PD_INDEX(virt)] & PAGE_HUGE)) {
                return;
        }

        uint64_t* pt = (uint64_t*)(pd[PD_INDEX(virt)] & PTE_ADDR_MASK);
        pt[PT_INDEX(virt)] = 0;
        tlb_flush(virt);
}

uint64_t paging_virt_to_phys(uint64_t virt) {
        if (!(pml4[PML4_INDEX(virt)] & PAGE_PRESENT)) {
                return 0;
        }

        uint64_t* pdpt = (uint64_t*)(pml4[PML4_INDEX(virt)] & PTE_ADDR_MASK);
        if (!(pdpt[PDP_INDEX(virt)] & PAGE_PRESENT)) {
                return 0;
        }

        uint64_t* pd = (uint64_t*)(pdpt[PDP_INDEX(virt)] & PTE_ADDR_MASK);
        uint64_t pd_entry = pd[PD_INDEX(virt)];
        if (!(pd_entry & PAGE_PRESENT)) {
                return 0;
        }

        if (pd_entry & PAGE_HUGE) {
                return (pd_entry & PTE_ADDR_MASK) + (virt & 0x1FFFFFULL);
        }

        uint64_t* pt = (uint64_t*)(pd_entry & PTE_ADDR_MASK);
        uint64_t pt_entry = pt[PT_INDEX(virt)];
        if (!(pt_entry & PAGE_PRESENT)) {
                return 0;
        }

        return (pt_entry & PTE_ADDR_MASK) + (virt & (PAGE_SIZE - 1ULL));
}

/*
 * paging_copy_from_phys - copy bytes from physical memory into DEST.
 *
 * Uses the identity map below IDENTITY_MAP_LIMIT and a temporary kernel
 * mapping window elsewhere.
 */
int
paging_copy_from_phys (void *dest, uint64_t src_phys, size_t len)
{
        uint8_t *dst = (uint8_t *) dest;

        while (len > 0) {
                uint64_t page = src_phys & PAGE_MASK;
                size_t offset = (size_t) (src_phys & (PAGE_SIZE - 1ULL));
                size_t chunk = (size_t) (PAGE_SIZE - offset);

                if (chunk > len) {
                        chunk = len;
                }

                if (page + offset < IDENTITY_MAP_LIMIT) {
                        memcpy (dst, (void *) (uintptr_t) (page + offset), chunk);
                } else {
                        if (paging_map_page (KERNEL_KMAP_WINDOW, page,
                                                                 PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                                return -1;
                        }

                        memcpy (dst, (void *) (uintptr_t) (KERNEL_KMAP_WINDOW + offset), chunk);
                        paging_unmap_page (KERNEL_KMAP_WINDOW);
                }

                dst += chunk;
                src_phys += chunk;
                len -= chunk;
        }

        return 0;
}
