/*
 * vfs.c - Virtual filesystem (ramfs backend).
 *
 * This mirrors the Linux initramfs model: a single in-memory tree of inodes
 * that is populated from a cpio archive at boot.
 */

#include "include/fs/vfs.h"
#include "include/heap.h"
#include "include/string.h"

struct vfs_dentry {
        char *name;
        struct vfs_inode *inode;
        struct vfs_dentry *next;
};

struct vfs_inode {
        enum vfs_type type;
        uint32_t mode;
        size_t size;
        union {
                struct {
                        uint8_t *data;
                } file;
                struct {
                        char *target;
                } symlink;
                struct {
                        struct vfs_dentry *children;
                } dir;
        };
};

static struct vfs_inode *root_inode;

static char *
vfs_strdup (const char *src)
{
        size_t len = strlen (src);
        char *copy = (char *) kmalloc (len + 1);

        if (copy == NULL) {
                return NULL;
        }

        memcpy (copy, src, len + 1);
        return copy;
}

static int
vfs_is_absolute (const char *path)
{
        return path != NULL && path[0] == '/';
}

static const char *
vfs_skip_slash (const char *path)
{
        while (*path == '/') {
                path++;
        }
        return path;
}

static struct vfs_dentry *
vfs_dir_find (struct vfs_inode *dir, const char *name, size_t len)
{
        struct vfs_dentry *entry;

        if (dir == NULL || dir->type != VFS_TYPE_DIR) {
                return NULL;
        }

        for (entry = dir->dir.children; entry != NULL; entry = entry->next) {
                if (strncmp (entry->name, name, len) == 0 && entry->name[len] == '\0') {
                        return entry;
                }
        }

        return NULL;
}

static int
vfs_dir_add (struct vfs_inode *dir, const char *name, struct vfs_inode *inode)
{
        struct vfs_dentry *entry;

        if (dir == NULL || dir->type != VFS_TYPE_DIR || name == NULL || inode == NULL) {
                return -1;
        }

        if (vfs_dir_find (dir, name, strlen (name)) != NULL) {
                return -1;
        }

        entry = (struct vfs_dentry *) kmalloc (sizeof (*entry));
        if (entry == NULL) {
                return -1;
        }

        entry->name = vfs_strdup (name);
        if (entry->name == NULL) {
                kfree (entry);
                return -1;
        }

        entry->inode = inode;
        entry->next = dir->dir.children;
        dir->dir.children = entry;
        return 0;
}

static struct vfs_inode *
vfs_inode_new (enum vfs_type type, uint32_t mode)
{
        struct vfs_inode *inode = (struct vfs_inode *) kmalloc (sizeof (*inode));

        if (inode == NULL) {
                return NULL;
        }

        memset (inode, 0, sizeof (*inode));
        inode->type = type;
        inode->mode = mode;
        return inode;
}

static struct vfs_inode *
vfs_lookup_parent (const char *path, char *name_out, size_t name_size, bool create)
{
        struct vfs_inode *current = root_inode;
        char component[256];
        const char *cursor = vfs_skip_slash (path);

        if (!vfs_is_absolute (path) || root_inode == NULL) {
                return NULL;
        }

        while (*cursor != '\0') {
                size_t len = 0;

                while (cursor[len] != '\0' && cursor[len] != '/') {
                        if (len + 1 >= sizeof (component)) {
                                return NULL;
                        }
                        component[len] = cursor[len];
                        len++;
                }
                component[len] = '\0';

                cursor += len;
                cursor = vfs_skip_slash (cursor);

                if (*cursor == '\0') {
                        if (name_out != NULL && name_size > 0) {
                                strncpy (name_out, component, name_size - 1);
                                name_out[name_size - 1] = '\0';
                        }
                        return current;
                }

                if (current->type != VFS_TYPE_DIR) {
                        return NULL;
                }

                {
                        struct vfs_dentry *entry = vfs_dir_find (current, component, len);

                        if (entry == NULL) {
                                if (!create) {
                                        return NULL;
                                }

                                struct vfs_inode *next = vfs_inode_new (VFS_TYPE_DIR, S_IFDIR | 0755U);

                                if (next == NULL || vfs_dir_add (current, component, next) != 0) {
                                        return NULL;
                                }

                                current = next;
                        } else {
                                current = entry->inode;
                        }
                }
        }

        if (name_out != NULL && name_size > 0) {
                name_out[0] = '\0';
        }

        return current;
}

