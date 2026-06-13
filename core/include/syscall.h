#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"

#define USER_ADDR_MIN   0x0000000000400000ULL
#define USER_ADDR_MAX   0x00000007FFFFFFFFULL

int64_t syscall_dispatch (uint64_t num, uint64_t a0, uint64_t a1,
                           uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

int copy_from_user (void *kbuf, uint64_t uaddr, size_t len);
int copy_to_user (uint64_t uaddr, const void *kbuf, size_t len);
int copy_str_from_user (char *kbuf, uint64_t uaddr, size_t kbuf_len);

#endif
