/*
 * cpio.c - "newc" cpio unpacker (Linux initramfs format).
 *
 * Archive layout:
 *   repeated [ header | path | file data ]
 *   terminated by a TRAILER!!! entry.
 */

#include "include/fs/cpio.h"
#include "include/fs/vfs.h"
#include "include/heap.h"
#include "include/paging.h"
#include "include/string.h"

#define CPIO_NEWC_MAGIC "070701"
#define CPIO_TRAILER  "TRAILER!!!"
#define CPIO_HEADER_SIZE 110U

struct cpio_newc_header {
        char c_magic[6];
        char c_ino[8];
        char c_mode[8];
        char c_uid[8];
        char c_gid[8];
        char c_nlink[8];
        char c_mtime[8];
        char c_filesize[8];
        char c_devmajor[8];
        char c_devminor[8];
        char c_rdevmajor[8];
        char c_rdevminor[8];
        char c_namesize[8];
        char c_check[8];
};

static uint32_t
cpio_parse_hex (const char *text, size_t len)
{
        uint32_t value = 0;

        for (size_t i = 0; i < len; i++) {
                char c = text[i];
                uint32_t digit;

                if (c >= '0' && c <= '9') {
                        digit = (uint32_t) (c - '0');
                } else if (c >= 'a' && c <= 'f') {
                        digit = (uint32_t) (c - 'a' + 10);
                } else if (c >= 'A' && c <= 'F') {
                        digit = (uint32_t) (c - 'A' + 10);
                } else {
                        digit = 0;
                }

                value = (value << 4) | digit;
        }

        return value;
}

static size_t
cpio_align4 (size_t value)
{
        return (value + 3U) & ~3U;
}

/* Align OFFSET to the next 4-byte boundary (cpio newc layout). */
static size_t
cpio_align_offset (size_t offset)
{
        return cpio_align4 (offset);
}

static char *
cpio_normalize_path (const char *name)
{
        char *path;
        size_t len = strlen (name);
        size_t start = 0;

        while (start < len && name[start] == '.') {
                start++;
        }
        while (start < len && name[start] == '/') {
                start++;
        }

        if (start >= len) {
                return NULL;
        }

        len -= start;
        path = (char *) kmalloc (len + 2);
        if (path == NULL) {
                return NULL;
        }

        path[0] = '/';
        memcpy (path + 1, name + start, len);
        path[len + 1] = '\0';
        return path;
}

static int
cpio_install_entry_phys (const char *path, uint32_t mode, uint64_t data_phys, size_t size)
{
        if ((mode & S_IFMT) == S_IFDIR) {
                return vfs_mkdir (path, mode);
        }

        if ((mode & S_IFMT) == S_IFLNK) {
                char *target;
                int rc;

                if (size == 0) {
                        return -1;
                }

                target = (char *) kmalloc (size + 1);
                if (target == NULL) {
                        return -1;
                }

                if (paging_copy_from_phys (target, data_phys, size) != 0) {
                        kfree (target);
                        return -1;
                }

                target[size] = '\0';
                rc = vfs_symlink (target, path);
                kfree (target);
                return rc;
        }

        if ((mode & S_IFMT) == S_IFREG || (mode & S_IFMT) == 0) {
                void *data = NULL;
                int rc;

                if ((mode & S_IFMT) == 0) {
                        mode |= S_IFREG;
                }

                if (size > 0) {
                        data = kmalloc (size);
                        if (data == NULL) {
                                return -1;
                        }

                        if (paging_copy_from_phys (data, data_phys, size) != 0) {
                                kfree (data);
                                return -1;
                        }
                }

                rc = vfs_create (path, mode, data, size);
                kfree (data);
                return rc;
        }

        return 0;
}

static int
cpio_install_entry (const char *path, uint32_t mode, const void *data, size_t size)
{
        if ((mode & S_IFMT) == S_IFDIR) {
                return vfs_mkdir (path, mode);
        }

        if ((mode & S_IFMT) == S_IFLNK) {
                char *target;
                int rc;

                if (data == NULL || size == 0) {
                        return -1;
                }

                target = (char *) kmalloc (size + 1);
                if (target == NULL) {
                        return -1;
                }

                memcpy (target, data, size);
                target[size] = '\0';
                rc = vfs_symlink (target, path);
                kfree (target);
                return rc;
        }

        if ((mode & S_IFMT) == S_IFREG || (mode & S_IFMT) == 0) {
                if ((mode & S_IFMT) == 0) {
                        mode |= S_IFREG;
                }
                return vfs_create (path, mode, data, size);
        }

        return 0;
}

