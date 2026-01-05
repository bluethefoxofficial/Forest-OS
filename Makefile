# =============================================================================
# FOREST OS ADVANCED MULTIARCH BUILD SYSTEM
# =============================================================================
# Supports:
# - 32-bit and 64-bit architectures
# - BIOS and UEFI boot modes  
# - Debug and Release builds
# - Cross-compilation toolchain detection
# - Distribution packaging
# - Advanced optimization levels
# =============================================================================

.DEFAULT_GOAL := help

# =============================================================================
# BUILD CONFIGURATION
# =============================================================================

# Architecture Configuration (32 or 64)
ARCH ?= 32
VALID_ARCHS := 32 64

# Boot Mode Configuration (bios or uefi)
BOOT_MODE ?= bios
VALID_BOOT_MODES := bios uefi

# Build Type Configuration (debug, release, optimize)
BUILD_TYPE ?= debug
VALID_BUILD_TYPES := debug release optimize

# Target Configuration
ifeq ($(ARCH),32)
    TARGET_ARCH := i686
    TARGET_TUPLE := i686-forestos
    ARCH_FLAGS := -m32 -march=i386 -mtune=i386
    ARCH_LDFLAGS := -m elf_i386
    EFI_ARCH := i386
else ifeq ($(ARCH),64)
    TARGET_ARCH := x86_64
    TARGET_TUPLE := x86_64-forestos
    ARCH_FLAGS := -m64 -march=x86-64 -mcmodel=kernel
    ARCH_LDFLAGS := -m elf_x86_64
    EFI_ARCH := x86_64
endif

# Validation
ifeq ($(filter $(ARCH),$(VALID_ARCHS)),)
$(error Invalid ARCH=$(ARCH). Valid values: $(VALID_ARCHS))
endif

ifeq ($(filter $(BOOT_MODE),$(VALID_BOOT_MODES)),)
$(error Invalid BOOT_MODE=$(BOOT_MODE). Valid values: $(VALID_BOOT_MODES))
endif

ifeq ($(filter $(BUILD_TYPE),$(VALID_BUILD_TYPES)),)
$(error Invalid BUILD_TYPE=$(BUILD_TYPE). Valid values: $(VALID_BUILD_TYPES))
endif

# =============================================================================
# DIRECTORY STRUCTURE  
# =============================================================================

REPO_ROOT := $(abspath $(CURDIR))
SRCDIR := src
OBJDIR := obj/$(ARCH)bit-$(BOOT_MODE)-$(BUILD_TYPE)
OUTDIR := build/$(ARCH)bit-$(BOOT_MODE)-$(BUILD_TYPE)
DISTDIR := dist
TOOLSDIR := tools

# Source directories
USER_SRCDIR := userspace
INITRD_DIR := initrd
LIBC_DIR := libs/libc
FORESTCORE_DIR := libs/forestcore
UACPI_SRCDIR := libs/uacpi/source

# Output directories  
GRUBDIR := $(OUTDIR)/boot/grub
EFIDIR := $(OUTDIR)/EFI/BOOT
INITRD_BIN_DIR := $(INITRD_DIR)/bin
INITRD_USR_BIN_DIR := $(INITRD_DIR)/usr/bin

# =============================================================================
# TOOLCHAIN CONFIGURATION
# =============================================================================

# Primary toolchain (defaults to host toolchain, override via environment if needed)
CC ?= gcc
LD ?= ld
AS := nasm
OBJCOPY ?= objcopy
STRIP ?= strip

# UEFI tools (when needed)
ifeq ($(BOOT_MODE),uefi)
    GENFW := GenFw
    SPLIT := split
endif

# =============================================================================
# BUILD FLAGS CONFIGURATION
# =============================================================================

# Common flags
COMMON_CFLAGS := $(ARCH_FLAGS) -ffreestanding -nostdlib -fno-pic -fno-pie \
                 -Wall -Wextra -I$(SRCDIR)/include -Ilibs/uacpi/include \
                 -mno-sse -mno-sse2 -mno-mmx -mno-3dnow -fcf-protection=none

