/*
 * gdt.c - GDT, TSS, and SYSCALL/SYSRET setup.
 */

#include "include/gdt.h"
#include "include/types.h"

#define MSR_EFER            0xC0000080U
#define MSR_STAR            0xC0000081U
#define MSR_LSTAR           0xC0000082U
#define MSR_SFMASK          0xC0000084U

#define GDT_ENTRIES         7
#define TSS_STACK_SIZE      (64U * 1024U)

struct gdt_entry {
        uint16_t limit_low;
        uint16_t base_low;
        uint8_t  base_mid;
        uint8_t  access;
        uint8_t  granularity;
        uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
        uint16_t limit;
        uint64_t base;
} __attribute__((packed));

struct tss64 {
        uint32_t reserved0;
        uint64_t rsp0;
        uint64_t rsp1;
        uint64_t rsp2;
        uint64_t reserved1;
        uint64_t ist1;
        uint64_t ist2;
        uint64_t ist3;
        uint64_t ist4;
        uint64_t ist5;
        uint64_t ist6;
        uint64_t ist7;
        uint64_t reserved2;
        uint16_t reserved3;
        uint16_t iomap_base;
} __attribute__((packed));

extern void gdt_load (struct gdt_ptr *ptr);
extern void syscall_entry (void);

static struct gdt_entry gdt[GDT_ENTRIES];
static struct tss64 tss;
static uint8_t tss_stack[TSS_STACK_SIZE] __attribute__((aligned (16)));

uint64_t tss_rsp0;

static void
gdt_set_entry (size_t index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
        gdt[index].limit_low   = (uint16_t) (limit & 0xFFFFU);
        gdt[index].base_low    = (uint16_t) (base & 0xFFFFU);
        gdt[index].base_mid    = (uint8_t) ((base >> 16) & 0xFFU);
        gdt[index].access      = access;
        gdt[index].granularity = (uint8_t) ((limit >> 16) & 0x0FU) | gran;
        gdt[index].base_high   = (uint8_t) ((base >> 24) & 0xFFU);
}

static void
gdt_set_tss (size_t index, uint64_t base, uint32_t limit)
{
        struct {
                uint16_t limit_low;
                uint16_t base_low;
                uint8_t  base_mid;
                uint8_t  access;
                uint8_t  granularity;
                uint8_t  base_high;
                uint32_t base_upper;
                uint32_t reserved;
        } __attribute__((packed)) *desc = (void *) &gdt[index];

        desc->limit_low   = (uint16_t) (limit & 0xFFFFU);
        desc->base_low    = (uint16_t) (base & 0xFFFFU);
        desc->base_mid    = (uint8_t) ((base >> 16) & 0xFFU);
        desc->access      = 0x89;
        desc->granularity = (uint8_t) ((limit >> 16) & 0x0FU);
        desc->base_high   = (uint8_t) ((base >> 24) & 0xFFU);
        desc->base_upper  = (uint32_t) (base >> 32);
        desc->reserved    = 0;
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

void
gdt_set_rsp0 (uint64_t rsp0)
{
        tss.rsp0 = rsp0;
        tss_rsp0 = rsp0;
}

void
gdt_init (void)
{
        struct gdt_ptr ptr;

        gdt_set_entry (0, 0, 0, 0, 0);
        gdt_set_entry (1, 0, 0, 0x9A, 0xA0);  /* kernel code 64-bit */
        gdt_set_entry (2, 0, 0, 0x92, 0xC0);  /* kernel data */
        gdt_set_entry (3, 0, 0, 0xF2, 0xC0);  /* user data   DPL=3 */
        gdt_set_entry (4, 0, 0, 0xFA, 0xA0);  /* user code   DPL=3 */

        tss.iomap_base = sizeof (tss);
        gdt_set_rsp0 ((uint64_t) (uintptr_t) (tss_stack + TSS_STACK_SIZE));

        gdt_set_tss (5, (uint64_t) (uintptr_t) &tss, sizeof (tss) - 1);

        ptr.limit = (uint16_t) (sizeof (gdt) - 1U);
        ptr.base = (uint64_t) (uintptr_t) gdt;
        gdt_load (&ptr);

        __asm__ volatile ("ltr %w0" :: "r" ((uint16_t) 0x28));

        wrmsr (MSR_STAR, ((uint64_t) (GDT_USER_CS - 16U) << 48)
                        | ((uint64_t) GDT_KERNEL_CS << 32));
        wrmsr (MSR_LSTAR, (uint64_t) (uintptr_t) syscall_entry);
        wrmsr (MSR_SFMASK, 1ULL << 9);  /* clear IF */

        {
                uint64_t efer = rdmsr (MSR_EFER);

                efer |= 1ULL << 0;   /* SCE */
                efer &= ~(1ULL << 11); /* NXE off: user pages executable */
                wrmsr (MSR_EFER, efer);
        }

        {
                uint64_t cr0;
                uint64_t cr4;

                __asm__ volatile ("mov %%cr0, %0" : "=r" (cr0));
                cr0 &= ~(1ULL << 2);   /* EM off */
                cr0 |= (1ULL << 1);    /* MP on */
                __asm__ volatile ("mov %0, %%cr0" :: "r" (cr0));

                __asm__ volatile ("mov %%cr4, %0" : "=r" (cr4));
                cr4 |= (1ULL << 9) | (1ULL << 10); /* OSFXSR | OSXMMEXCPT */
                __asm__ volatile ("mov %0, %%cr4" :: "r" (cr4));
        }
}
