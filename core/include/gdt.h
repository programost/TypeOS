#ifndef GDT_H
#define GDT_H

#include "types.h"

#define GDT_KERNEL_CS   0x08U
#define GDT_KERNEL_DS   0x10U
#define GDT_USER_DS     0x1BU
#define GDT_USER_CS     0x23U

void gdt_init (void);
void gdt_set_rsp0 (uint64_t rsp0);
void enter_usermode (uint64_t rip, uint64_t rsp, uint64_t argc,
                     uint64_t argv, uint64_t envp);
void usermode_resume (uint64_t rip, uint64_t rsp, uint64_t rflags, uint64_t retval);
void usermode_resume_task (void *task);

#endif
