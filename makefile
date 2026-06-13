# Компиляторы и флаги
CC       = gcc
NASM     = nasm
LD       = ld

NASM_ELF = elf64
LD_EMU   = elf_x86_64

CFLAGS   = -ffreestanding -nostdlib -nostartfiles -nodefaultlibs \
        -Wall -Wextra -O2 -I core -m64 -mno-red-zone -mgeneral-regs-only \
        -fno-stack-protector -fno-pie -Icore/include
NASMFLAGS = -f $(NASM_ELF)

LDFLAGS  = -m $(LD_EMU) -T linker.ld

# Папки

CORE_DIR  = core
BUILD_DIR = build
ISO_BOOT  = iso/boot

# Поиск всех исходников (рекурсивно)
BOOT_ASM = boot/multiboot.asm
C_SRCS   := $(shell find $(CORE_DIR) -type f -name '*.c')
ASM_SRCS := $(shell find $(CORE_DIR) -type f -name '*.asm')

# Объектные файлы с сохранением структуры каталогов
BOOT_OBJ := $(BUILD_DIR)/boot/multiboot.o
C_OBJS   := $(patsubst $(CORE_DIR)/%.c, $(BUILD_DIR)/%.o, $(C_SRCS))
ASM_OBJS := $(patsubst $(CORE_DIR)/%.asm, $(BUILD_DIR)/%.o, $(ASM_SRCS))
OBJS     := $(BOOT_OBJ) $(C_OBJS) $(ASM_OBJS)

# Итоговое ядро
KERNEL   := $(ISO_BOOT)/kernel.bin

# Основная цель
all: $(KERNEL)

QEMU_MEM ?= 2048M

run: iso
	qemu-system-x86_64 -cdrom typeos.iso -m $(QEMU_MEM) \
		-device isa-debugcon,iobase=0xe9,chardev=debugcon \
		-chardev stdio,id=debugcon,mux=on,signal=off \
		-serial chardev:debugcon

iso: $(KERNEL)
	grub-mkrescue -o typeos.iso iso/

# Линковка ядра
$(KERNEL): $(OBJS) linker.ld | $(ISO_BOOT)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

# Правило для C-файлов
$(BUILD_DIR)/%.o: $(CORE_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Правило для ASM-файлов
$(BUILD_DIR)/%.o: $(CORE_DIR)/%.asm | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BOOT_OBJ): $(BOOT_ASM) | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(NASM) $(NASMFLAGS) $< -o $@

# Создание нужных папок
$(BUILD_DIR) $(ISO_BOOT):
	mkdir -p $@

# Очистка
clean:
	rm -rf $(BUILD_DIR) $(KERNEL)

# Автоматические зависимости для C (опционально)
DEPS = $(C_OBJS:.o=.d)
-include $(DEPS)
$(BUILD_DIR)/%.d: $(CORE_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) $< > $@

.PHONY: all clean
