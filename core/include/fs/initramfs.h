#ifndef INITRAMFS_H
#define INITRAMFS_H

#include "multiboot.h"

/*
 * initramfs_load - unpack the first Multiboot module into the VFS.
 *
 * Returns 0 on success, a negative errno-style code on failure, and
 * leaves the VFS empty when no module is present.
 */
int initramfs_load (struct multiboot_info *info);
void initramfs_list_root (void);

#endif
