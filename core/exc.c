/*
 * exc.c - CPU exception handlers.
 */

#include "include/vga.h"
#include "include/paging.h"

static uint64_t
read_msr (uint32_t msr)
{
        uint32_t low;
        uint32_t high;

        __asm__ volatile ("rdmsr" : "=a" (low), "=d" (high) : "c" (msr));
        return ((uint64_t) high << 32) | low;
}

void
page_fault_handler (uint64_t error_code, uint64_t cr2, uint64_t rip, uint64_t cs)
{
        uint64_t pte = paging_pte_value (cr2);
        uint64_t cr0;
        uint64_t cr4;

        __asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));
        __asm__ volatile ("mov %%cr4, %0" : "=r" (cr4));

        kprintf ("\n#PF: cr2=%p err=%llx pte=%llx\n",
                 (void *) (uintptr_t) cr2,
                 (unsigned long long) error_code,
                 (unsigned long long) pte);
        kprintf ("#PF: cs=%llx rip=%p cr0=%llx cr4=%llx efer=%llx\n",
                 (unsigned long long) cs,
                 (void *) (uintptr_t) rip,
                 (unsigned long long) cr0,
                 (unsigned long long) cr4,
                 (unsigned long long) read_msr (0xC0000080U));

        for (;;) {
                __asm__ volatile ("hlt");
        }
}