# Architecture-specific flags
ifeq ($(ARCH),32)
    COMMON_CFLAGS += -mno-red-zone
else ifeq ($(ARCH),64)
    COMMON_CFLAGS += -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse3
endif

# Boot mode specific flags
ifeq ($(BOOT_MODE),uefi)
    COMMON_CFLAGS += -DUEFI_BOOT -fshort-wchar
else
    COMMON_CFLAGS += -DBIOS_BOOT
endif

# Build type specific flags
ifeq ($(BUILD_TYPE),debug)
    CFLAGS := $(COMMON_CFLAGS) -g -O0 -DDEBUG -fno-omit-frame-pointer
    LDFLAGS := $(ARCH_LDFLAGS) -g
else ifeq ($(BUILD_TYPE),release)
    # Temporarily build release with debug-friendly opts to avoid toolchain issues
    CFLAGS := $(COMMON_CFLAGS) -O0 -DRELEASE -fno-omit-frame-pointer -ffunction-sections -fdata-sections
    LDFLAGS := $(ARCH_LDFLAGS) -O0 --gc-sections -s
else ifeq ($(BUILD_TYPE),optimize)
    CFLAGS := $(COMMON_CFLAGS) -O3 -DOPTIMIZE -fomit-frame-pointer -ffunction-sections \
              -fdata-sections -flto -march=native -mtune=native
    LDFLAGS := $(ARCH_LDFLAGS) -O3 --gc-sections -s -flto
endif

# Special interrupt handling flags
INTERRUPT_CFLAGS := $(CFLAGS) -mgeneral-regs-only

# Linker script selection
ifeq ($(BOOT_MODE),uefi)
    LINKER_SCRIPT := src/link_uefi_$(ARCH).ld
else ifeq ($(ARCH),64)
    LINKER_SCRIPT := src/link64.ld
else
    LINKER_SCRIPT := src/link.ld
endif

# Boot assembly selection
ifeq ($(ARCH),64)
    BOOT_ASM := src/boot64.asm
else
    BOOT_ASM := src/boot.asm
endif

LDFLAGS += -T $(LINKER_SCRIPT) --allow-multiple-definition

# Assembly flags (NASM format)
ifeq ($(ARCH),32)
    ASFLAGS := -f elf32 -D__i386__
else
    ASFLAGS := -f elf64 -D__x86_64__
endif

# =============================================================================
# COLOR OUTPUT
# =============================================================================

