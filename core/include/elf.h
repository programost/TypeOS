#ifndef ELF_H
#define ELF_H

#include "types.h"

#define ELF_MAGIC       0x464C457FU
#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define ET_EXEC         2
#define ET_DYN          3
#define PT_LOAD         1

#define PF_X            1U
#define PF_W            2U
#define PF_R            4U

struct elf64_hdr {
        unsigned char e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint64_t e_entry;
        uint64_t e_phoff;
        uint64_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
} __attribute__((packed));

struct elf64_phdr {
        uint32_t p_type;
        uint32_t p_flags;
        uint64_t p_offset;
        uint64_t p_vaddr;
        uint64_t p_paddr;
        uint64_t p_filesz;
        uint64_t p_memsz;
        uint64_t p_align;
} __attribute__((packed));

int elf_load (const char *path, uint64_t *entry_out, uint64_t *stack_top_out);
uint64_t elf_user_stack_init (const char *const *argv, const char *const *envp,
                              uint64_t *argc_out, uint64_t *argv_out,
                              uint64_t *envp_out);

#endif
