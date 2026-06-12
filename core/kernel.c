#include <types.h>
#include <vga.h>
#include <keyboard.h>
#include <idt.h>
#include <multiboot.h>
#include <paging.h>
#include <heap.h>
#include <string.h>
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

        struct vfs_stat st;
        struct vfs_file file;

        if (vfs_stat ("/init", &st) == 0) {
                enum { PREVIEW_MAX = 512 };
                char preview[PREVIEW_MAX];
                size_t to_read = st.size;

                kprintf ("/init: %u bytes\n", (unsigned) st.size);

                if (to_read >= PREVIEW_MAX) {
                        to_read = PREVIEW_MAX - 1;
                }

                if (vfs_open ("/init", &file) == 0) {
                        ssize_t bytes = vfs_read (&file, preview, to_read);

                        if (bytes > 0) {
                                preview[bytes] = '\0';
                                kprintf ("/init preview:\n%s\n", preview);
                        }

                        vfs_close (&file);
                }
        }

        void* test = kmalloc(128);
        if (test) {
                kprintf("Heap OK: kmalloc(128) = %p\n", test);
                kfree(test);
        } else {
                kprintf ("Heap exhausted: kmalloc(128) failed\n");
        }

        kprintf("Hello, World!\n");
        keyboard_init();
        idt_init();
        kprintf("Press a key...\n");
        int key = keyboard_getchar();
        kprintf("You pressed: %c\n", (char)key);

        char *str;
        for (;;) {
                kprintf("> ");
                str = get_string();
                if (strcmp(str, "exit") == 0) {
                        break;
                } else if (strcmp(str, "") == 0); 
                else {
                        kprintf("You entered: %s\n", str);
                }
        }

        while (1) {
                __asm__ volatile("hlt");
        }
}