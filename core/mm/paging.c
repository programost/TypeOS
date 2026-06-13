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

uint64_t
paging_current_cr3 (void)
{
        uint64_t cr3;

        __asm__ volatile ("mov %%cr3, %0" : "=r" (cr3) : : "memory");
        return cr3;
}

void
paging_switch_cr3 (uint64_t cr3)
{
        pml4 = (uint64_t *) (uintptr_t) (cr3 & PTE_ADDR_MASK);
        __asm__ volatile ("mov %0, %%cr3" :: "r" (cr3) : "memory");
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

static void
tlb_flush_range (uint64_t virt, uint64_t size)
{
        uint64_t addr = virt & PAGE_MASK;
        uint64_t end = (virt + size + PAGE_SIZE - 1ULL) & PAGE_MASK;

        while (addr < end) {
                tlb_flush (addr);
                addr += PAGE_SIZE;
        }
}

static uint64_t*
table_entry_virt (uint64_t entry)
{
        return (uint64_t *) (uintptr_t) (entry & PTE_ADDR_MASK);
}

/*
 * Split one 2 MiB huge-page PD entry into a 4 KiB page table that preserves
 * the same identity mapping. Required before user ELF segments can be mapped
 * inside the boot huge-page region (e.g. 0x400000).
 */
static int
split_huge_page (uint64_t *pd, size_t pd_index)
{
        uint64_t huge = pd[pd_index];
        uint64_t huge_phys;
        uint64_t pte_flags;
        uint64_t *pt;
        size_t i;

        if (!(huge & PAGE_PRESENT) || !(huge & PAGE_HUGE)) {
                return 0;
        }

        huge_phys = huge & PTE_ADDR_MASK;
        pte_flags = (huge & ~PTE_ADDR_MASK) & ~PAGE_HUGE;

        pt = paging_alloc_table ();
        if (pt == NULL) {
                return -1;
        }

        for (i = 0; i < 512; i++) {
                pt[i] = huge_phys + (uint64_t) i * PAGE_SIZE + pte_flags;
        }

        pd[pd_index] = ((uint64_t) (uintptr_t) pt) | PAGE_PRESENT | PAGE_WRITABLE;
        tlb_flush_range (pd_index * (2ULL * 1024ULL * 1024ULL), 2ULL * 1024ULL * 1024ULL);
        return 0;
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

static void
paging_extend_identity_map (void)
{
        uint64_t addr;

        for (addr = IDENTITY_4K_START; addr < IDENTITY_4K_END; addr += PAGE_SIZE) {
                if (paging_map_page (addr, addr, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                        return;
                }
        }

        paging_flush_tlb ();
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
        reserve_frames (USER_IMAGE_PHYS_START, USER_IMAGE_PHYS_END);

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

        paging_extend_identity_map ();
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

static int
paging_map_page_flags (uint64_t virt, uint64_t phys, uint64_t flags, uint64_t table_flags)
{
        if ((virt & (PAGE_SIZE - 1ULL)) || (phys & (PAGE_SIZE - 1ULL))) {
                return -1;
        }

        uint64_t* pdpt = get_or_create_table(pml4, PML4_INDEX(virt), table_flags);
        if (!pdpt) {
                return -1;
        }

        uint64_t* pd = get_or_create_table(pdpt, PDP_INDEX(virt), table_flags);
        if (!pd) {
                return -1;
        }

        {
                size_t pd_idx = (size_t) PD_INDEX (virt);

                if ((pd[pd_idx] & PAGE_PRESENT) && (pd[pd_idx] & PAGE_HUGE)) {
                        if (split_huge_page (pd, pd_idx) != 0) {
                                return -1;
                        }
                }
        }

        uint64_t* pt = get_or_create_table(pd, PD_INDEX(virt), table_flags);
        if (!pt) {
                return -1;
        }

        pt[PT_INDEX(virt)] = (phys & PTE_ADDR_MASK) | flags;
        tlb_flush(virt);
        return 0;
}

int paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
        return paging_map_page_flags (virt, phys, flags,
                                      PAGE_PRESENT | PAGE_WRITABLE);
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

uint64_t
paging_pte_value (uint64_t virt)
{
        if (!(pml4[PML4_INDEX (virt)] & PAGE_PRESENT)) {
                return 0;
        }

        {
                uint64_t *pdpt = table_entry_virt (pml4[PML4_INDEX (virt)]);

                if (!(pdpt[PDP_INDEX (virt)] & PAGE_PRESENT)) {
                        return 0;
                }

                {
                        uint64_t *pd = table_entry_virt (pdpt[PDP_INDEX (virt)]);
                        uint64_t pd_entry = pd[PD_INDEX (virt)];

                        if (!(pd_entry & PAGE_PRESENT) || (pd_entry & PAGE_HUGE)) {
                                return 0;
                        }

                        {
                                uint64_t *pt = table_entry_virt (pd_entry);

                                return pt[PT_INDEX (virt)];
                        }
                }
        }
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

void
paging_split_huge_range (uint64_t virt_start, uint64_t virt_end)
{
        size_t pd_first = (size_t) ((virt_start & PAGE_MASK) >> 21);
        size_t pd_last = (size_t) (((virt_end - 1ULL) & PAGE_MASK) >> 21);
        size_t i;

        if (virt_end <= virt_start) {
                return;
        }

        for (i = pd_first; i <= pd_last; i++) {
                if ((pd0[i] & PAGE_PRESENT) && (pd0[i] & PAGE_HUGE)) {
                        if (split_huge_page (pd0, i) != 0) {
                                return;
                        }
                }
        }

        paging_flush_tlb ();
}

static uint64_t *
paging_get_pt (uint64_t virt)
{
        uint64_t *pdpt;
        uint64_t *pd;
        uint64_t pd_entry;

        if (!(pml4[PML4_INDEX (virt)] & PAGE_PRESENT)) {
                return NULL;
        }

        pdpt = table_entry_virt (pml4[PML4_INDEX (virt)]);

        if (!(pdpt[PDP_INDEX (virt)] & PAGE_PRESENT)) {
                return NULL;
        }

        pd = table_entry_virt (pdpt[PDP_INDEX (virt)]);
        pd_entry = pd[PD_INDEX (virt)];

        if (!(pd_entry & PAGE_PRESENT) || (pd_entry & PAGE_HUGE)) {
                return NULL;
        }

        return table_entry_virt (pd_entry);
}

static int
paging_copy_frame (uint64_t dst_phys, uint64_t src_phys)
{
        if (src_phys < IDENTITY_MAP_LIMIT && dst_phys < IDENTITY_MAP_LIMIT) {
                memcpy ((void *) (uintptr_t) dst_phys, (const void *) (uintptr_t) src_phys,
                        PAGE_SIZE);
                return 0;
        }

        if (paging_map_page (KERNEL_KMAP_WINDOW, src_phys, PAGE_PRESENT) != 0) {
                return -1;
        }

        if (paging_map_page (KERNEL_KMAP_WINDOW + PAGE_SIZE, dst_phys,
                             PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                paging_unmap_page (KERNEL_KMAP_WINDOW);
                return -1;
        }

        memcpy ((void *) (uintptr_t) (KERNEL_KMAP_WINDOW + PAGE_SIZE),
                (const void *) (uintptr_t) KERNEL_KMAP_WINDOW, PAGE_SIZE);
        paging_unmap_page (KERNEL_KMAP_WINDOW);
        paging_unmap_page (KERNEL_KMAP_WINDOW + PAGE_SIZE);
        return 0;
}

static int
paging_clone_user_pt (uint64_t *dst_pd, uint64_t *src_pt, size_t pd_idx,
                      uint64_t virt_base)
{
        uint64_t *dst_pt = paging_alloc_table ();
        size_t i;

        if (dst_pt == NULL) {
                return -1;
        }

        dst_pd[pd_idx] = ((uint64_t) (uintptr_t) dst_pt) | PAGE_PRESENT
                | PAGE_WRITABLE | PAGE_USER;

        for (i = 0; i < 512; i++) {
                uint64_t src_entry = src_pt[i];
                void *frame;
                uint64_t dst_phys;
                uint64_t virt = virt_base + (uint64_t) i * PAGE_SIZE;

                if (!(src_entry & PAGE_PRESENT)) {
                        continue;
                }

                if (!(src_entry & PAGE_USER)) {
                        dst_pt[i] = src_entry;
                        continue;
                }

                frame = paging_alloc_page ();
                if (frame == NULL) {
                        return -1;
                }

                dst_phys = (uint64_t) (uintptr_t) frame;
                if (paging_copy_frame (dst_phys, src_entry & PTE_ADDR_MASK) != 0) {
                        paging_free_page (frame);
                        return -1;
                }

                dst_pt[i] = dst_phys | (src_entry & ~PTE_ADDR_MASK);
                tlb_flush (virt);
        }

        return 0;
}

static int
paging_clone_user_pd (uint64_t *dst_pdpt, uint64_t *src_pd, size_t pdpt_idx,
                      uint64_t virt_base)
{
        uint64_t *dst_pd = paging_alloc_table ();
        size_t i;

        if (dst_pd == NULL) {
                return -1;
        }

        dst_pdpt[pdpt_idx] = ((uint64_t) (uintptr_t) dst_pd) | PAGE_PRESENT
                | PAGE_WRITABLE | PAGE_USER;

        for (i = 0; i < 512; i++) {
                uint64_t entry = src_pd[i];

                if (!(entry & PAGE_PRESENT)) {
                        continue;
                }

                if (!(entry & PAGE_USER)) {
                        dst_pd[i] = entry;
                        continue;
                }

                if (entry & PAGE_HUGE) {
                        dst_pd[i] = entry;
                        continue;
                }

                if (paging_clone_user_pt (dst_pd, table_entry_virt (entry), i,
                                          virt_base + (uint64_t) i * 0x200000ULL) != 0) {
                        return -1;
                }
        }

        return 0;
}

static int
paging_clone_user_pdpt (uint64_t *dst_pml4, uint64_t *src_pdpt, size_t pml4_idx,
                        uint64_t virt_base)
{
        uint64_t *dst_pdpt = paging_alloc_table ();
        size_t i;

        if (dst_pdpt == NULL) {
                return -1;
        }

        dst_pml4[pml4_idx] = ((uint64_t) (uintptr_t) dst_pdpt) | PAGE_PRESENT
                | PAGE_WRITABLE | PAGE_USER;

        for (i = 0; i < 512; i++) {
                uint64_t entry = src_pdpt[i];

                if (!(entry & PAGE_PRESENT)) {
                        continue;
                }

                if (!(entry & PAGE_USER)) {
                        dst_pdpt[i] = entry;
                        continue;
                }

                if (paging_clone_user_pd (dst_pdpt, table_entry_virt (entry), i,
                                          virt_base + (uint64_t) i * 0x40000000ULL) != 0) {
                        return -1;
                }
        }

        return 0;
}

uint64_t
paging_clone_current_address_space (int copy_user)
{
        uint64_t *src_pml4 = pml4;
        uint64_t *dst_pml4 = paging_alloc_table ();
        size_t i;

        if (dst_pml4 == NULL) {
                return 0;
        }

        for (i = 0; i < 512; i++) {
                uint64_t entry = src_pml4[i];

                if (!(entry & PAGE_PRESENT)) {
                        continue;
                }

                if (!copy_user || !(entry & PAGE_USER)) {
                        dst_pml4[i] = entry;
                        continue;
                }

                if (paging_clone_user_pdpt (dst_pml4, table_entry_virt (entry), i,
                                            (uint64_t) i << 39) != 0) {
                        return 0;
                }
        }

        return (uint64_t) (uintptr_t) dst_pml4;
}

static void
paging_set_user_path (uint64_t virt)
{
        uint64_t *pdpt;
        uint64_t *pd;

        pml4[PML4_INDEX (virt)] |= PAGE_USER;

        if (!(pml4[PML4_INDEX (virt)] & PAGE_PRESENT)) {
                return;
        }

        pdpt = table_entry_virt (pml4[PML4_INDEX (virt)]);
        pdpt[PDP_INDEX (virt)] |= PAGE_USER;

        if (!(pdpt[PDP_INDEX (virt)] & PAGE_PRESENT)) {
                return;
        }

        pd = table_entry_virt (pdpt[PDP_INDEX (virt)]);
        pd[PD_INDEX (virt)] |= PAGE_USER;
}

int
paging_promote_user_identity (uint64_t virt, uint64_t flags)
{
        uint64_t *pt;
        uint64_t page = virt & PAGE_MASK;

        if (page < USER_IMAGE_PHYS_START || page >= USER_IMAGE_PHYS_END) {
                return -1;
        }

        pt = paging_get_pt (virt);
        if (pt == NULL) {
                return -1;
        }

        paging_set_user_path (virt);
        pt[PT_INDEX (virt)] = page | flags | PAGE_USER | PAGE_WRITABLE;
        tlb_flush (virt);
        return 0;
}

void
paging_split_identity_huge_pages (void)
{
        paging_split_huge_range (0, IDENTITY_MAP_LIMIT);
}

void
paging_flush_tlb (void)
{
        uint64_t cr3;

        __asm__ volatile ("mov %%cr3, %0" : "=r" (cr3) : : "memory");
        __asm__ volatile ("mov %0, %%cr3" :: "r" (cr3) : "memory");
}

int
paging_map_user_page (uint64_t virt, uint64_t phys, uint64_t flags)
{
        return paging_map_page_flags (virt, phys, flags | PAGE_USER,
                                      PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
}

static void
paging_zero_frame (void *frame)
{
        uint64_t phys = (uint64_t) (uintptr_t) frame;

        if (paging_map_page (KERNEL_KMAP_WINDOW, phys, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                return;
        }

        memset8 ((void *) (uintptr_t) KERNEL_KMAP_WINDOW, 0, PAGE_SIZE);
        paging_unmap_page (KERNEL_KMAP_WINDOW);
}

int
paging_map_user_range (uint64_t virt, size_t size, uint64_t flags)
{
        uint64_t addr = virt & PAGE_MASK;
        uint64_t end = (virt + size + PAGE_SIZE - 1ULL) & PAGE_MASK;

        while (addr < end) {
                void *frame = paging_alloc_page ();

                if (frame == NULL) {
                        return -1;
                }

                paging_zero_frame (frame);

                if (paging_map_user_page (addr, (uint64_t) (uintptr_t) frame, flags) != 0) {
                        paging_free_page (frame);
                        return -1;
                }

                addr += PAGE_SIZE;
        }

        return 0;
}

int
paging_clear_phys (uint64_t phys, size_t len)
{
        while (len > 0) {
                uint64_t page = phys & PAGE_MASK;
                size_t offset = (size_t) (phys & (PAGE_SIZE - 1ULL));
                size_t chunk = (size_t) (PAGE_SIZE - offset);

                if (chunk > len) {
                        chunk = len;
                }

                if (paging_map_page (KERNEL_KMAP_WINDOW, page,
                                     PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                        return -1;
                }

                memset8 ((void *) (uintptr_t) (KERNEL_KMAP_WINDOW + offset), 0, chunk);
                paging_unmap_page (KERNEL_KMAP_WINDOW);

                phys += chunk;
                len -= chunk;
        }

        return 0;
}

int
paging_write_phys (uint64_t phys, const void *src, size_t len)
{
        const uint8_t *in = (const uint8_t *) src;

        while (len > 0) {
                uint64_t page = phys & PAGE_MASK;
                size_t offset = (size_t) (phys & (PAGE_SIZE - 1ULL));
                size_t chunk = (size_t) (PAGE_SIZE - offset);

                if (chunk > len) {
                        chunk = len;
                }

                if (paging_map_page (KERNEL_KMAP_WINDOW, page,
                                     PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                        return -1;
                }

                memcpy ((void *) (uintptr_t) (KERNEL_KMAP_WINDOW + offset), in, chunk);
                paging_unmap_page (KERNEL_KMAP_WINDOW);

                in += chunk;
                phys += chunk;
                len -= chunk;
        }

        return 0;
}
