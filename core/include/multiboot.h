#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289u

/* Multiboot2 tag types (see GNU Multiboot specification). */
#define MULTIBOOT_TAG_TYPE_END                    0
#define MULTIBOOT_TAG_TYPE_INFORMATION_REQUEST    1
#define MULTIBOOT_TAG_TYPE_MODULE                   3
#define MULTIBOOT_TAG_TYPE_MMAP                     6

/* Information request identifiers. */
#define MULTIBOOT_INFO_REQUEST_MEMORY_MAP         6

/* Memory map entry types. */
#define MULTIBOOT_MEMORY_AVAILABLE                1

struct multiboot_info {
        uint32_t total_size;
        uint32_t reserved;
};

struct multiboot_tag {
        uint32_t type;
        uint32_t size;
};

struct multiboot_tag_mmap {
        uint32_t type;
        uint32_t size;
        uint32_t entry_size;
        uint32_t entry_version;
};

struct multiboot_mmap_entry {
        uint64_t base_addr;
        uint64_t length;
        uint32_t type;
        uint32_t reserved;
};

/*
 * Module loaded by the bootloader (initrd, etc.).
 * mod_start and mod_end are physical addresses supplied by GRUB.
 */
struct multiboot_tag_module {
        uint32_t type;
        uint32_t size;
        uint32_t mod_start;
        uint32_t mod_end;
        char cmdline[];
};

static inline struct multiboot_tag *
multiboot_first_tag (struct multiboot_info *info)
{
        return (struct multiboot_tag *) ((uint8_t *) info + 8);
}

static inline struct multiboot_tag *
multiboot_next_tag (struct multiboot_tag *tag)
{
        return (struct multiboot_tag *) ((uint8_t *) tag + ((tag->size + 7U) & ~7U));
}

struct multiboot_tag *multiboot_find_tag (struct multiboot_info *info, uint32_t type);
struct multiboot_tag_mmap *multiboot_find_mmap (struct multiboot_info *info);
unsigned multiboot_module_count (struct multiboot_info *info);
struct multiboot_tag_module *multiboot_module_at (struct multiboot_info *info,
                                                                                                   unsigned index);
size_t multiboot_module_size (const struct multiboot_tag_module *module);
void multiboot_print_info (struct multiboot_info *info);

#endif
