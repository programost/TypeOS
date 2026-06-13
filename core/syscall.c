/*
 * syscall.c - Linux x86_64 syscall subset for static initramfs binaries.
 */

#include "include/syscall.h"
#include "include/proc.h"
#include "include/heap.h"
#include "include/fs/vfs.h"
#include "include/paging.h"
#include "include/sched.h"
#include "include/keyboard.h"
#include "include/vga.h"
#include "include/string.h"

#define SYS_readv           19
#define SYS_writev          20
#define SYS_read            0
#define SYS_write           1
#define SYS_open            2
#define SYS_close           3
#define SYS_stat            4
#define SYS_fstat           5
#define SYS_poll            7
#define SYS_lseek           8
#define SYS_mmap            9
#define SYS_mprotect        10
#define SYS_munmap          11
#define SYS_brk             12
#define SYS_rt_sigaction    13
#define SYS_rt_sigprocmask  14
#define SYS_ioctl           16
#define SYS_pipe            22
#define SYS_access          21
#define SYS_dup             32
#define SYS_dup2            33
#define SYS_getpid          39
#define SYS_socket          41
#define SYS_execve          59
#define SYS_uname           63
#define SYS_fcntl           72
#define SYS_getcwd          79
#define SYS_chdir           80
#define SYS_dup3            292
#define SYS_readlink        89
#define SYS_unlink          87
#define SYS_getuid          102
#define SYS_getgid          104
#define SYS_geteuid         107
#define SYS_getegid         108
#define SYS_getppid         110
#define SYS_getpgrp         111
#define SYS_setsid          112
#define SYS_getpgid         121
#define SYS_arch_prctl      158
#define SYS_mount           165
#define SYS_gettid          186
#define SYS_getdents64      217
#define SYS_set_tid_address 218
#define SYS_exit            60
#define SYS_wait4           61
#define SYS_kill            62
#define SYS_futex           202
#define SYS_set_robust_list 273
#define SYS_exit_group      231
#define SYS_madvise         28
#define SYS_getitimer       105
#define SYS_setitimer       106
#define SYS_reboot          169
#define SYS_clock_gettime   228
#define SYS_clock_nanosleep 230
#define SYS_openat          257
#define SYS_newfstatat      262
#define SYS_readlinkat      267
#define SYS_getrandom       318
#define SYS_prlimit64       302
#define SYS_openat2         437
#define SYS_statx           332
#define SYS_pivot_root      155
#define SYS_pipe2           293
#define SYS_sched_getparam  125
#define SYS_sched_setscheduler 128
#define SYS_rseq            334
#define SYS_mbind           234
#define SYS_clone           56
#define SYS_fork            57
#define SYS_vfork           58
#define SYS_setpgid         109

#define ARCH_SET_FS         0x1002U
#define ARCH_GET_FS         0x1003U
#define MSR_FS_BASE         0xC0000100U

#define CLONE_VM            0x00000100U
#define CLONE_SETTLS        0x00080000U

#define AT_FDCWD            (-100)

#define E_BADF              9
#define E_CHLD              10
#define E_FAULT             14
#define E_INVAL             22
#define E_NOSYS             38
#define E_NOENT             2
#define E_NOMEM             12
#define E_ACCES             13
#define E_NOTDIR            20
#define E_ISDIR             21
#define EEXIST              17
#define EPIPE               32

#define ERR(n)              (-(n))

#define O_RDONLY            0U
#define O_DIRECTORY         0200000U

#define F_DUPFD             0
#define F_GETFD             1
#define F_SETFD             2
#define F_GETFL             3
#define F_DUPFD_CLOEXEC     1030

#define TCGETS              0x5401U
#define TIOCGPGRP           0x540FU
#define TIOCSPGRP           0x5410U
#define TIOCGWINSZ          0x5413U

#define POLLIN              0x001

#define DT_UNKNOWN          0
#define DT_DIR              4
#define DT_REG              8
#define DT_LNK              10

struct linux_dirent64 {
        uint64_t d_ino;
        int64_t  d_off;
        uint16_t d_reclen;
        uint8_t  d_type;
        char     d_name[];
} __attribute__((packed));

struct linux_timespec {
        int64_t tv_sec;
        int64_t tv_nsec;
} __attribute__((packed));

struct linux_stat {
        uint64_t st_dev;
        uint64_t st_ino;
        uint64_t st_nlink;
        uint32_t st_mode;
        uint32_t st_uid;
        uint32_t st_gid;
        uint32_t __pad0;
        uint64_t st_rdev;
        int64_t  st_size;
        int64_t  st_blksize;
        int64_t  st_blocks;
        uint64_t st_atime;
        uint64_t st_atime_nsec;
        uint64_t st_mtime;
        uint64_t st_mtime_nsec;
        uint64_t st_ctime;
        uint64_t st_ctime_nsec;
        int64_t  __unused[3];
} __attribute__((packed));

struct linux_utss {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
} __attribute__((packed));

struct linux_winsize {
        uint16_t ws_row;
        uint16_t ws_col;
        uint16_t ws_xpixel;
        uint16_t ws_ypixel;
} __attribute__((packed));

struct linux_termios {
        uint32_t c_iflag;
        uint32_t c_oflag;
        uint32_t c_cflag;
        uint32_t c_lflag;
        uint8_t  c_line;
        uint8_t  c_cc[19];
} __attribute__((packed));

