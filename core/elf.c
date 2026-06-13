/*
 * elf.c - ELF64 loader for static Linux binaries.
 */

#include "include/elf.h"
#include "include/fs/vfs.h"
#include "include/paging.h"
#include "include/proc.h"
#include "include/string.h"
#include "include/vga.h"

static int
elf_check_hdr (const struct elf64_hdr *hdr)
{
        if (hdr->e_ident[0] != 0x7FU || hdr->e_ident[1] != (unsigned char) 'E'
            || hdr->e_ident[2] != (unsigned char) 'L' || hdr->e_ident[3] != (unsigned char) 'F') {
                return -1;
        }

        if (hdr->e_ident[4] != ELFCLASS64 || hdr->e_ident[5] != ELFDATA2LSB) {
                return -1;
        }

        if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) {
                return -1;
        }

        if (hdr->e_machine != 0x3EU) { /* EM_X86_64 */
                return -1;
        }

        return 0;
}

static uint64_t
elf_segment_flags (uint32_t p_flags)
{
        uint64_t flags = PAGE_PRESENT;

        if (p_flags & PF_W) {
                flags |= PAGE_WRITABLE;
        }

        return flags;
}

static int
elf_map_segment (const char *path, const struct elf64_phdr *ph)
{
        uint64_t seg_start = ph->p_vaddr & PAGE_MASK;
        uint64_t seg_end = (ph->p_vaddr + ph->p_memsz + PAGE_SIZE - 1ULL) & PAGE_MASK;
        uint64_t flags = elf_segment_flags (ph->p_flags);
        uint64_t file_end = ph->p_vaddr + ph->p_filesz;
        uint64_t addr;

        if (seg_start < USER_IMAGE_PHYS_START
            || seg_end > USER_IMAGE_PHYS_END) {
                kprintf ("ELF: segment outside fixed image window: %p-%p\n",
                         (void *) (uintptr_t) seg_start,
                         (void *) (uintptr_t) seg_end);
                return -1;
        }

        paging_split_huge_range (seg_start, seg_end);

        for (addr = seg_start; addr < seg_end; addr += PAGE_SIZE) {
                uint64_t page_end = addr + PAGE_SIZE;
                uint64_t copy_start;
                uint64_t copy_end;

                copy_start = addr < ph->p_vaddr ? ph->p_vaddr : addr;
                copy_end = page_end < file_end ? page_end : file_end;

                paging_clear_phys (addr, PAGE_SIZE);

                if (copy_start < copy_end) {
                        size_t len = (size_t) (copy_end - copy_start);
                        size_t src_off = (size_t) (ph->p_offset + (copy_start - ph->p_vaddr));
                        char kbuf[4096];

                        if (len > sizeof (kbuf)) {
                                return -1;
                        }

                        if (vfs_read_at (path, src_off, kbuf, len) < 0) {
                                return -1;
                        }

                        if (paging_write_phys (copy_start, kbuf, len) != 0) {
                                return -1;
                        }
                }

                if (paging_promote_user_identity (addr, flags) != 0) {
                        return -1;
                }
        }

        return 0;
}