static void
vfs_symlink_resolve (const char *base, const char *target, char *out, size_t outlen)
{
        const char *slash;
        size_t dirlen;

        if (target[0] == '/') {
                strncpy (out, target, outlen - 1);
                out[outlen - 1] = '\0';
                return;
        }

        slash = strrchr (base, '/');
        if (slash == NULL) {
                out[0] = '\0';
                return;
        }

        if (slash == base) {
                dirlen = 1;
        } else {
                dirlen = (size_t) (slash - base);
        }

        if (dirlen + 1 + strlen (target) + 1 > outlen) {
                out[0] = '\0';
                return;
        }

        memcpy (out, base, dirlen);
        out[dirlen] = '/';
        strcpy (out + dirlen + 1, target);
}

static struct vfs_inode *
vfs_lookup_path (const char *path, bool follow_symlink)
{
        char curpath[512];
        char linkbuf[512];
        struct vfs_inode *current;
        char component[256];
        const char *cursor;
        unsigned depth = 0;

        if (!vfs_is_absolute (path) || root_inode == NULL) {
                return NULL;
        }

        strncpy (curpath, path, sizeof (curpath) - 1);
        curpath[sizeof (curpath) - 1] = '\0';

restart:
        current = root_inode;
        cursor = vfs_skip_slash (curpath);

        while (*cursor != '\0') {
                        size_t len = 0;

                        while (cursor[len] != '\0' && cursor[len] != '/') {
                                if (len + 1 >= sizeof (component)) {
                                        return NULL;
                                }
                                component[len] = cursor[len];
                                len++;
                        }
                        component[len] = '\0';

                        cursor += len;
                        cursor = vfs_skip_slash (cursor);

                        if (current->type != VFS_TYPE_DIR) {
                                return NULL;
                        }

                        {
                                struct vfs_dentry *entry = vfs_dir_find (current, component, len);

                                if (entry == NULL) {
                                        return NULL;
                                }

                                current = entry->inode;
                        }

                        if (current->type == VFS_TYPE_SYMLINK && follow_symlink) {
                                char rest[256];

                                if (++depth > 8 || current->symlink.target == NULL) {
                                        return NULL;
                                }

                                strncpy (rest, cursor, sizeof (rest) - 1);
                                rest[sizeof (rest) - 1] = '\0';

                                {
                                        char partial[512];
                                        size_t plen = strlen (curpath) - strlen (cursor);

                                        if (plen >= sizeof (partial)) {
                                                return NULL;
                                        }

                                        memcpy (partial, curpath, plen);
                                        partial[plen] = '\0';
                                        vfs_symlink_resolve (partial, current->symlink.target,
                                                             linkbuf, sizeof (linkbuf));
                                }

                                if (rest[0] != '\0') {
                                        size_t l = strlen (linkbuf);

                                        if (l + 1 + strlen (rest) + 1 > sizeof (linkbuf)) {
                                                return NULL;
                                        }

                                        linkbuf[l] = '/';
                                        strcpy (linkbuf + l + 1, rest);
                                }

                                strncpy (curpath, linkbuf, sizeof (curpath) - 1);
                                curpath[sizeof (curpath) - 1] = '\0';
                                depth = 0;
                                goto restart;
                        }
                }

        if (current->type == VFS_TYPE_SYMLINK && follow_symlink) {
                if (current->symlink.target == NULL) {
                        return NULL;
                }

                vfs_symlink_resolve (curpath, current->symlink.target,
                                     linkbuf, sizeof (linkbuf));
                return vfs_lookup_path (linkbuf, true);
        }

        return current;
}

void
vfs_init (void)
{
        root_inode = vfs_inode_new (VFS_TYPE_DIR, S_IFDIR | 0755U);
}

struct vfs_inode *
vfs_root_inode (void)
{
        return root_inode;
}

int
vfs_mkdir (const char *path, uint32_t mode)
{
        char name[256];
        struct vfs_inode *parent = vfs_lookup_parent (path, name, sizeof (name), true);
        struct vfs_inode *inode;

        if (parent == NULL || name[0] == '\0') {
                return -1;
        }

        if (vfs_dir_find (parent, name, strlen (name)) != NULL) {
                return 0;
        }

        inode = vfs_inode_new (VFS_TYPE_DIR, mode);
        if (inode == NULL) {
                return -1;
        }

        return vfs_dir_add (parent, name, inode);
}

int
vfs_create (const char *path, uint32_t mode, const void *data, size_t size)
{
        char name[256];
        struct vfs_inode *parent = vfs_lookup_parent (path, name, sizeof (name), true);
        struct vfs_inode *inode;
        uint8_t *copy;

        if (parent == NULL || name[0] == '\0') {
                return -1;
        }

        if (vfs_dir_find (parent, name, strlen (name)) != NULL) {
                return -1;
        }

        inode = vfs_inode_new (VFS_TYPE_FILE, mode);
        if (inode == NULL) {
                return -1;
        }

        inode->size = size;
        if (size > 0) {
                copy = (uint8_t *) kmalloc (size);
                if (copy == NULL) {
                        kfree (inode);
                        return -1;
                }
                memcpy (copy, data, size);
                inode->file.data = copy;
        }

        if (vfs_dir_add (parent, name, inode) != 0) {
                kfree (inode->file.data);
                kfree (inode);
                return -1;
        }

        return 0;
}