struct linux_pollfd {
        int32_t  fd;
        int16_t  events;
        int16_t  revents;
} __attribute__((packed));

struct linux_rlimit {
        uint64_t rlim_cur;
        uint64_t rlim_max;
};

struct linux_iovec {
        uint64_t iov_base;
        uint64_t iov_len;
};

static int
user_ptr_ok (uint64_t addr, size_t len)
{
        if (addr < USER_ADDR_MIN || addr > USER_ADDR_MAX) {
                return 0;
        }

        if (len > USER_ADDR_MAX - addr) {
                return 0;
        }

        return 1;
}

static inline void
wrmsr (uint32_t msr, uint64_t value)
{
        uint32_t low = (uint32_t) value;
        uint32_t high = (uint32_t) (value >> 32);

        __asm__ volatile ("wrmsr" :: "c" (msr), "a" (low), "d" (high) : "memory");
}

static inline uint64_t
rdmsr (uint32_t msr)
{
        uint32_t low;
        uint32_t high;

        __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
        return ((uint64_t) high << 32) | low;
}

int
copy_from_user (void *kbuf, uint64_t uaddr, size_t len)
{
        if (kbuf == NULL || !user_ptr_ok (uaddr, len)) {
                return -1;
        }

        memcpy (kbuf, (const void *) (uintptr_t) uaddr, len);
        return 0;
}

int
copy_to_user (uint64_t uaddr, const void *kbuf, size_t len)
{
        if (kbuf == NULL || !user_ptr_ok (uaddr, len)) {
                return -1;
        }

        memcpy ((void *) (uintptr_t) uaddr, kbuf, len);
        return 0;
}

int
copy_str_from_user (char *kbuf, uint64_t uaddr, size_t kbuf_len)
{
        size_t i;

        if (kbuf == NULL || kbuf_len == 0) {
                return -1;
        }

        for (i = 0; i < kbuf_len - 1; i++) {
                char c;

                if (copy_from_user (&c, uaddr + i, 1) != 0) {
                        return -1;
                }

                kbuf[i] = c;

                if (c == '\0') {
                        return 0;
                }
        }

        kbuf[kbuf_len - 1] = '\0';
        return -1;
}

static int
copy_user_string_array (char kargv[][256], uint64_t uargv, unsigned max)
{
        unsigned i;

        for (i = 0; i < max - 1; i++) {
                uint64_t uptr;

                if (copy_from_user (&uptr, uargv + (uint64_t) i * 8ULL, 8) != 0) {
                        return -1;
                }

                if (uptr == 0) {
                        kargv[i][0] = '\0';
                        return 0;
                }

                if (copy_str_from_user (kargv[i], uptr, 256) != 0) {
                        return -1;
                }
        }

        return -1;
}

static int
vfs_type_to_dirent (enum vfs_type type)
{
        switch (type) {
        case VFS_TYPE_DIR:
                return DT_DIR;
        case VFS_TYPE_FILE:
                return DT_REG;
        case VFS_TYPE_SYMLINK:
                return DT_LNK;
        default:
                return DT_UNKNOWN;
        }
}

static int
fd_alloc (struct vfs_file **out)
{
        struct process *p = proc_current ();
        unsigned i;

        for (i = 0; i < PROC_MAX_FD; i++) {
                if (p->fds[i] == NULL) {
                        struct vfs_file *f = (struct vfs_file *) kmalloc (sizeof (*f));

                        if (f == NULL) {
                                return -1;
                        }

                        p->fds[i] = f;
                        f->inode = NULL;
                        f->pos = 0;
                        f->path[0] = '\0';
                        f->pipe = NULL;
                        f->pipe_end = -1;
                        *out = f;
                        return (int) i;
                }
        }

        return -1;
}

static struct vfs_file *
fd_get (int fd)
{
        struct process *p = proc_current ();

        if (fd < 0 || fd >= PROC_MAX_FD) {
                return NULL;
        }

        return p->fds[fd];
}

static int
is_console_path (const char *path)
{
        return path != NULL
                && (strcmp (path, "/dev/console") == 0
                    || strcmp (path, "/dev/tty") == 0);
}

static int
is_null_path (const char *path)
{
        return path != NULL && strcmp (path, "/dev/null") == 0;
}

static void
strncat_simple (char *dest, const char *src, size_t destlen)
{
        size_t len = strlen (dest);
        size_t i;

        for (i = 0; len + i + 1 < destlen && src[i] != '\0'; i++) {
                dest[len + i] = src[i];
        }

        dest[len + i] = '\0';
}

static void
resolve_path (const char *path, char *out, size_t outlen)
{
        struct process *p = proc_current ();

        if (path == NULL || outlen == 0) {
                if (outlen > 0) {
                        out[0] = '\0';
                }
                return;
        }

        if (path[0] == '/') {
                strncpy (out, path, outlen - 1U);
                out[outlen - 1U] = '\0';
                return;
        }

        if (strcmp (p->cwd, "/") == 0) {
                out[0] = '/';
                strncpy (out + 1, path, outlen - 2U);
                out[outlen - 1U] = '\0';
        } else {
                strncpy (out, p->cwd, outlen - 1U);
                out[outlen - 1U] = '\0';
                strncat_simple (out, "/", outlen);
                strncat_simple (out, path, outlen);
        }
}

