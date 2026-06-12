#ifndef CPIO_H
#define CPIO_H

#include "types.h"

/*
 * cpio_populate_vfs - unpack a "newc" cpio archive into the VFS root.
 *
 * DATA/SIZE describe the archive bytes (for example a Multiboot module).
 */
int cpio_populate_vfs (const void *data, size_t size);
int cpio_populate_vfs_phys (uint64_t base_phys, size_t size);

#endif
