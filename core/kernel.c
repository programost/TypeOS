#include <types.h>
#include <vga.h>
#include <keyboard.h>
#include <idt.h>
#include <multiboot.h>
#include <paging.h>
#include <heap.h>
#include <string.h>
#include <gdt.h>
#include <proc.h>
#include <sched.h>
#include <fs/initramfs.h>
#include <fs/vfs.h>

void kmain(uint64_t magic, struct multiboot_info* mb2_info) {
        vga_init();

        if (magic != MULTIBOOT2_BOOTLOADER_MAGIC) {
                kprintf ("Invalid multiboot magic: 0x%x\n", (uint32_t) magic);
        } else {
                kprintf ("Multiboot2 OK, info @ %p\n", (void *) mb2_info);
                multiboot_print_info (mb2_info);
        }

        paging_init (mb2_info);
        heap_init();
        initramfs_load (mb2_info);

        gdt_init();
        idt_init();
        proc_init();
        sched_init();

        static const char *envp[] = {
                "HOME=/",
                "PATH=/bin:/sbin:/usr/bin",
                "TERM=linux",
                NULL
        };

        kprintf ("Starting /bin/sh...\n");

        vfs_mkdir ("/proc", 0755U);
        vfs_mkdir ("/sys", 0755U);

        static const char *argv[] = { "/bin/sh", NULL };

        if (proc_exec ("/bin/sh", argv, envp) != 0) {
                kprintf ("sh failed, trying /linuxrc\n");
                static const char *linuxrc_argv[] = { "/linuxrc", NULL };

                if (proc_exec ("/linuxrc", linuxrc_argv, envp) != 0) {
                        kprintf ("Could not start sh or linuxrc\n");
                }
        }

        for (;;) {
                __asm__ volatile ("hlt");
        }
}