static int
fd_dup_to (int oldfd, int newfd)
{
        struct process *p = proc_current ();
        struct vfs_file *src = fd_get (oldfd);
        struct vfs_file *dst;
        int fd;

        if (src == NULL) {
                return ERR (E_BADF);
        }

        if (newfd < 0) {
                fd = fd_alloc (&dst);
                if (fd < 0) {
                        return ERR (E_NOMEM);
                }
        } else {
                if (newfd < 0 || newfd >= PROC_MAX_FD) {
                        return ERR (E_BADF);
                }

                if (p->fds[newfd] != NULL) {
                        vfs_close (p->fds[newfd]);
                        kfree (p->fds[newfd]);
                }

                dst = (struct vfs_file *) kmalloc (sizeof (*dst));
                if (dst == NULL) {
                        return ERR (E_NOMEM);
                }

                p->fds[newfd] = dst;
                fd = newfd;
        }

        memcpy (dst, src, sizeof (*dst));
        if (dst->pipe != NULL && dst->pipe_end == 0) {
                dst->pipe->readers++;
        } else if (dst->pipe != NULL && dst->pipe_end == 1) {
                dst->pipe->writers++;
        }

        return fd;
}

static int
fd_dup_from (int oldfd, int minfd)
{
        struct process *p = proc_current ();
        struct vfs_file *src = fd_get (oldfd);
        struct vfs_file *dst;
        int fd;

        if (src == NULL || minfd < 0 || minfd >= PROC_MAX_FD) {
                return ERR (E_BADF);
        }

        for (fd = minfd; fd < PROC_MAX_FD; fd++) {
                if (p->fds[fd] == NULL) {
                        break;
                }
        }

        if (fd >= PROC_MAX_FD) {
                return ERR (E_BADF);
        }

        dst = (struct vfs_file *) kmalloc (sizeof (*dst));
        if (dst == NULL) {
                return ERR (E_NOMEM);
        }

        memcpy (dst, src, sizeof (*dst));
        if (dst->pipe != NULL && dst->pipe_end == 0) {
                dst->pipe->readers++;
        } else if (dst->pipe != NULL && dst->pipe_end == 1) {
                dst->pipe->writers++;
        }

        p->fds[fd] = dst;
        return fd;
}

static int64_t
sys_pipe_impl (uint64_t ufd, int close_on_exec)
{
        struct pipe *pipe = proc_pipe_alloc ();
        struct vfs_file *readf;
        struct vfs_file *writef;
        int readfd;
        int writefd;
        int userfds[2];

        (void) close_on_exec;

        if (pipe == NULL) {
                return ERR (E_NOMEM);
        }

        readfd = fd_alloc (&readf);
        if (readfd < 0) {
                proc_pipe_free (pipe);
                return ERR (E_NOMEM);
        }

        writefd = fd_alloc (&writef);
        if (writefd < 0) {
                kfree (proc_current ()->fds[readfd]);
                proc_current ()->fds[readfd] = NULL;
                proc_pipe_free (pipe);
                return ERR (E_NOMEM);
        }

        pipe->readers = 1;
        pipe->writers = 1;

        readf->pipe = pipe;
        readf->pipe_end = 0;
        readf->inode = NULL;
        readf->path[0] = '\0';

        writef->pipe = pipe;
        writef->pipe_end = 1;
        writef->inode = NULL;
        writef->path[0] = '\0';

        userfds[0] = readfd;
        userfds[1] = writefd;

        if (copy_to_user (ufd, userfds, sizeof (userfds)) != 0) {
                proc_current ()->fds[readfd] = NULL;
                proc_current ()->fds[writefd] = NULL;
                kfree (readf);
                kfree (writef);
                proc_pipe_free (pipe);
                return ERR (E_FAULT);
        }

        return 0;
}

static ssize_t
pipe_read (struct pipe *pipe, void *buf, size_t count)
{
        uint8_t *out = (uint8_t *) buf;
        size_t done = 0;

        if (pipe == NULL || buf == NULL) {
                return -1;
        }

        while (done < count && pipe->count > 0) {
                out[done++] = pipe->buf[pipe->tail];
                pipe->tail = (pipe->tail + 1U) % sizeof (pipe->buf);
                pipe->count--;
        }

        return (ssize_t) done;
}

static ssize_t
pipe_write (struct pipe *pipe, const void *buf, size_t count)
{
        const uint8_t *in = (const uint8_t *) buf;
        size_t done = 0;

        if (pipe == NULL || buf == NULL) {
                return -1;
        }

        while (done < count && pipe->count < sizeof (pipe->buf)) {
                pipe->buf[pipe->head] = in[done++];
                pipe->head = (pipe->head + 1U) % sizeof (pipe->buf);
                pipe->count++;
        }

        return (ssize_t) done;
}

static int
sys_open_path (const char *path, int flags)
{
        struct vfs_file *file;
        int fd;

        (void) flags;

        fd = fd_alloc (&file);
        if (fd < 0) {
                return ERR (E_NOMEM);
        }

        if (is_console_path (path) || is_null_path (path)) {
                strncpy (file->path, path, sizeof (file->path) - 1U);
                file->path[sizeof (file->path) - 1U] = '\0';
                file->inode = NULL;
                file->pipe = NULL;
                file->pipe_end = -1;
                file->pos = 0;
                return fd;
        }

        if (vfs_open (path, file) != 0) {
                kfree (proc_current ()->fds[fd]);
                proc_current ()->fds[fd] = NULL;
                return ERR (E_NOENT);
        }

        return fd;
}

