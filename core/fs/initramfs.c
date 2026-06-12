/*
 * initramfs.c - early root filesystem loader.
 *
 * Linux loads an embedded cpio image as initramfs; we do the same using the
 * first Multiboot module supplied by GRUB.
 */

#include "include/fs/initramfs.h"
#include "include/fs/cpio.h"
#include "include/fs/vfs.h"
#include "include/heap.h"
#include "include/multiboot.h"
#include "include/paging.h"
#include "include/vga.h"

void
initramfs_list_root (void)
{
        struct vfs_dirent entries[32];
        int count = vfs_readdir (VFS_ROOT, entries, 32);
        int i;

        if (count < 0) {
                kprintf ("Initramfs: cannot read root directory\n");
                return;
        }

        kprintf ("Root directory (%d entries):\n", count);

        for (i = 0; i < count; i++) {
                const char *kind = "file";

                if (entries[i].type == VFS_TYPE_DIR) {
                        kind = "dir";
                } else if (entries[i].type == VFS_TYPE_SYMLINK) {
                        kind = "link";
                }

                kprintf ("  %-16s %s\n", entries[i].name, kind);
        }
}

int
initramfs_load (struct multiboot_info *info)
{
        struct multiboot_tag_module *module;
        size_t size;
        int rc;

        vfs_init ();

        module = multiboot_module_at (info, 0);
        if (module == NULL) {
                kprintf ("Initramfs: no module, empty rootfs\n");
                return 0;
        }

        size = multiboot_module_size (module);
        if (size == 0) {
                kprintf ("Initramfs: module is empty\n");
                return -1;
        }

        kprintf ("Initramfs: loading module at phys %p (%u bytes)\n",
                 (void *) (uintptr_t) module->mod_start, (unsigned) size);

        rc = cpio_populate_vfs_phys (module->mod_start, size);

        if (rc != 0) {
                kprintf ("Initramfs: cpio unpack failed (%d)\n", rc);
                return rc;
        }

        kprintf ("Initramfs: unpacked %u bytes\n", (unsigned) size);
        initramfs_list_root ();
        return 0;
}