int
cpio_populate_vfs (const void *data, size_t size)
{
        const uint8_t *cursor = (const uint8_t *) data;
        const uint8_t *end = cursor + size;

        if (data == NULL) {
                return -1;
        }

        while ((size_t) (end - cursor) >= CPIO_HEADER_SIZE) {
                const struct cpio_newc_header *hdr = (const struct cpio_newc_header *) cursor;
                uint32_t namesize;
                uint32_t filesize;
                uint32_t mode;
                const char *name;
                const uint8_t *name_end;
                const uint8_t *file_data;
                char *path;
                int rc;

                if (memcmp (hdr->c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
                        return -1;
                }

                namesize = cpio_parse_hex (hdr->c_namesize, 8);
                filesize = cpio_parse_hex (hdr->c_filesize, 8);
                mode = cpio_parse_hex (hdr->c_mode, 8);

                cursor += CPIO_HEADER_SIZE;
                if ((size_t) (end - cursor) < namesize) {
                        return -1;
                }

                name = (const char *) cursor;
                if (namesize == 0) {
                        return -1;
                }

                if (strncmp (name, CPIO_TRAILER, namesize - 1) == 0) {
                        return 0;
                }

                name_end = cursor + namesize;
                file_data = (const uint8_t *) cpio_align_offset ((size_t) name_end);
                if (file_data > end) {
                        return -1;
                }
                if ((size_t) (end - file_data) < filesize) {
                        return -1;
                }

                path = cpio_normalize_path (name);
                if (path == NULL) {
                        cursor = (const uint8_t *) cpio_align_offset ((size_t) file_data + filesize);
                        continue;
                }

                rc = cpio_install_entry (path, mode,
                                         filesize > 0 ? file_data : NULL,
                                         filesize);
                kfree (path);

                if (rc != 0) {
                        return rc;
                }

                cursor = (const uint8_t *) cpio_align_offset ((size_t) file_data + filesize);
        }

        return -1;
}

int
cpio_populate_vfs_phys (uint64_t base_phys, size_t size)
{
        size_t offset = 0;

        while (offset + CPIO_HEADER_SIZE <= size) {
                struct cpio_newc_header hdr;
                uint32_t namesize;
                uint32_t filesize;
                uint32_t mode;
                char *name;
                char *path;
                int rc;

                if (paging_copy_from_phys (&hdr, base_phys + offset, sizeof (hdr)) != 0) {
                        return -1;
                }

                if (memcmp (hdr.c_magic, CPIO_NEWC_MAGIC, 6) != 0) {
                        return -1;
                }

                namesize = cpio_parse_hex (hdr.c_namesize, 8);
                filesize = cpio_parse_hex (hdr.c_filesize, 8);
                mode = cpio_parse_hex (hdr.c_mode, 8);

                if (namesize == 0 || namesize > 4096) {
                        return -1;
                }

                offset += CPIO_HEADER_SIZE;
                if (offset + namesize > size) {
                        return -1;
                }

                name = (char *) kmalloc (namesize);
                if (name == NULL) {
                        return -1;
                }

                if (paging_copy_from_phys (name, base_phys + offset, namesize) != 0) {
                        kfree (name);
                        return -1;
                }

                if (strncmp (name, CPIO_TRAILER, namesize - 1) == 0) {
                        kfree (name);
                        return 0;
                }

                offset += namesize;
                offset = cpio_align_offset (offset);
                if (offset + filesize > size) {
                        kfree (name);
                        return -1;
                }

                path = cpio_normalize_path (name);
                kfree (name);

                if (path == NULL) {
                        offset = cpio_align_offset (offset + filesize);
                        continue;
                }

                rc = cpio_install_entry_phys (path, mode, base_phys + offset, filesize);
                kfree (path);

                if (rc != 0) {
                        return rc;
                }

                offset = cpio_align_offset (offset + filesize);
        }

        return -1;
}