static int
fd_is_tty_out (int fd)
{
        struct vfs_file *file;

        if (fd == 1 || fd == 2) {
                return 1;
        }

        file = fd_get (fd);
        return file != NULL && is_console_path (file->path);
}

static int64_t
sys_write (int fd, uint64_t buf, size_t count)
{
        char kbuf[512];
        size_t done = 0;
        struct vfs_file *file = fd_get (fd);

        if (file != NULL && is_null_path (file->path)) {
                return (int64_t) count;
        }

        if (file != NULL && file->pipe != NULL && file->pipe_end == 1) {
                char kbuf[512];
                size_t done = 0;

                while (done < count) {
                        size_t chunk = count - done;
                        ssize_t n;

                        if (chunk > sizeof (kbuf)) {
                                chunk = sizeof (kbuf);
                        }

                        if (copy_from_user (kbuf, buf + done, chunk) != 0) {
                                return ERR (E_FAULT);
                        }

                        for (;;) {
                                n = pipe_write (file->pipe, kbuf, chunk);
                                if (n < 0) {
                                        return ERR (E_FAULT);
                                }

                                if (n > 0) {
                                        sched_wake_all ();
                                        break;
                                }

                                if (file->pipe->readers == 0) {
                                        return ERR (EPIPE);
                                }

                                sched_block_current ();
                        }

                        done += (size_t) n;
                }

                return (int64_t) done;
        }

        if (fd_is_tty_out (fd)) {
                while (done < count) {
                        size_t chunk = count - done;

                        if (chunk > sizeof (kbuf)) {
                                chunk = sizeof (kbuf);
                        }

                        if (copy_from_user (kbuf, buf + done, chunk) != 0) {
                                return ERR (E_FAULT);
                        }

                        for (size_t i = 0; i < chunk; i++) {
                                vga_putchar (kbuf[i]);
                        }

                        done += chunk;
                }

                return (int64_t) done;
        }

        if (file == NULL) {
                return ERR (E_BADF);
        }

        return (int64_t) count;
}

static int64_t
sys_read (int fd, uint64_t buf, size_t count)
{
        char kbuf[256];
        struct vfs_file *file = fd_get (fd);
        ssize_t n;

        if (file != NULL && file->pipe != NULL && file->pipe_end == 0) {
                for (;;) {
                        ssize_t n = pipe_read (file->pipe, kbuf, count);

                        if (n < 0) {
                                return ERR (E_FAULT);
                        }

                        if (n > 0) {
                                if (copy_to_user (buf, kbuf, (size_t) n) != 0) {
                                        return ERR (E_FAULT);
                                }
                                return n;
                        }

                        if (file->pipe->writers == 0) {
                                return 0;
                        }

                        sched_block_current ();
                }
        }

        if (fd == 0 || (file != NULL && is_console_path (file->path))) {
                if (count == 0) {
                        return 0;
                }

                {
                        int c = keyboard_getchar ();
                        char ch = (char) c;

                        if (copy_to_user (buf, &ch, 1) != 0) {
                                return ERR (E_FAULT);
                        }

                        return 1;
                }
        }

        if (file != NULL && is_null_path (file->path)) {
                return 0;
        }

        file = fd_get (fd);
        if (file == NULL) {
                return ERR (E_BADF);
        }

        if (count > sizeof (kbuf)) {
                count = sizeof (kbuf);
        }

        n = vfs_read (file, kbuf, count);
        if (n < 0) {
                return ERR (E_FAULT);
        }

        if (n > 0 && copy_to_user (buf, kbuf, (size_t) n) != 0) {
                return ERR (E_FAULT);
        }

        return n;
}

static int64_t
sys_writev (int fd, uint64_t iovp, unsigned long iovcnt)
{
        unsigned long i;
        int64_t total = 0;

        for (i = 0; i < iovcnt; i++) {
                struct linux_iovec iov;
                int64_t n;

                if (copy_from_user (&iov, iovp + (uint64_t) i * sizeof (iov), sizeof (iov)) != 0) {
                        return total > 0 ? total : ERR (E_FAULT);
                }

                n = sys_write (fd, iov.iov_base, (size_t) iov.iov_len);
                if (n < 0) {
                        return total > 0 ? total : n;
                }

                total += n;
                if ((size_t) n < (size_t) iov.iov_len) {
                        break;
                }
        }

        return total;
}