static uint64_t
elf_build_stack (const char *const *argv, const char *const *envp,
                 uint64_t *argc_out, uint64_t *argv_out, uint64_t *envp_out)
{
        uint64_t sp = USER_STACK_TOP;
        uint64_t argv_ptrs[16];
        uint64_t envp_ptrs[16];
        uint64_t argv_table = 0;
        uint64_t envp_table = 0;
        unsigned argc = 0;
        unsigned envc = 0;
        unsigned i;

        if (paging_map_user_range (USER_STACK_TOP - USER_STACK_SIZE,
                                   USER_STACK_SIZE + PAGE_SIZE,
                                   PAGE_PRESENT | PAGE_WRITABLE) != 0) {
                return 0;
        }

        while (argv != NULL && argv[argc] != NULL && argc < 15) {
                argc++;
        }

        while (envp != NULL && envp[envc] != NULL && envc < 15) {
                envc++;
        }

        for (i = 0; i < envc; i++) {
                size_t len = strlen (envp[i]) + 1;

                sp -= len;
                sp &= ~0xFULL;
                memcpy ((void *) (uintptr_t) sp, envp[i], len);
                envp_ptrs[i] = sp;
        }

        envp_ptrs[envc] = 0;

        for (i = 0; i < argc; i++) {
                size_t len = strlen (argv[i]) + 1;

                sp -= len;
                sp &= ~0xFULL;
                memcpy ((void *) (uintptr_t) sp, argv[i], len);
                argv_ptrs[i] = sp;
        }

        argv_ptrs[argc] = 0;

        {
                static const uint8_t random_seed[16] = {
                        0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                        0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
                };
                uint64_t random_ptr;

                sp -= 16;
                sp &= ~0xFULL;
                memcpy ((void *) (uintptr_t) sp, random_seed, 16);
                random_ptr = sp;

                sp -= 16;
                sp &= ~0xFULL;
                *(uint64_t *) (uintptr_t) (sp + 8) = 0ULL;
                *(uint64_t *) (uintptr_t) sp = 0ULL;

                sp -= 16;
                sp &= ~0xFULL;
                *(uint64_t *) (uintptr_t) (sp + 8) = random_ptr;
                *(uint64_t *) (uintptr_t) sp = 25ULL;

                sp -= 16;
                sp &= ~0xFULL;
                *(uint64_t *) (uintptr_t) (sp + 8) = 4096ULL;
                *(uint64_t *) (uintptr_t) sp = 6ULL;
        }

        {
                unsigned j;

                for (j = envc; j > 0; j--) {
                        sp -= 8;
                        *(uint64_t *) (uintptr_t) sp = envp_ptrs[j];
                }
                sp -= 8;
                *(uint64_t *) (uintptr_t) sp = envp_ptrs[0];
                envp_table = sp;

                for (j = argc; j > 0; j--) {
                        sp -= 8;
                        *(uint64_t *) (uintptr_t) sp = argv_ptrs[j];
                }
                sp -= 8;
                *(uint64_t *) (uintptr_t) sp = argv_ptrs[0];
                argv_table = sp;
        }

        sp -= 8;
        *(uint64_t *) (uintptr_t) sp = (uint64_t) argc;

        if (argc_out != NULL) {
                *argc_out = argc;
        }

        if (argv_out != NULL) {
                *argv_out = argv_table;
        }

        if (envp_out != NULL) {
                *envp_out = envp_table;
        }

        return sp;
}

int
elf_load (const char *path, uint64_t *entry_out, uint64_t *stack_top_out)
{
        struct elf64_hdr hdr;
        struct elf64_phdr ph;
        unsigned i;

        if (path == NULL || entry_out == NULL || stack_top_out == NULL) {
                return -1;
        }

        if (vfs_read_at (path, 0, &hdr, sizeof (hdr)) != (ssize_t) sizeof (hdr)) {
                kprintf ("ELF: cannot read header for %s\n", path);
                return -1;
        }

        if (elf_check_hdr (&hdr) != 0) {
                kprintf ("ELF: invalid header for %s\n", path);
                return -1;
        }

        for (i = 0; i < hdr.e_phnum; i++) {
                size_t off = (size_t) hdr.e_phoff + (size_t) i * sizeof (ph);

                if (vfs_read_at (path, off, &ph, sizeof (ph)) != (ssize_t) sizeof (ph)) {
                        return -1;
                }

                if (ph.p_type != PT_LOAD) {
                        continue;
                }

                if (elf_map_segment (path, &ph) != 0) {
                        kprintf ("ELF: segment map failed for %s\n", path);
                        return -1;
                }
        }

        *entry_out = hdr.e_entry;
        paging_flush_tlb ();
        kprintf ("ELF: loaded %s, entry %p\n", path, (void *) (uintptr_t) hdr.e_entry);
        return 0;
}

uint64_t
elf_user_stack_init (const char *const *argv, const char *const *envp,
                     uint64_t *argc_out, uint64_t *argv_out, uint64_t *envp_out)
{
        return elf_build_stack (argv, envp, argc_out, argv_out, envp_out);
}