int
vfs_symlink (const char *target, const char *path)
{
        char name[256];
        struct vfs_inode *parent = vfs_lookup_parent (path, name, sizeof (name), true);
        struct vfs_inode *inode;

        if (parent == NULL || name[0] == '\0' || target == NULL) {
                return -1;
        }

        if (vfs_dir_find (parent, name, strlen (name)) != NULL) {
                return -1;
        }

        inode = vfs_inode_new (VFS_TYPE_SYMLINK, S_IFLNK | 0777U);
        if (inode == NULL) {
                return -1;
        }

        inode->symlink.target = vfs_strdup (target);
        if (inode->symlink.target == NULL) {
                kfree (inode);
                return -1;
        }

        inode->size = strlen (target);

        if (vfs_dir_add (parent, name, inode) != 0) {
                kfree (inode->symlink.target);
                kfree (inode);
                return -1;
        }

        return 0;
}

int
vfs_stat (const char *path, struct vfs_stat *st)
{
        struct vfs_inode *inode = vfs_lookup_path (path, true);

        if (inode == NULL || st == NULL) {
                return -1;
        }

        st->type = inode->type;
        st->mode = inode->mode;
        st->size = inode->size;
        return 0;
}

int
vfs_fstat (struct vfs_file *file, struct vfs_stat *st)
{
        if (file == NULL || file->inode == NULL || st == NULL) {
                return -1;
        }

        st->type = file->inode->type;
        st->mode = file->inode->mode;
        st->size = file->inode->size;
        return 0;
}

int
vfs_readlink (const char *path, char *buf, size_t bufsz)
{
        struct vfs_inode *inode = vfs_lookup_path (path, false);
        size_t len;

        if (inode == NULL || buf == NULL || bufsz == 0
            || inode->type != VFS_TYPE_SYMLINK || inode->symlink.target == NULL) {
                return -1;
        }

        len = strlen (inode->symlink.target);
        if (len >= bufsz) {
                len = bufsz - 1;
        }

        memcpy (buf, inode->symlink.target, len);
        buf[len] = '\0';
        return (int) len;
}

int
vfs_open (const char *path, struct vfs_file *file)
{
        struct vfs_inode *inode = vfs_lookup_path (path, true);

        if (inode == NULL || file == NULL) {
                return -1;
        }

        if (inode->type != VFS_TYPE_FILE && inode->type != VFS_TYPE_DIR) {
                return -1;
        }

        strncpy (file->path, path, sizeof (file->path) - 1);
        file->path[sizeof (file->path) - 1] = '\0';
        file->inode = inode;
        file->pos = 0;
        return 0;
}

ssize_t
vfs_read (struct vfs_file *file, void *buf, size_t count)
{
        size_t available;

        if (file == NULL || buf == NULL || file->inode == NULL
                || file->inode->type != VFS_TYPE_FILE) {
                return -1;
        }

        if (file->pos >= file->inode->size) {
                return 0;
        }

        available = file->inode->size - file->pos;
        if (count > available) {
                count = available;
        }

        if (count > 0 && file->inode->file.data != NULL) {
                memcpy (buf, file->inode->file.data + file->pos, count);
                file->pos += count;
        }

        return (ssize_t) count;
}

void
vfs_close (struct vfs_file *file)
{
        if (file == NULL) {
                return;
        }

        file->inode = NULL;
        file->pos = 0;
}

int
vfs_readdir (const char *path, struct vfs_dirent *entries, size_t max_entries)
{
        struct vfs_inode *inode = vfs_lookup_path (path, true);
        struct vfs_dentry *child;
        size_t count = 0;

        if (inode == NULL || inode->type != VFS_TYPE_DIR || entries == NULL) {
                return -1;
        }

        for (child = inode->dir.children; child != NULL && count < max_entries;
                 child = child->next) {
                entries[count].name = child->name;
                entries[count].type = child->inode->type;
                count++;
        }

        return (int) count;
}

ssize_t
vfs_read_at (const char *path, size_t offset, void *buf, size_t count)
{
        struct vfs_inode *inode = vfs_lookup_path (path, true);
        uint8_t *dst = (uint8_t *) buf;
        size_t available;

        if (inode == NULL || inode->type != VFS_TYPE_FILE || buf == NULL) {
                return -1;
        }

        if (offset >= inode->size) {
                return 0;
        }

        available = inode->size - offset;
        if (count > available) {
                count = available;
        }

        if (count > 0 && inode->file.data != NULL) {
                memcpy (dst, inode->file.data + offset, count);
        }

        return (ssize_t) count;
}