static int64_t
sys_brk (uint64_t addr)
{
        struct process *p = proc_current ();
        uint64_t old = p->brk;

        if (addr == 0) {
                return (int64_t) old;
        }

        if (addr < USER_BRK_START) {
                return (int64_t) old;
        }

        while (p->brk < addr) {
                void *frame = paging_alloc_page ();

                if (frame == NULL) {
                        return (int64_t) old;
                }

                if (paging_clear_phys ((uint64_t) (uintptr_t) frame, PAGE_SIZE) != 0) {
                        paging_free_page (frame);
                        return (int64_t) old;
                }

                if (paging_map_user_page (p->brk, (uint64_t) (uintptr_t) frame,
                                           PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                        paging_free_page (frame);
                        return (int64_t) old;
                }

                p->brk += PAGE_SIZE;
        }

        p->brk = (addr + PAGE_SIZE - 1ULL) & PAGE_MASK;
        if (p->brk < addr) {
                p->brk = addr;
        }

        return (int64_t) addr;
}

static int64_t
sys_mmap (uint64_t addr, uint64_t len, uint64_t prot, uint64_t flags, int64_t fd,
          uint64_t offset)
{
        uint64_t map_addr;
        uint64_t page_flags = PAGE_PRESENT | PAGE_USER;

        (void) prot;
        (void) fd;
        (void) offset;

        if (len == 0 || (flags & 0x20U) == 0) { /* MAP_ANONYMOUS */
                return ERR (E_INVAL);
        }

        if (prot & 0x2U) {
                page_flags |= PAGE_WRITABLE;
        }

        map_addr = (addr != 0) ? addr : USER_BRK_START + 0x1000000ULL;
        map_addr = (map_addr + PAGE_SIZE - 1ULL) & PAGE_MASK;

        if (paging_map_user_range (map_addr, (size_t) len, page_flags) != 0) {
                return ERR (E_NOMEM);
        }

        return (int64_t) map_addr;
}

static int64_t
sys_lseek (int fd, int64_t offset, int whence)
{
        struct vfs_file *file = fd_get (fd);
        struct vfs_stat vs;

        if (file == NULL) {
                return ERR (E_BADF);
        }

        if (file->inode == NULL) {
                return 0;
        }

        if (vfs_fstat (file, &vs) != 0 || vs.type != VFS_TYPE_FILE) {
                return 0;
        }

        switch (whence) {
        case 0:
                file->pos = (size_t) offset;
                break;
        case 1:
                file->pos = (size_t) ((int64_t) file->pos + offset);
                break;
        case 2:
                file->pos = (size_t) ((int64_t) vs.size + offset);
                break;
        default:
                return ERR (E_INVAL);
        }

        return (int64_t) file->pos;
}

static int64_t
sys_getdents64 (int fd, uint64_t buf, size_t count)
{
        struct vfs_file *file = fd_get (fd);
        struct vfs_stat vs;
        struct vfs_dirent entries[32];
        int n;
        size_t out = 0;
        unsigned i;

        if (file == NULL || vfs_fstat (file, &vs) != 0 || vs.type != VFS_TYPE_DIR) {
                return ERR (E_NOTDIR);
        }

        n = vfs_readdir (file->path, entries, 32);
        if (n < 0) {
                return ERR (E_FAULT);
        }

        for (i = (unsigned) file->pos; i < (unsigned) n; i++) {
                size_t namelen = strlen (entries[i].name) + 1;
                size_t reclen = (sizeof (struct linux_dirent64) + namelen + 7U) & ~7U;
                struct linux_dirent64 ent;

                if (out + reclen > count) {
                        break;
                }

                memset (&ent, 0, sizeof (ent));
                ent.d_ino = (uint64_t) (i + 1);
                ent.d_off = (int64_t) (i + 1);
                ent.d_reclen = (uint16_t) reclen;
                ent.d_type = (uint8_t) vfs_type_to_dirent (entries[i].type);

                if (copy_to_user (buf + out, &ent, sizeof (ent)) != 0) {
                        return ERR (E_FAULT);
                }

                if (copy_to_user (buf + out + sizeof (ent), entries[i].name, namelen) != 0) {
                        return ERR (E_FAULT);
                }

                out += reclen;
                file->pos = i + 1;
        }

        return (int64_t) out;
}

static int
fill_stat (struct linux_stat *st, const struct vfs_stat *vs)
{
        memset (st, 0, sizeof (*st));
        st->st_mode = vs->mode;
        st->st_size = (int64_t) vs->size;
        st->st_nlink = 1;
        return 0;
}

int64_t
syscall_dispatch (uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                  uint64_t a3, uint64_t a4, uint64_t a5)
{
        (void) a4;
        (void) a5;

        switch (num) {
        case SYS_read:
                return sys_read ((int) a0, a1, (size_t) a2);

        case SYS_write:
                return sys_write ((int) a0, a1, (size_t) a2);

        case SYS_writev:
                return sys_writev ((int) a0, a1, (unsigned long) a2);

        case SYS_readv: {
                struct linux_iovec iov;

                if (copy_from_user (&iov, a1, sizeof (iov)) != 0) {
                        return ERR (E_FAULT);
                }

                return sys_read ((int) a0, iov.iov_base, (size_t) iov.iov_len);
        }

        case SYS_open:
        case SYS_openat:
        case SYS_openat2: {
                char path[256];
                char resolved[256];
                int flags = 0;

                if (num == SYS_openat) {
                        if (copy_str_from_user (path, a1, sizeof (path)) != 0) {
                                return ERR (E_FAULT);
                        }

                        flags = (int) a2;
                } else if (num == SYS_openat2) {
                        if (copy_str_from_user (path, a1, sizeof (path)) != 0) {
                                return ERR (E_FAULT);
                        }

                        flags = 0;
                } else {
                        if (copy_str_from_user (path, a0, sizeof (path)) != 0) {
                                return ERR (E_FAULT);
                        }

                        flags = (int) a1;
                }

                resolve_path (path, resolved, sizeof (resolved));
                return sys_open_path (resolved, flags);
        }

        case SYS_close: {
                struct process *p = proc_current ();
                int fd = (int) a0;
                struct vfs_file *file;

                if (fd < 0 || fd >= PROC_MAX_FD || p->fds[fd] == NULL) {
                        return ERR (E_BADF);
                }

                file = p->fds[fd];

                if (file->pipe != NULL) {
                        if (file->pipe_end == 0 && file->pipe->readers > 0) {
                                file->pipe->readers--;
                        } else if (file->pipe_end == 1 && file->pipe->writers > 0) {
                                file->pipe->writers--;
                        }

                        if (file->pipe->readers == 0 && file->pipe->writers == 0) {
                                proc_pipe_free (file->pipe);
                        }
                } else {
                        vfs_close (file);
                }

                kfree (file);
                p->fds[fd] = NULL;
                return 0;
        }

        case SYS_dup:
                return fd_dup_to ((int) a0, -1);

        case SYS_dup2:
                if ((int) a1 < 0 || (int) a1 >= PROC_MAX_FD) {
                        return ERR (E_BADF);
                }
                return fd_dup_to ((int) a0, (int) a1);

        case SYS_dup3:
                if ((int) a1 < 0 || (int) a1 >= PROC_MAX_FD) {
                        return ERR (E_BADF);
                }
                return fd_dup_to ((int) a0, (int) a1);

        case SYS_pipe:
                return sys_pipe_impl (a0, 0);

        case SYS_pipe2:
                return sys_pipe_impl (a0, (int) a1);

        case SYS_fork:
        case SYS_vfork:
                return proc_fork ();

        case SYS_clone:
                if (a0 & CLONE_VM) {
                        if (a0 & CLONE_SETTLS) {
                                proc_set_fs_base (a4);
                        }
                        return proc_current ()->pid;
                }
                return proc_fork ();

        case SYS_wait4: {
                int status = 0;
                int pid = (int) a0;
                int reaped;

                if (!proc_has_children ()) {
                        return ERR (E_CHLD);
                }

                for (;;) {
                        reaped = proc_fork_take_status (pid, &status);
                        if (reaped >= 0) {
                                if (a1 != 0 && copy_to_user (a1, &status, sizeof (status)) != 0) {
                                        return ERR (E_FAULT);
                                }
                                return reaped;
                        }

                        if (!proc_has_children ()) {
                                return ERR (E_CHLD);
                        }

                        if (proc_run_runnable ()) {
                                continue;
                        }

                        sched_block_current ();
                }
        }

        case SYS_kill:
                return 0;

        case SYS_socket:
                return sys_open_path ("/dev/null", 0);

        case SYS_setsid:
                proc_current ()->sid = proc_current ()->pid;
                return proc_current ()->sid;

        case SYS_setpgid:
                if ((int) a0 == 0 || (int) a0 == proc_current ()->pid) {
                        proc_current ()->pgid = (int) a1;
                        return 0;
                }
                return ERR (E_INVAL);

        case SYS_getpgid:
                return proc_current ()->pgid;

        case SYS_getpgrp:
                return proc_current ()->pgid;

        case SYS_getppid:
                return proc_current ()->ppid;

        case SYS_getcwd: {
                struct process *p = proc_current ();
                size_t len = strlen (p->cwd) + 1;

                if (a1 < len) {
                        return ERR (E_INVAL);
                }

                if (copy_to_user (a0, p->cwd, len) != 0) {
                        return ERR (E_FAULT);
                }

                return (int64_t) len;
        }

        case SYS_chdir: {
                char path[256];
                char resolved[256];
                struct vfs_stat st;

                if (copy_str_from_user (path, a0, sizeof (path)) != 0) {
                        return ERR (E_FAULT);
                }

                resolve_path (path, resolved, sizeof (resolved));

                if (vfs_stat (resolved, &st) != 0 || st.type != VFS_TYPE_DIR) {
                        return ERR (E_NOENT);
                }

                strncpy (proc_current ()->cwd, resolved, sizeof (proc_current ()->cwd) - 1U);
                proc_current ()->cwd[sizeof (proc_current ()->cwd) - 1U] = '\0';
                return 0;
        }

        case SYS_access: {
                char path[256];
                char resolved[256];
                struct vfs_stat st;

                if (copy_str_from_user (path, a0, sizeof (path)) != 0) {
                        return ERR (E_FAULT);
                }

                resolve_path (path, resolved, sizeof (resolved));

                if (vfs_stat (resolved, &st) != 0) {
                        return ERR (E_NOENT);
                }

                return 0;
        }

        case SYS_unlink: {
                char path[256];
                char resolved[256];

                if (copy_str_from_user (path, a0, sizeof (path)) != 0) {
                        return ERR (E_FAULT);
                }

                resolve_path (path, resolved, sizeof (resolved));

                if (is_console_path (resolved) || is_null_path (resolved)) {
                        return ERR (E_ACCES);
                }

                return 0;
        }

        case SYS_fcntl: {
                int cmd = (int) a1;

                switch (cmd) {
                case F_DUPFD:
                        return fd_dup_from ((int) a0, (int) a2);
                case F_DUPFD_CLOEXEC:
                        return fd_dup_from ((int) a0, (int) a2);
                case F_GETFD:
                        return 0;
                case F_SETFD:
                        return 0;
                case F_GETFL:
                        return (int64_t) (O_RDONLY | 0200000U);
                default:
                        return 0;
                }
        }

        case SYS_ioctl: {
                struct linux_winsize ws = { 25, 80, 0, 0 };
                struct linux_termios term;

                switch (a1) {
                case TIOCGWINSZ:
                        if (copy_to_user (a2, &ws, sizeof (ws)) != 0) {
                                return ERR (E_FAULT);
                        }
                        return 0;
                case TIOCGPGRP: {
                        int pgid = proc_current ()->pgid;

                        if (copy_to_user (a2, &pgid, sizeof (pgid)) != 0) {
                                return ERR (E_FAULT);
                        }
                        return 0;
                }
                case TIOCSPGRP:
                        return 0;
                case TCGETS:
                        memset (&term, 0, sizeof (term));
                        term.c_cflag = 0x4B00U;
                        term.c_cc[0] = 3;
                        if (copy_to_user (a2, &term, sizeof (term)) != 0) {
                                return ERR (E_FAULT);
                        }
                        return 0;
                default:
                        return 0;
                }
        }

        case SYS_poll: {
                struct linux_pollfd pfd;
                int32_t ready = 0;

                if (a0 == 0 || a2 == 0) {
                        return 0;
                }

                if (copy_from_user (&pfd, a0, sizeof (pfd)) != 0) {
                        return ERR (E_FAULT);
                }

                if (pfd.fd >= 0 && fd_get (pfd.fd) != NULL) {
                        pfd.revents = (int16_t) (pfd.events & POLLIN);
                        ready = 1;
                }

                if (copy_to_user (a0, &pfd, sizeof (pfd)) != 0) {
                        return ERR (E_FAULT);
                }

                return ready;
        }

        case SYS_prlimit64: {
                struct linux_rlimit rl = { 0x10000000ULL, 0x10000000ULL };

                if (a2 != 0 && copy_to_user (a2, &rl, sizeof (rl)) != 0) {
                        return ERR (E_FAULT);
                }

                return 0;
        }

        case SYS_futex:
        case SYS_mbind:
                return 0;

        case SYS_rt_sigaction:
        case SYS_rt_sigprocmask:
        case SYS_set_tid_address:
                return 0;

        case SYS_sched_getparam:
        case SYS_sched_setscheduler:
        case SYS_getitimer:
        case SYS_setitimer:
        case SYS_clock_nanosleep:
        case SYS_madvise:
        case SYS_reboot:
        case SYS_mprotect:
                return 0;

        case SYS_brk:
                return sys_brk (a0);

        case SYS_mmap:
                return sys_mmap (a0, a1, a2, a3, (int64_t) a4, a5);

        case SYS_munmap:
                return 0;

        case SYS_lseek:
                return sys_lseek ((int) a0, (int64_t) a1, (int) a2);

        case SYS_getdents64:
                return sys_getdents64 ((int) a0, a1, (size_t) a2);

        case SYS_set_robust_list:
                return 0;

        case SYS_rseq:
                return 0;

        case SYS_arch_prctl:
                if (a0 == ARCH_SET_FS) {
                        proc_set_fs_base (a1);
                        return 0;
                }

                if (a0 == ARCH_GET_FS) {
                        uint64_t base = rdmsr (MSR_FS_BASE);

                        if (copy_to_user (a1, &base, sizeof (base)) != 0) {
                                return ERR (E_FAULT);
                        }

                        return 0;
                }

                return ERR (E_INVAL);

        case SYS_mount: {
                char source[64];
                char target[256];
                char fstype[32];

                if (copy_str_from_user (source, a0, sizeof (source)) != 0
                    || copy_str_from_user (target, a1, sizeof (target)) != 0
                    || copy_str_from_user (fstype, a2, sizeof (fstype)) != 0) {
                        return ERR (E_FAULT);
                }

                vfs_mkdir (target, 0755U);
                return 0;
        }

        case SYS_pivot_root:
                return 0;

        case SYS_clock_gettime: {
                struct linux_timespec ts = { 0, 0 };

                if (copy_to_user (a1, &ts, sizeof (ts)) != 0) {
                        return ERR (E_FAULT);
                }

                return 0;
        }

        case SYS_readlinkat:
        case SYS_readlink: {
                char path[256];
                char target[256];
                ssize_t len;
                uint64_t path_arg = a0;
                uint64_t buf_arg = a1;
                uint64_t bufsz_arg = a2;

                if (num == SYS_readlinkat) {
                        if ((int64_t) a0 != AT_FDCWD) {
                                return ERR (E_NOSYS);
                        }
                        path_arg = a1;
                        buf_arg = a2;
                        bufsz_arg = a3;
                }

                if (copy_str_from_user (path, path_arg, sizeof (path)) != 0) {
                        return ERR (E_FAULT);
                }

                len = vfs_readlink (path, target, sizeof (target));
                if (len < 0) {
                        return ERR (E_NOENT);
                }

                if ((size_t) len >= (size_t) bufsz_arg) {
                        len = (ssize_t) bufsz_arg - 1;
                }

                if (copy_to_user (buf_arg, target, (size_t) len) != 0) {
                        return ERR (E_FAULT);
                }

                return len;
        }

        case SYS_getrandom: {
                static const uint8_t fake_random[256] = { 0x42 };
                size_t len = (size_t) a1;

                if (len > sizeof (fake_random)) {
                        len = sizeof (fake_random);
                }

                if (copy_to_user (a0, fake_random, len) != 0) {
                        return ERR (E_FAULT);
                }

                return (int64_t) len;
        }

        case SYS_execve: {
                char path[256];
                char kargv[16][256];
                char kenvp[16][256];
                const char *argv_ptrs[17];
                const char *envp_ptrs[17];
                unsigned i;

                memset (kargv, 0, sizeof (kargv));
                memset (kenvp, 0, sizeof (kenvp));

                if (copy_str_from_user (path, a0, sizeof (path)) != 0) {
                        return ERR (E_FAULT);
                }

                if (copy_user_string_array (kargv, a1, 16) != 0) {
                        return ERR (E_FAULT);
                }

                if (copy_user_string_array (kenvp, a2, 16) != 0) {
                        return ERR (E_FAULT);
                }

                for (i = 0; i < 16; i++) {
                        argv_ptrs[i] = (kargv[i][0] != '\0') ? kargv[i] : NULL;
                        if (argv_ptrs[i] == NULL) {
                                break;
                        }
                }
                argv_ptrs[i] = NULL;

                for (i = 0; i < 16; i++) {
                        envp_ptrs[i] = (kenvp[i][0] != '\0') ? kenvp[i] : NULL;
                        if (envp_ptrs[i] == NULL) {
                                break;
                        }
                }
                envp_ptrs[i] = NULL;

                if (proc_exec (path, argv_ptrs, envp_ptrs) != 0) {
                        return ERR (E_NOENT);
                }

                return 0;
        }

        case SYS_getpid:
                return proc_current ()->pid;

        case SYS_getuid:
        case SYS_getgid:
        case SYS_geteuid:
        case SYS_getegid:
                return 0;

        case SYS_gettid:
                return proc_current ()->pid;

        case SYS_stat:
        case SYS_fstat:
        case SYS_newfstatat:
        case SYS_statx: {
                struct linux_stat lst;
                struct vfs_stat vs;
                char path[256];
                const char *kpath = NULL;
                uint64_t dst;

                if (num == SYS_newfstatat || num == SYS_statx) {
                        if (copy_str_from_user (path, a1, sizeof (path)) != 0) {
                                return ERR (E_FAULT);
                        }

                        if (path[0] != '/' && (int64_t) a0 == AT_FDCWD) {
                                char abs[280];
                                size_t plen = strlen (path);

                                if (plen + 2 >= sizeof (abs)) {
                                        return ERR (E_FAULT);
                                }

                                abs[0] = '/';
                                memcpy (abs + 1, path, plen + 1);
                                strncpy (path, abs, sizeof (path) - 1);
                                path[sizeof (path) - 1] = '\0';
                        }

                        if ((int64_t) a0 != AT_FDCWD && path[0] != '/') {
                                return ERR (E_NOSYS);
                        }

                        kpath = path;
                        dst = (num == SYS_statx) ? a2 : a2;
                } else if (num == SYS_stat) {
                        if (copy_str_from_user (path, a0, sizeof (path)) != 0) {
                                return ERR (E_FAULT);
                        }

                        kpath = path;
                        dst = a1;
                } else if (num == SYS_fstat) {
                        struct vfs_file *file = fd_get ((int) a0);

                        if (file == NULL) {
                                return ERR (E_BADF);
                        }

                        if (file->inode == NULL && is_console_path (file->path)) {
                                memset (&lst, 0, sizeof (lst));
                                lst.st_mode = 0200600U;
                                lst.st_nlink = 1;

                                if (copy_to_user (a1, &lst, sizeof (lst)) != 0) {
                                        return ERR (E_FAULT);
                                }

                                return 0;
                        }

                        if (vfs_fstat (file, &vs) != 0) {
                                return ERR (E_BADF);
                        }

                        fill_stat (&lst, &vs);

                        if (copy_to_user (a1, &lst, sizeof (lst)) != 0) {
                                return ERR (E_FAULT);
                        }

                        return 0;
                } else {
                        return ERR (E_NOSYS);
                }

                if (vfs_stat (kpath, &vs) != 0) {
                        return ERR (E_NOENT);
                }

                fill_stat (&lst, &vs);

                if (copy_to_user (dst, &lst, sizeof (lst)) != 0) {
                        return ERR (E_FAULT);
                }

                return 0;
        }

        case SYS_uname: {
                struct linux_utss uts;

                memset (&uts, 0, sizeof (uts));
                strncpy (uts.sysname, "Linux", sizeof (uts.sysname) - 1);
                strncpy (uts.nodename, "TypeOS", sizeof (uts.nodename) - 1);
                strncpy (uts.release, "6.1.0", sizeof (uts.release) - 1);
                strncpy (uts.version, "TypeOS", sizeof (uts.version) - 1);
                strncpy (uts.machine, "x86_64", sizeof (uts.machine) - 1);

                if (copy_to_user (a0, &uts, sizeof (uts)) != 0) {
                        return ERR (E_FAULT);
                }

                return 0;
        }

        case SYS_exit:
        case SYS_exit_group:
                kprintf ("\n[process exited with code %lld]\n", (long long) a0);
                proc_exit ((int) a0);
                if (proc_current ()->state == TASK_ZOMBIE) {
                        proc_run_runnable ();
                        for (;;) {
                                __asm__ volatile ("sti; hlt" ::: "memory");
                        }
                }
                return 0;

        default:
                return ERR (E_NOSYS);
        }
}
