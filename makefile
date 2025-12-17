# Compiler and Linker Configuration
CC = gcc
LD = ld
AS = nasm

# Compiler and Linker Flags
CFLAGS = -m32 -c -ffreestanding -Wall -g -O0 -I$(SRCDIR)/include
INTERRUPT_CFLAGS = $(CFLAGS) -mgeneral-regs-only
LDFLAGS = -m elf_i386 -T src/link.ld --allow-multiple-definition
ASFLAGS = -f elf32

# Colors
NO_COLOR=\033[0m
OK_COLOR=\033[32;01m
ERROR_COLOR=\033[31;01m
WARN_COLOR=\033[33;01m

# Directories
SRCDIR = src
OBJDIR = obj
OUTDIR = iso
GRUBDIR = $(OUTDIR)/boot/grub
GRUB_CFG = $(GRUBDIR)/grub.cfg
USER_SRCDIR = userspace
INITRD_DIR = initrd
INITRD_BIN_DIR = $(INITRD_DIR)/bin
INITRD_USR_BIN_DIR = $(INITRD_DIR)/usr/bin
INITRD = $(OUTDIR)/boot/initrd.tar
INITRD_FILES := $(shell find $(INITRD_DIR) -type f 2>/dev/null)
LIBC_DIR = libc
LIBC_INCLUDE_DIR = $(LIBC_DIR)/include
LIBC_INITRD_DIR = $(INITRD_DIR)/usr/libc

# Output Binary and ISO
OUTPUT = $(OUTDIR)/boot/kernel.bin
ISO_NAME := forest_nightly_$(shell date +%Y%m%d_%H%M%S).iso
ISO = $(ISO_NAME)

# Source Files
CSOURCES = $(filter-out $(SRCDIR)/interrupt.c $(SRCDIR)/interrupt_handlers.c, $(wildcard $(SRCDIR)/*.c))
ASMSOURCES = $(wildcard $(SRCDIR)/*.asm)

# Object Files
COBJECTS = $(CSOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
ASMOBJECTS = $(ASMSOURCES:$(SRCDIR)/%.asm=$(OBJDIR)/%.o)
INTERRUPT_OBJECTS = $(OBJDIR)/interrupt.o $(OBJDIR)/interrupt_handlers.o

# Userspace shell build
USER_LIBC_SRCS = $(wildcard userspace/libc/*.c)
USER_LIBC_OBJECTS = $(USER_LIBC_SRCS:userspace/libc/%.c=$(OBJDIR)/userlib_%.o)
USER_SUPPORT_OBJECTS = $(USER_LIBC_OBJECTS)

USER_APP_SRCS = $(wildcard $(USER_SRCDIR)/*.c)
USER_APPS = $(basename $(notdir $(USER_APP_SRCS)))
USER_APP_OBJECTS = $(USER_APPS:%=$(OBJDIR)/user_%.o)
USER_ELFS = $(USER_APPS:%=$(OBJDIR)/%.elf)
USER_PRIMARY_APP = shell
USER_PRIMARY_ELF = $(OBJDIR)/$(USER_PRIMARY_APP).elf
USER_ELF_BIN = $(OBJDIR)/$(USER_PRIMARY_APP)_elf.o
USER_APP_BINARIES = $(USER_APPS:%=$(INITRD_BIN_DIR)/%.elf)
# Keep linker address here for documentation (actual value in userspace/link.ld)
USER_LINK_ADDR = 0x40001000
USER_CFLAGS = -m32 -c -ffreestanding -nostdlib -Wall -g -O0 -I$(SRCDIR)/include -I$(LIBC_INCLUDE_DIR) -fno-pic -fno-pie -DUSERSPACE_BUILD

# Build Targets
.PHONY: all iso build run clean help

all: iso
	@echo "$(OK_COLOR)Build successful (kernel + ISO).$(NO_COLOR)"

iso: $(ISO)
	@echo "$(OK_COLOR)ISO ready: $(ISO)$(NO_COLOR)"

$(OUTPUT): $(COBJECTS) $(ASMOBJECTS) $(INTERRUPT_OBJECTS) $(USER_ELF_BIN)
	@mkdir -p $(GRUBDIR)
	@echo "$(OK_COLOR)Linking objects...$(NO_COLOR)"
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo "$(OK_COLOR)Kernel binary generated successfully.$(NO_COLOR)"

$(OBJDIR)/interrupt.o: $(SRCDIR)/interrupt.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling (interrupt) $<...$(NO_COLOR)"
	@$(CC) $(INTERRUPT_CFLAGS) -o $@ $<

$(OBJDIR)/interrupt_handlers.o: $(SRCDIR)/interrupt_handlers.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling (interrupt) $<...$(NO_COLOR)"
	@$(CC) $(INTERRUPT_CFLAGS) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.asm
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $<...$(NO_COLOR)"
	@$(AS) $(ASFLAGS) -o $@ $<

$(OBJDIR)/user_%.o: $(USER_SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling userspace $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(OBJDIR)/userlib_%.o: userspace/libc/%.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling userspace libc $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -I$(SRCDIR)/include -o $@ $<

$(USER_ELFS): $(USER_SUPPORT_OBJECTS)
$(OBJDIR)/%.elf: $(OBJDIR)/user_%.o $(USER_SUPPORT_OBJECTS) userspace/link.ld
	@echo "$(OK_COLOR)Linking userspace ELF $(@F)...$(NO_COLOR)"
	@$(LD) -m elf_i386 -T userspace/link.ld -nostdlib -o $@ $(filter-out userspace/link.ld,$^)

$(USER_ELF_BIN): $(USER_PRIMARY_ELF)
	@echo "$(OK_COLOR)Embedding $(USER_PRIMARY_APP) ELF into kernel...$(NO_COLOR)"
	@$(LD) -m elf_i386 -r -b binary -o $@ $<

.PHONY: refresh-libc
refresh-libc:
	@echo "$(OK_COLOR)Refreshing exported libc sources...$(NO_COLOR)"
	@mkdir -p $(LIBC_INCLUDE_DIR)/libc
	@rm -f $(LIBC_DIR)/*.c
	@rm -f $(LIBC_INCLUDE_DIR)/*.h
	@rm -rf $(LIBC_INCLUDE_DIR)/libc
	@mkdir -p $(LIBC_INCLUDE_DIR)/libc
	@cp $(SRCDIR)/string.c $(SRCDIR)/util.c $(SRCDIR)/system.c $(LIBC_DIR)/
	@cp $(SRCDIR)/include/types.h $(SRCDIR)/include/util.h $(SRCDIR)/include/string.h $(SRCDIR)/include/system.h $(LIBC_INCLUDE_DIR)/
	@cp $(SRCDIR)/include/libc/*.h $(LIBC_INCLUDE_DIR)/libc/

$(INITRD): refresh-libc $(OUTPUT) $(INITRD_FILES) $(USER_APP_BINARIES)
	@mkdir -p $(OUTDIR)/boot
	@echo "$(OK_COLOR)Copying libc into initrd for developers...$(NO_COLOR)"
	@rm -rf $(LIBC_INITRD_DIR)
	@mkdir -p $(LIBC_INITRD_DIR)
	@cp -r $(LIBC_DIR)/. $(LIBC_INITRD_DIR)/
	@echo "$(OK_COLOR)Building initrd tar (ustar)...$(NO_COLOR)"
	@tar --format=ustar --exclude='.gitkeep' -cf $@ -C $(INITRD_DIR) .

$(INITRD): $(USER_APP_BINARIES)

$(INITRD_BIN_DIR)/%.elf: $(OBJDIR)/%.elf
	@mkdir -p $(INITRD_BIN_DIR) $(INITRD_USR_BIN_DIR)
	@echo "$(OK_COLOR)Installing $(@F) into initrd /bin and /usr/bin...$(NO_COLOR)"
	@cp $< $(INITRD_BIN_DIR)/$*.elf
	@cp $< $(INITRD_USR_BIN_DIR)/$*.elf

$(GRUB_CFG): Grub/grub.cfg
	@mkdir -p $(GRUBDIR)
	@echo "$(OK_COLOR)Copying GRUB config...$(NO_COLOR)"
	@cp $< $@

$(ISO): $(OUTPUT) $(GRUB_CFG) $(INITRD)
	@echo "$(OK_COLOR)Building ISO image...$(NO_COLOR)"
	@grub-mkrescue -o $(ISO) $(OUTDIR)

# Run the Kernel in QEMU
run: $(ISO)
	@echo "$(OK_COLOR)Running kernel in QEMU...$(NO_COLOR)"
	@qemu-system-i386 -cdrom $(ISO) -serial stdio -no-shutdown

# Build ISO Image
build: $(ISO)
	@echo "$(OK_COLOR)ISO image built successfully.$(NO_COLOR)"

# Clean Up
clean:
	@echo "$(OK_COLOR)Cleaning up...$(NO_COLOR)"
	@rm -rf $(OBJDIR) $(OUTDIR)
	@rm -f *.iso iso.iso
	@echo "$(OK_COLOR)Clean up complete.$(NO_COLOR)"

# Help Message
help:
	@echo "Usage: make [all|run|build|clean|help]"
	@echo "  all:    Build the kernel binary"
	@echo "  run:    Run the kernel in QEMU"
	@echo "  build:  Build the ISO image"
	@echo "  clean:  Clean up the build files"
	@echo "  help:   Display this help message"
