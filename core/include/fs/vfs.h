#ifndef VFS_H
#define VFS_H

#include "types.h"

/* Linux-compatible file type bits (subset). */
#define S_IFMT   0170000U
#define S_IFDIR  0040000U
#define S_IFREG  0100000U
#define S_IFLNK  0120000U

#define VFS_ROOT "/"

enum vfs_type {
        VFS_TYPE_DIR,
        VFS_TYPE_FILE,
        VFS_TYPE_SYMLINK,
};

struct vfs_stat {
        enum vfs_type type;
        uint32_t mode;
        size_t size;
};

struct vfs_file;
struct pipe;

struct vfs_file {
        struct vfs_inode *inode;
        size_t pos;
        char path[256];
        struct pipe *pipe;
        int pipe_end;
};

struct vfs_inode;
struct vfs_dirent {
        const char *name;
        enum vfs_type type;
};

void vfs_init (void);
struct vfs_inode *vfs_root_inode (void);

int vfs_mkdir (const char *path, uint32_t mode);
int vfs_create (const char *path, uint32_t mode, const void *data, size_t size);
int vfs_symlink (const char *target, const char *path);

int vfs_stat (const char *path, struct vfs_stat *st);
int vfs_fstat (struct vfs_file *file, struct vfs_stat *st);
int vfs_readlink (const char *path, char *buf, size_t bufsz);
int vfs_open (const char *path, struct vfs_file *file);
ssize_t vfs_read (struct vfs_file *file, void *buf, size_t count);
void vfs_close (struct vfs_file *file);

int vfs_readdir (const char *path, struct vfs_dirent *entries, size_t max_entries);
ssize_t vfs_read_at (const char *path, size_t offset, void *buf, size_t count);

#endif
