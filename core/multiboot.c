/*
 * multiboot.c - Multiboot2 information parsing.
 *
 * GRUB passes a tagged list of boot information to the kernel.  These
 * helpers walk that list and expose memory map and module tags.
 */

#include "include/multiboot.h"
#include "include/vga.h"

static bool
multiboot_info_valid (struct multiboot_info *info)
{
        return info != NULL && info->total_size >= 16;
}

static struct multiboot_tag *
multiboot_tag_end (struct multiboot_info *info)
{
        return (struct multiboot_tag *) ((uint8_t *) info + info->total_size);
}

/*
 * multiboot_find_tag - return the first tag matching TYPE.
 */
struct multiboot_tag *
multiboot_find_tag (struct multiboot_info *info, uint32_t type)
{
        struct multiboot_tag *tag;
        struct multiboot_tag *end;

        if (!multiboot_info_valid (info)) {
                return NULL;
        }

        tag = multiboot_first_tag (info);
        end = multiboot_tag_end (info);

        while (tag < end && tag->type != MULTIBOOT_TAG_TYPE_END) {
                if (tag->type == type) {
                        return tag;
                }
                tag = multiboot_next_tag (tag);
        }

        return NULL;
}

/*
 * multiboot_find_mmap - return the memory map tag, if present.
 */
struct multiboot_tag_mmap *
multiboot_find_mmap (struct multiboot_info *info)
{
        return (struct multiboot_tag_mmap *) multiboot_find_tag (info,
                                                                                                                         MULTIBOOT_TAG_TYPE_MMAP);
}

/*
 * multiboot_module_count - number of modules loaded by the bootloader.
 */
unsigned
multiboot_module_count (struct multiboot_info *info)
{
        struct multiboot_tag *tag;
        struct multiboot_tag *end;
        unsigned count = 0;

        if (!multiboot_info_valid (info)) {
                return 0;
        }

        tag = multiboot_first_tag (info);
        end = multiboot_tag_end (info);

        while (tag < end && tag->type != MULTIBOOT_TAG_TYPE_END) {
                if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
                        count++;
                }
                tag = multiboot_next_tag (tag);
        }

        return count;
}

/*
 * multiboot_module_at - return module INDEX (0-based), or NULL.
 */
struct multiboot_tag_module *
multiboot_module_at (struct multiboot_info *info, unsigned index)
{
        struct multiboot_tag *tag;
        struct multiboot_tag *end;
        unsigned seen = 0;

        if (!multiboot_info_valid (info)) {
                return NULL;
        }

        tag = multiboot_first_tag (info);
        end = multiboot_tag_end (info);

        while (tag < end && tag->type != MULTIBOOT_TAG_TYPE_END) {
                if (tag->type == MULTIBOOT_TAG_TYPE_MODULE) {
                        if (seen == index) {
                                return (struct multiboot_tag_module *) tag;
                        }
                        seen++;
                }
                tag = multiboot_next_tag (tag);
        }

        return NULL;
}

/*
 * multiboot_module_size - byte size of a loaded module.
 */
size_t
multiboot_module_size (const struct multiboot_tag_module *module)
{
        if (module == NULL || module->mod_end <= module->mod_start) {
                return 0;
        }

        return (size_t) (module->mod_end - module->mod_start);
}

/*
 * multiboot_print_info - print a short summary of boot tags.
 */
void
multiboot_print_info (struct multiboot_info *info)
{
        struct multiboot_tag_mmap *mmap;
        unsigned modules;
        unsigned i;

        if (!multiboot_info_valid (info)) {
                kprintf ("Multiboot info: unavailable\n");
                return;
        }

        kprintf ("Multiboot info size: %u bytes\n", info->total_size);

        mmap = multiboot_find_mmap (info);
        if (mmap) {
                kprintf ("Memory map: entry_size=%u\n", mmap->entry_size);
        } else {
                kprintf ("Memory map: not provided\n");
        }

        modules = multiboot_module_count (info);
        kprintf ("Modules: %u\n", modules);

        for (i = 0; i < modules; i++) {
                struct multiboot_tag_module *mod = multiboot_module_at (info, i);

                if (mod == NULL) {
                        continue;
                }

                kprintf ("  [%u] phys %p-%p (%u bytes)",
                                 i,
                                 (void *) (uintptr_t) mod->mod_start,
                                 (void *) (uintptr_t) mod->mod_end,
                                 (unsigned) multiboot_module_size (mod));

                if (mod->cmdline[0] != '\0') {
                        kprintf (" cmdline=\"%s\"", mod->cmdline);
                }

                kprintf ("\n");
        }
}