NO_COLOR := \033[0m
OK_COLOR := \033[32;01m
ERROR_COLOR := \033[31;01m
WARN_COLOR := \033[33;01m
INFO_COLOR := \033[36;01m

# =============================================================================
# SOURCE AND OBJECT FILES
# =============================================================================

# Kernel sources
EXCLUDED_CSOURCES := \
    $(wildcard $(SRCDIR)/interrupt_*.c) \
    $(SRCDIR)/acpi_interrupt_routing.c \
    $(SRCDIR)/fault_prevention.c \
    $(SRCDIR)/ipi_smp_coordination.c \
    $(SRCDIR)/irq_management.c \
    $(SRCDIR)/msi_support.c \
    $(SRCDIR)/smp_interrupt_distribution.c \
    $(SRCDIR)/spurious_interrupt.c \
    $(SRCDIR)/watchdog_interrupt_support.c \
    $(SRCDIR)/interrupt_driven_io.c

EXCLUDED_CSOURCES := $(filter-out $(SRCDIR)/interrupt.c $(SRCDIR)/interrupt_handlers.c $(SRCDIR)/interrupt_utils.c,$(EXCLUDED_CSOURCES))

CSOURCES := $(filter-out $(EXCLUDED_CSOURCES), $(wildcard $(SRCDIR)/*.c))
GRAPHICS_CSOURCES := $(wildcard $(SRCDIR)/graphics/*.c $(SRCDIR)/graphics/drivers/*.c)
PANICUI_CSOURCES := $(wildcard $(SRCDIR)/panicui*.c)
# Exclude boot files from wildcard, add correct one based on BOOT_ASM variable
ASMSOURCES_RAW := $(wildcard $(SRCDIR)/*.asm $(SRCDIR)/*.s)
ASMSOURCES := $(filter-out $(SRCDIR)/boot.asm $(SRCDIR)/boot64.asm,$(ASMSOURCES_RAW)) $(BOOT_ASM)
UACPI_CSOURCES := $(wildcard $(UACPI_SRCDIR)/*.c)

# Boot mode specific sources
ifeq ($(BOOT_MODE),uefi)
    BOOT_SOURCES := $(wildcard $(SRCDIR)/uefi/*.c)
else
    BOOT_SOURCES := $(wildcard $(SRCDIR)/bios/*.c)
endif

# Object files
COBJECTS := $(CSOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
GRAPHICS_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(GRAPHICS_CSOURCES))
PANICUI_OBJECTS := $(PANICUI_CSOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)
BOOT_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(BOOT_SOURCES))
UACPI_OBJECTS := $(UACPI_CSOURCES:$(UACPI_SRCDIR)/%.c=$(OBJDIR)/uacpi_%.o)
ASMOBJECTS := $(patsubst $(SRCDIR)/%.asm,$(OBJDIR)/%.o,$(filter %.asm,$(ASMSOURCES))) \
              $(patsubst $(SRCDIR)/%.s,$(OBJDIR)/%.o,$(filter %.s,$(ASMSOURCES)))
INTERRUPT_OBJECTS := $(OBJDIR)/interrupt.o $(OBJDIR)/interrupt_handlers.o

ALL_OBJECTS := $(COBJECTS) $(GRAPHICS_OBJECTS) $(PANICUI_OBJECTS) $(BOOT_OBJECTS) \
               $(ASMOBJECTS) $(INTERRUPT_OBJECTS) $(UACPI_OBJECTS)

# Userspace configuration
USER_OBJDIR := $(OBJDIR)/userspace
USER_LIBC_SRCS := $(wildcard userspace/libc/*.c)
USER_LIBC_OBJECTS := $(USER_LIBC_SRCS:userspace/libc/%.c=$(USER_OBJDIR)/libc_%.o)
USER_SUPPORT_OBJECTS := $(USER_LIBC_OBJECTS)

USER_APP_SRCS := $(filter-out userspace/crt0.c,$(wildcard $(USER_SRCDIR)/*.c))
USER_APPS := $(basename $(notdir $(USER_APP_SRCS)))
USER_APP_OBJECTS := $(USER_APPS:%=$(USER_OBJDIR)/%.o)
USER_ELFS := $(USER_APPS:%=$(USER_OBJDIR)/%.elf)
USER_PRIMARY_APP := shell
USER_PRIMARY_ELF := $(USER_OBJDIR)/$(USER_PRIMARY_APP).elf
USER_ELF_BIN := $(OBJDIR)/$(USER_PRIMARY_APP)_elf.o
USER_APP_BINARIES := $(USER_APPS:%=$(INITRD_BIN_DIR)/%.elf)

USER_CFLAGS := $(ARCH_FLAGS) -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
               -Wall -Wextra -g -O0 \
               -I$(SRCDIR)/include -I$(LIBC_DIR)/include \
               -fno-pic -fno-pie -DUSERSPACE_BUILD -mno-sse -mno-sse2 -mno-mmx -mno-3dnow
USER_LDFLAGS := $(ARCH_LDFLAGS) -nostdlib -T userspace/link.ld

# =============================================================================
# OUTPUT FILES
# =============================================================================

# Kernel binary
ifeq ($(BOOT_MODE),uefi)
    OUTPUT_ELF := $(OUTDIR)/kernel.elf
    OUTPUT := $(OUTDIR)/BOOTX64.EFI
else
    OUTPUT := $(OUTDIR)/boot/kernel.bin
endif

# Boot files
GRUB_CFG := $(GRUBDIR)/grub.cfg
INITRD := $(OUTDIR)/boot/initrd.tar
INITRD_FILES := $(shell find $(INITRD_DIR) -type f 2>/dev/null)

# Distribution files
ISO_NAME := forestos_$(ARCH)bit_$(BOOT_MODE)_$(BUILD_TYPE)_$(shell date +%Y%m%d_%H%M%S).iso
ISO := $(DISTDIR)/$(ISO_NAME)
IMG_NAME := forestos_$(ARCH)bit_$(BOOT_MODE)_$(BUILD_TYPE)_$(shell date +%Y%m%d_%H%M%S).img
IMG := $(DISTDIR)/$(IMG_NAME)

# =============================================================================
# MAIN TARGETS
# =============================================================================

.PHONY: help all build iso img dist clean show-config

help:
	@echo "$(INFO_COLOR)Forest OS Advanced Build System$(NO_COLOR)"
	@echo ""
	@echo "$(OK_COLOR)Configuration:$(NO_COLOR)"
	@echo "  ARCH=$(ARCH) BOOT_MODE=$(BOOT_MODE) BUILD_TYPE=$(BUILD_TYPE)"
	@echo ""
	@echo "$(OK_COLOR)Main Targets:$(NO_COLOR)"
	@echo "  help        Show this help message"
	@echo "  all         Build kernel and create bootable image"  
	@echo "  build       Build kernel binary only"
	@echo "  iso         Create ISO image (BIOS mode)"
	@echo "  img         Create disk image (UEFI mode)"
	@echo "  dist        Create distribution package"
	@echo "  clean       Clean build files"
	@echo ""
	@echo "$(OK_COLOR)Architecture Targets:$(NO_COLOR)"
	@echo "  build32     Build 32-bit version"
	@echo "  build64     Build 64-bit version"
	@echo "  buildall    Build all architecture combinations"
	@echo ""
	@echo "$(OK_COLOR)Configuration Variables:$(NO_COLOR)"
	@echo "  ARCH=<32|64>              Target architecture (default: 32)"
	@echo "  BOOT_MODE=<bios|uefi>     Boot mode (default: bios)"
	@echo "  BUILD_TYPE=<debug|release|optimize>  Build type (default: debug)"
	@echo ""
	@echo "$(OK_COLOR)Examples:$(NO_COLOR)"
	@echo "  make ARCH=64 BOOT_MODE=uefi BUILD_TYPE=release"
	@echo "  make build32"
	@echo "  make dist"

all: ensure-toolchain show-config 
	@$(MAKE) build
ifeq ($(BOOT_MODE),uefi)
	@$(MAKE) img
else
	@$(MAKE) iso
endif

build: ensure-toolchain $(OUTPUT)

show-config:
	@echo "$(INFO_COLOR)Build Configuration:$(NO_COLOR)"
	@echo "  Architecture: $(ARCH)-bit ($(TARGET_ARCH))"
	@echo "  Boot Mode: $(BOOT_MODE)"
	@echo "  Build Type: $(BUILD_TYPE)"
	@echo "  Target: $(TARGET_TUPLE)"
	@echo "  Output: $(OUTPUT)"
	@echo ""

# =============================================================================
# TOOLCHAIN VALIDATION
# =============================================================================

.PHONY: ensure-toolchain
ensure-toolchain:
	@if ! command -v $(CC) >/dev/null 2>&1; then \
		echo "$(ERROR_COLOR)Required compiler '$(CC)' not found in PATH.$(NO_COLOR)"; \
		exit 1; \
	fi
	@echo "$(OK_COLOR)Using compiler: $(CC)$(NO_COLOR)"

# =============================================================================
# KERNEL BUILD RULES
# =============================================================================

# UEFI kernel build
ifeq ($(BOOT_MODE),uefi)
$(OUTPUT): $(OUTPUT_ELF)
	@mkdir -p $(EFIDIR)
	@echo "$(OK_COLOR)Converting ELF to UEFI PE32+ binary...$(NO_COLOR)"
	@$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel* \
	            -j .rela* -j .reloc -j .bss -j .stack --target=efi-app-$(EFI_ARCH) $< $@
	@echo "$(OK_COLOR)UEFI kernel binary generated: $@$(NO_COLOR)"

$(OUTPUT_ELF): $(ALL_OBJECTS) $(USER_ELF_BIN)
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Linking UEFI ELF binary...$(NO_COLOR)"
	@$(LD) $(LDFLAGS) -o $@ $^

# BIOS kernel build  
else
$(OUTPUT): $(ALL_OBJECTS) $(USER_ELF_BIN)
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Linking BIOS kernel binary...$(NO_COLOR)"
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo "$(OK_COLOR)Kernel binary generated: $@$(NO_COLOR)"
endif

# =============================================================================
# OBJECT FILE BUILD RULES
# =============================================================================

# Special interrupt handling compilation
$(OBJDIR)/interrupt.o: $(SRCDIR)/interrupt.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling (interrupt) $<...$(NO_COLOR)"
	@$(CC) $(INTERRUPT_CFLAGS) -c -o $@ $<

$(OBJDIR)/interrupt_handlers.o: $(SRCDIR)/interrupt_handlers.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling (interrupt) $<...$(NO_COLOR)"
	@$(CC) $(INTERRUPT_CFLAGS) -c -o $@ $<

# uACPI compilation
$(OBJDIR)/uacpi_%.o: $(UACPI_SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling (uACPI) $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Standard C compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Compiling $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Assembly compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.asm
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $<...$(NO_COLOR)"
	@$(AS) $(ASFLAGS) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.s
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $<...$(NO_COLOR)"
	@$(AS) $(ASFLAGS) -o $@ $<

# Graphics subdirectory compilation
$(OBJDIR)/graphics/%.o: $(SRCDIR)/graphics/%.c
	@mkdir -p $(OBJDIR)/graphics
	@echo "$(OK_COLOR)Compiling graphics $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/graphics/drivers/%.o: $(SRCDIR)/graphics/drivers/%.c
	@mkdir -p $(OBJDIR)/graphics/drivers
	@echo "$(OK_COLOR)Compiling graphics driver $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Boot mode specific compilation
$(OBJDIR)/uefi/%.o: $(SRCDIR)/uefi/%.c
	@mkdir -p $(OBJDIR)/uefi
	@echo "$(OK_COLOR)Compiling UEFI $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/bios/%.o: $(SRCDIR)/bios/%.c
	@mkdir -p $(OBJDIR)/bios
	@echo "$(OK_COLOR)Compiling BIOS $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# =============================================================================
# USERSPACE BUILD RULES
# =============================================================================

$(USER_OBJDIR)/%.o: $(USER_SRCDIR)/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling userspace $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/libc_%.o: userspace/libc/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling userspace libc $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -I$(SRCDIR)/include -o $@ $<

$(USER_OBJDIR)/crt0.o: userspace/crt0.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling userspace crt0...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -I$(SRCDIR)/include -o $@ $<

$(USER_ELFS): $(USER_SUPPORT_OBJECTS) $(USER_OBJDIR)/crt0.o
$(USER_OBJDIR)/%.elf: $(USER_OBJDIR)/%.o $(USER_SUPPORT_OBJECTS) $(USER_OBJDIR)/crt0.o userspace/link.ld
	@echo "$(OK_COLOR)Linking userspace ELF $(@F)...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/$*.o $(USER_SUPPORT_OBJECTS)

$(USER_ELF_BIN): $(USER_PRIMARY_ELF)
	@echo "$(OK_COLOR)Embedding $(USER_PRIMARY_APP) ELF into kernel...$(NO_COLOR)"
	@$(LD) $(ARCH_LDFLAGS) -r -b binary -o $@ $<

# =============================================================================
# INITRD AND LIBRARY BUILD RULES
# =============================================================================

.PHONY: refresh-libc refresh-forestcore

refresh-libc: refresh-forestcore
	@echo "$(OK_COLOR)Refreshing exported libc sources...$(NO_COLOR)"
	@rm -rf $(LIBC_DIR)/include/libc
	@mkdir -p $(LIBC_DIR)/include/libc
	@cp -r $(SRCDIR)/include/libc/* $(LIBC_DIR)/include/libc/

refresh-forestcore:
	@echo "$(OK_COLOR)Refreshing ForestCore runtime exports...$(NO_COLOR)"
	@mkdir -p $(FORESTCORE_DIR)/src $(FORESTCORE_DIR)/include
	@rm -f $(FORESTCORE_DIR)/src/*.c $(FORESTCORE_DIR)/include/*.h
	@cp $(SRCDIR)/string.c $(SRCDIR)/util.c $(SRCDIR)/system.c $(SRCDIR)/audio.c $(FORESTCORE_DIR)/src/
	@cp $(SRCDIR)/include/types.h $(SRCDIR)/include/util.h $(SRCDIR)/include/string.h \
	    $(SRCDIR)/include/system.h $(SRCDIR)/include/net.h $(SRCDIR)/include/driver.h $(FORESTCORE_DIR)/include/

$(INITRD): refresh-libc $(OUTPUT) $(INITRD_FILES) $(USER_APP_BINARIES)
	@mkdir -p $(OUTDIR)/boot
	@echo "$(OK_COLOR)Copying libc into initrd...$(NO_COLOR)"
	@rm -rf $(INITRD_DIR)/usr/libc
	@mkdir -p $(INITRD_DIR)/usr/libc
	@cp -r $(LIBC_DIR)/. $(INITRD_DIR)/usr/libc/
	@echo "$(OK_COLOR)Building initrd tar archive...$(NO_COLOR)"
	@tar --format=ustar --exclude='.gitkeep' -cf $@ -C $(INITRD_DIR) .

$(INITRD_BIN_DIR)/%.elf: $(USER_OBJDIR)/%.elf
	@mkdir -p $(INITRD_BIN_DIR) $(INITRD_USR_BIN_DIR)
	@echo "$(OK_COLOR)Installing $(@F) into initrd...$(NO_COLOR)"
	@cp $< $(INITRD_BIN_DIR)/$*.elf
	@cp $< $(INITRD_USR_BIN_DIR)/$*.elf

# =============================================================================
# BOOT IMAGE CREATION
# =============================================================================

# BIOS ISO creation
iso: ensure-toolchain $(ISO)

$(GRUB_CFG): Grub/grub.cfg
	@mkdir -p $(GRUBDIR)
	@echo "$(OK_COLOR)Copying GRUB config...$(NO_COLOR)"
	@cp $< $@

$(ISO): $(OUTPUT) $(GRUB_CFG) $(INITRD)
	@mkdir -p $(DISTDIR)
	@echo "$(OK_COLOR)Building BIOS ISO image...$(NO_COLOR)"
	@grub-mkrescue -o $@ $(OUTDIR)
	@echo "$(OK_COLOR)ISO created: $@$(NO_COLOR)"

# UEFI disk image creation
img: ensure-toolchain $(IMG)

$(IMG): $(OUTPUT) $(INITRD)
	@mkdir -p $(DISTDIR)
	@echo "$(OK_COLOR)Creating UEFI disk image...$(NO_COLOR)"
	@dd if=/dev/zero of=$@ bs=1M count=64 2>/dev/null
	@# Create minimal GPT using available tools (try sfdisk, fdisk, or skip partitioning)
	@if command -v sfdisk >/dev/null 2>&1; then \
		printf 'label: gpt\nstart=2048, size=126976, type=C12A7328-F81F-11D2-BA4B-00A0C93EC93B\n' | sfdisk -q $@; \
	elif command -v parted >/dev/null 2>&1; then \
		parted -s $@ mklabel gpt && parted -s $@ mkpart primary fat32 1MiB 63MiB && parted -s $@ set 1 esp on; \
	elif command -v sgdisk >/dev/null 2>&1; then \
		sgdisk -n 1:2048:129023 -t 1:EF00 $@; \
	else \
		echo "$(WARN_COLOR)Warning: No partitioning tool found (sfdisk/parted/sgdisk). Creating unpartitioned image.$(NO_COLOR)"; \
	fi
	@echo "$(OK_COLOR)UEFI disk image created: $@$(NO_COLOR)"

# =============================================================================
# MULTI-ARCHITECTURE BUILD TARGETS
# =============================================================================

.PHONY: build32 build64 build32-bios build32-uefi build64-bios build64-uefi buildall

build32:
	@$(MAKE) ARCH=32 all

build64:
	@$(MAKE) ARCH=64 all

build32-bios:
	@$(MAKE) ARCH=32 BOOT_MODE=bios all

build32-uefi:
	@$(MAKE) ARCH=32 BOOT_MODE=uefi all

build64-bios:
	@$(MAKE) ARCH=64 BOOT_MODE=bios all

build64-uefi:
	@$(MAKE) ARCH=64 BOOT_MODE=uefi all

buildall: build32-bios build32-uefi build64-bios build64-uefi
	@echo "$(OK_COLOR)All architecture combinations built successfully!$(NO_COLOR)"

# =============================================================================
# DISTRIBUTION PACKAGING
# =============================================================================

.PHONY: dist dist-clean

dist: buildall
	@echo "$(INFO_COLOR)Creating distribution package...$(NO_COLOR)"
	@mkdir -p $(DISTDIR)/forestos-complete/
	@echo "$(OK_COLOR)Copying binaries...$(NO_COLOR)"
	@cp -r build/* $(DISTDIR)/forestos-complete/
	@echo "$(OK_COLOR)Creating distribution archive...$(NO_COLOR)"
	@cd $(DISTDIR) && tar -czf forestos-complete-$(shell date +%Y%m%d_%H%M%S).tar.gz forestos-complete/
	@echo "$(OK_COLOR)Distribution package created in $(DISTDIR)/$(NO_COLOR)"

dist-clean:
	@echo "$(OK_COLOR)Cleaning distribution files...$(NO_COLOR)"
	@rm -rf $(DISTDIR)

# =============================================================================
# TESTING AND DEBUGGING
# =============================================================================

.PHONY: run run-bios run-uefi debug test

run: run-bios

run-bios: ensure-toolchain
	@$(MAKE) ARCH=$(ARCH) BOOT_MODE=bios BUILD_TYPE=$(BUILD_TYPE) iso
	@echo "$(OK_COLOR)Running $(ARCH)-bit BIOS kernel in QEMU...$(NO_COLOR)"
	@qemu-system-$(TARGET_ARCH) -cdrom $(DISTDIR)/forestos_$(ARCH)bit_bios_$(BUILD_TYPE)_*.iso \
	                            -serial stdio -no-shutdown -enable-kvm 2>/dev/null || \
	 qemu-system-$(TARGET_ARCH) -cdrom $(DISTDIR)/forestos_$(ARCH)bit_bios_$(BUILD_TYPE)_*.iso \
	                            -serial stdio -no-shutdown

run-uefi: ensure-toolchain
	@$(MAKE) ARCH=$(ARCH) BOOT_MODE=uefi BUILD_TYPE=$(BUILD_TYPE) img
	@echo "$(OK_COLOR)Running $(ARCH)-bit UEFI kernel in QEMU...$(NO_COLOR)"
	@qemu-system-$(TARGET_ARCH) -drive file=$(DISTDIR)/forestos_$(ARCH)bit_uefi_$(BUILD_TYPE)_*.img,if=ide \
	                            -bios /usr/share/ovmf/OVMF.fd -serial stdio -no-shutdown

debug: ensure-toolchain
	@$(MAKE) ARCH=$(ARCH) BOOT_MODE=$(BOOT_MODE) BUILD_TYPE=debug iso
	@echo "$(OK_COLOR)Running kernel in debug mode...$(NO_COLOR)"
	@qemu-system-$(TARGET_ARCH) -cdrom $(DISTDIR)/forestos_$(ARCH)bit_$(BOOT_MODE)_debug_*.iso \
	                            -serial stdio -no-shutdown -s -S

# =============================================================================
# CLEANUP TARGETS
# =============================================================================

.PHONY: clean clean-all clean-kernel clean-userspace

clean:
	@echo "$(OK_COLOR)Cleaning build files for $(ARCH)-bit $(BOOT_MODE) $(BUILD_TYPE)...$(NO_COLOR)"
	@rm -rf $(OBJDIR) $(OUTDIR)

clean-all:
	@echo "$(OK_COLOR)Cleaning all build files...$(NO_COLOR)"
	@rm -rf obj/ build/ *.iso
	@rm -rf $(DISTDIR)

clean-kernel:
	@echo "$(OK_COLOR)Cleaning kernel objects...$(NO_COLOR)"
	@find $(OBJDIR) -name "*.o" -not -path "$(USER_OBJDIR)/*" -delete 2>/dev/null || true

clean-userspace:
	@echo "$(OK_COLOR)Cleaning userspace objects...$(NO_COLOR)"
	@rm -rf $(USER_OBJDIR)
	@rm -f $(USER_APP_BINARIES)

# =============================================================================
# UTILITY TARGETS
# =============================================================================

.PHONY: info list-objects size stats

info:
	@echo "$(INFO_COLOR)Forest OS Build System Information$(NO_COLOR)"
	@echo "Repository: $(REPO_ROOT)"
	@echo "Toolchain: $(FORESTOS_TOOLCHAIN_PREFIX)"
	@echo "Sysroot: $(FORESTOS_SYSROOT)"
	@echo "Target: $(TARGET_TUPLE)"
	@echo "Objects: $(OBJDIR)"
	@echo "Output: $(OUTDIR)"

list-objects:
	@echo "$(INFO_COLOR)Object files that will be built:$(NO_COLOR)"
	@echo "$(ALL_OBJECTS)" | tr ' ' '\n' | sort

size: $(OUTPUT)
	@echo "$(INFO_COLOR)Binary size information:$(NO_COLOR)"
	@ls -lh $(OUTPUT)
	@echo ""
	@$(STRIP) --version >/dev/null 2>&1 && \
		echo "Stripped size:" && \
		$(STRIP) -s $(OUTPUT) -o $(OUTPUT).stripped && \
		ls -lh $(OUTPUT).stripped && \
		rm -f $(OUTPUT).stripped || true

stats:
	@echo "$(INFO_COLOR)Build statistics:$(NO_COLOR)"
	@echo "C source files: $(words $(CSOURCES) $(GRAPHICS_CSOURCES) $(PANICUI_CSOURCES))"
	@echo "Assembly files: $(words $(ASMSOURCES))"
	@echo "Object files: $(words $(ALL_OBJECTS))"
	@echo "Userspace apps: $(words $(USER_APPS))"

# =============================================================================
# HELP AND DOCUMENTATION
# =============================================================================

.PHONY: help-advanced

help-advanced:
	@echo "$(INFO_COLOR)Forest OS Advanced Build System - Detailed Help$(NO_COLOR)"
	@echo ""
	@echo "$(OK_COLOR)Build System Features:$(NO_COLOR)"
	@echo "  • Multi-architecture support (32-bit and 64-bit)"
	@echo "  • BIOS and UEFI boot mode support"
	@echo "  • Multiple build types (debug, release, optimize)"
	@echo "  • Cross-compilation toolchain integration"
	@echo "  • Distribution packaging"
	@echo "  • Advanced optimization options"
	@echo ""
	@echo "$(OK_COLOR)Directory Structure:$(NO_COLOR)"
	@echo "  obj/           - Object files (per configuration)"
	@echo "  build/         - Built kernels (per configuration)"
	@echo "  dist/          - Distribution packages"
	@echo ""
	@echo "$(OK_COLOR)Configuration Examples:$(NO_COLOR)"
	@echo "  make ARCH=32 BOOT_MODE=bios BUILD_TYPE=debug"
	@echo "  make ARCH=64 BOOT_MODE=uefi BUILD_TYPE=release"
	@echo "  make buildall  # Build all combinations"
	@echo "  make dist      # Create distribution package"
