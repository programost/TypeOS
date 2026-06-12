CC = gcc
CFLAGS = -m64 -ffreestanding -fno-builtin -nostdlib -mno-red-zone \
         -fno-stack-protector -Wall -Wextra -O2 -I. -Icore/include

LD = ld
# Убираем --oformat binary и добавляем работу через linker.ld
LDFLAGS = -m elf_x86_64 -nostdlib -T linker.ld

ASM = nasm
ASMFLAGS = -f elf64

OBJ = boot/multiboot.o core/idtasm.o core/idt.o core/drivers/vga.o core/drivers/keyboard.o core/kernel.o 

all: myos.iso

# Компиляция ассемблера
boot/multiboot.o: boot/multiboot.S
	$(ASM) $(ASMFLAGS) boot/multiboot.S -o boot/multiboot.o

core/idtasm.o: core/idtasm.S
	$(ASM) $(ASMFLAGS) core/idtasm.S -o core/idtasm.o

# Компиляция Си
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Сборка полноценного ELF64 бинарника
kernel.bin: $(OBJ)
	$(LD) $(LDFLAGS) $(OBJ) -o kernel.bin

# Сборка ISO
myos.iso: kernel.bin
	mkdir -p iso/boot/grub
	cp kernel.bin iso/boot/
	cp grub.cfg iso/boot/grub/
	grub-mkrescue -o myos.iso iso

clean:
	rm -f kernel.bin myos.iso
	rm -f core/*.o core/drivers/*.o boot/*.o
	rm -rf iso

run: myos.iso
	qemu-system-x86_64 -cdrom myos.iso

.PHONY: all clean run
