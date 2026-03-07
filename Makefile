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
    TARGET_ARCH := i386
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

# Forest OS Toolchain Configuration
# Override via FORESTOS_TOOLCHAIN_DIR if toolchain is elsewhere
FORESTOS_TOOLCHAIN_DIR ?= $(REPO_ROOT)/forestos-toolchain

# Architecture-specific toolchain configuration
ifeq ($(ARCH),32)
    FORESTOS_TOOLCHAIN_PREFIX := i686-forestos
    TOOLCHAIN_ARCH_DIR := $(FORESTOS_TOOLCHAIN_DIR)/install
    ARCH_FLAGS := -m32 -march=i386 -mtune=i386
    ARCH_LDFLAGS := -m elf_i386
    EFI_ARCH := i386
else ifeq ($(ARCH),64)
    # Try to find 64-bit Forest OS toolchain
    FORESTOS_TOOLCHAIN_PREFIX := x86_64-forestos
    TOOLCHAIN_ARCH_DIR := $(FORESTOS_TOOLCHAIN_DIR)/install
    
    # Check if Forest OS 64-bit toolchain exists
    ifeq ($(wildcard $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-gcc),)
        # Fallback to host system x86_64 toolchain for development
        $(info Forest OS 64-bit toolchain not found - using host x86_64 toolchain)
        FORESTOS_TOOLCHAIN_PREFIX := x86_64-linux-gnu
        TOOLCHAIN_ARCH_DIR := 
        CC := gcc
        CXX := g++
        LD := ld
        AR := ar
        STRIP := strip
        OBJCOPY := objcopy
        OBJDUMP := objdump
        READELF := readelf
        SIZE := size
        FORESTOS_TOOLCHAIN_HAS_64BIT := false
    else
        FORESTOS_TOOLCHAIN_HAS_64BIT := true
    endif
    
    ARCH_FLAGS := -m64 -march=x86-64 -mcmodel=kernel
    ARCH_LDFLAGS := -m elf_x86_64
    EFI_ARCH := x86_64
endif

# Cross-compiler tools (use cross-compiler when available, fallback to host)
ifeq ($(FORESTOS_TOOLCHAIN_HAS_64BIT),false)
    # Using host toolchain fallback
    CC := gcc
    CXX := g++
    LD := ld
    AS := as
    NASM := nasm
    OBJCOPY ?= objcopy
    STRIP ?= strip
else
    # Using Forest OS cross-compiler
    CC := $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-gcc
    CXX := $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-g++
    LD := $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-ld
    AS := $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-as
    NASM := nasm
    OBJCOPY ?= $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-objcopy
    STRIP ?= $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-strip
endif
NM := $(TOOLCHAIN_ARCH_DIR)/bin/$(FORESTOS_TOOLCHAIN_PREFIX)-nm

# Find libgcc.a for soft-float support (needed for freestanding floating-point operations)
# This library provides __muldf3, __divdf3, __adddf3, __subdf3, and other soft-float functions
LIBGCC_PATH := $(shell $(CC) -print-libgcc-file-name 2>/dev/null)
# Only use libgcc if it's a valid absolute path (not just "libgcc.a")
ifeq ($(shell test -f "$(LIBGCC_PATH)" && echo yes),yes)
    LIBGCC := $(LIBGCC_PATH)
else
    LIBGCC :=
endif

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
                 -fcf-protection=none

# Architecture-specific flags
ifeq ($(ARCH),32)
    # 32-bit: Strict i386 compatibility, use x87 FPU for kernel floating-point paths
    COMMON_CFLAGS += -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mno-3dnow \
                      -D__i386__ -D__32BIT__ -mfpmath=387
else ifeq ($(ARCH),64)
    # 64-bit: x86_64 with kernel memory model, no SIMD in kernel
    COMMON_CFLAGS += -mno-red-zone -mcmodel=kernel -mno-mmx -mno-sse -mno-sse2 \
                      -D__x86_64__ -D__64BIT__ -mno-80387 -msoft-float
endif

# Boot mode specific flags
ifeq ($(BOOT_MODE),uefi)
    COMMON_CFLAGS += -DUEFI_BOOT -fshort-wchar
else
    COMMON_CFLAGS += -DBIOS_BOOT
endif

# Build type specific flags
ifeq ($(BUILD_TYPE),debug)
    CFLAGS := $(COMMON_CFLAGS) -g -O0
    LDFLAGS := $(ARCH_LDFLAGS) -g
else
ifeq ($(BUILD_TYPE),release)
    CFLAGS := $(COMMON_CFLAGS) -O0
    LDFLAGS := $(ARCH_LDFLAGS) -O0 --gc-sections -s
else
    CFLAGS := $(COMMON_CFLAGS) -O3
    LDFLAGS := $(ARCH_LDFLAGS) -O3 --gc-sections -s -flto
endif
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
    BOOT_OBJ := boot64.o
else
    BOOT_ASM := src/boot.asm
    BOOT_OBJ := boot.o
endif

# Boot objects (must be first for multiboot header)
BOOT_OBJECTS := $(OBJDIR)/$(BOOT_OBJ)

LDFLAGS += -T $(LINKER_SCRIPT) --allow-multiple-definition

# NASM Assembly flags
ifeq ($(ARCH),32)
    NASMFLAGS := -f elf32 -D__i386__
else
    NASMFLAGS := -f elf64 -D__x86_64__
endif

# GNU as flags for .s files
ASFLAGS :=

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
    $(SRCDIR)/interrupt_priority.c \
    $(SRCDIR)/interrupt_statistics.c \
    $(SRCDIR)/interrupt_driven_io.c \
    $(SRCDIR)/interrupt_coalescing.c \
    $(SRCDIR)/interrupt_latency_optimization.c \
    $(SRCDIR)/interrupt_load_balancing.c \
    $(SRCDIR)/interrupt_context_switching.c \
    $(SRCDIR)/interrupt_affinity_control.c \
    $(SRCDIR)/interrupt_memory_sync.c \
    $(SRCDIR)/interrupt_eoi_management.c \
    $(SRCDIR)/interrupt_mask_primitives.c \
    $(SRCDIR)/interrupt_profiling.c \
    $(SRCDIR)/interrupt_replay_mechanism.c \
    $(SRCDIR)/interrupt_throttling.c \
    $(SRCDIR)/interrupt_vector_allocation.c \
    $(SRCDIR)/interrupt_controller_abstraction.c \
    $(SRCDIR)/cgdm_integration.c \
    $(wildcard $(SRCDIR)/interrupt_*.c) \
    $(SRCDIR)/acpi_interrupt_routing.c \
    $(SRCDIR)/fault_prevention.c \
    $(SRCDIR)/ipi_smp_coordination.c \
    $(SRCDIR)/irq_management.c \
    $(SRCDIR)/msi_support.c \
    $(SRCDIR)/smp_interrupt_distribution.c \
    $(SRCDIR)/spurious_interrupt.c \
    $(SRCDIR)/watchdog_interrupt_support.c \
    $(SRCDIR)/stb_vorbis.c \
    $(SRCDIR)/sound_pcm_device.c \
    $(SRCDIR)/usb.c \
    $(SRCDIR)/usb_hid.c \
    $(SRCDIR)/usb_hub.c \
    $(SRCDIR)/ehci_hc.c \
    $(SRCDIR)/uhci_hc.c \
    $(SRCDIR)/ohci_hc.c \
    $(SRCDIR)/xhci_hc.c

EXCLUDED_CSOURCES := $(filter-out $(SRCDIR)/interrupt.c $(SRCDIR)/interrupt_handlers.c $(SRCDIR)/interrupt_utils.c,$(EXCLUDED_CSOURCES))
# CGDM sources
CGDM_CSOURCES := $(SRCDIR)/display_manager.c $(SRCDIR)/mode_state.c $(SRCDIR)/hotkey.c

# ELF test source
ELF_TEST_CSOURCES := $(SRCDIR)/elf_test.c

CSOURCES := $(filter-out $(EXCLUDED_CSOURCES), $(wildcard $(SRCDIR)/*.c)) $(SRCDIR)/symlink.c
COBJECTS := $(CSOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Subsystem objects (subdirectory sources)
GRAPHICS_SRCS := $(wildcard $(SRCDIR)/graphics/*.c) $(wildcard $(SRCDIR)/graphics/drivers/*.c)
GRAPHICS_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(GRAPHICS_SRCS))

PANICUI_SRCS := $(wildcard $(SRCDIR)/panicui*.c)
PANICUI_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(PANICUI_SRCS))

CANOPY_SRCS := $(wildcard $(SRCDIR)/canopy/*.c)
CANOPY_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(CANOPY_SRCS))

INPUT_SRCS := $(wildcard $(SRCDIR)/input/*.c)
INPUT_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(INPUT_SRCS))

USB_CSOURCES := $(wildcard $(SRCDIR)/usb/*.c)

CGDM_SRCS := $(wildcard $(SRCDIR)/cgdm*.c)
CGDM_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(filter-out $(SRCDIR)/cgdm_integration.c,$(CGDM_SRCS)))

INTERRUPT_SRCS := $(wildcard $(SRCDIR)/interrupt*.c)
INTERRUPT_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(filter-out $(EXCLUDED_CSOURCES),$(INTERRUPT_SRCS)))

FS_SRCS := $(wildcard $(SRCDIR)/fs/*.c)
FS_OBJECTS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(FS_SRCS))

UACPI_SRCS := $(wildcard $(UACPI_SRCDIR)/*.c)
UACPI_OBJECTS := $(patsubst $(UACPI_SRCDIR)/%.c,$(OBJDIR)/uacpi_%.o,$(UACPI_SRCS))

# ASM objects (from .s and .asm files)
ASM_SRCS := $(wildcard $(SRCDIR)/*.s) $(wildcard $(SRCDIR)/*.asm)
ASMOBJECTS := $(patsubst $(SRCDIR)/%.s,$(OBJDIR)/%.o,$(filter $(SRCDIR)/%.s,$(ASM_SRCS))) \
              $(patsubst $(SRCDIR)/%.asm,$(OBJDIR)/%.o,$(filter $(SRCDIR)/%.asm,$(ASM_SRCS)))

ALL_OBJECTS := $(COBJECTS) $(GRAPHICS_OBJECTS) $(PANICUI_OBJECTS) $(CANOPY_OBJECTS) $(INPUT_OBJECTS) $(CGDM_OBJECTS) $(BOOT_OBJECTS) \
               $(ASMOBJECTS) $(INTERRUPT_OBJECTS) $(UACPI_OBJECTS) $(ELF_TEST_OBJECTS) $(FS_OBJECTS) \
               $(USB_CSOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o) $(HW_CSOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Userspace configuration
USER_OBJDIR := $(OBJDIR)/userspace
USER_LIBC_SRCS := $(wildcard userspace/libc/*.c)
USER_LIBC_OBJECTS := $(USER_LIBC_SRCS:userspace/libc/%.c=$(USER_OBJDIR)/libc_%.o)
USER_SUPPORT_OBJECTS := $(USER_LIBC_OBJECTS)

# Exclude crt0.c and session_config.c (library, not an app) from userspace apps
USER_APP_SRCS := $(filter-out userspace/crt0.c userspace/session_config.c,$(wildcard $(USER_SRCDIR)/*.c))
USER_APPS := $(basename $(notdir $(USER_APP_SRCS)))
USER_APP_OBJECTS := $(USER_APPS:%=$(USER_OBJDIR)/%.o)

# Apps that use LeafGFX and need special linking
LEAFGFX_APPS := canopydm canopyde test_mouse
USER_ELFS := $(filter-out $(LEAFGFX_APPS:%=$(USER_OBJDIR)/%.elf),$(USER_APPS:%=$(USER_OBJDIR)/%.elf))
USER_PRIMARY_APP := shell
USER_PRIMARY_ELF := $(USER_OBJDIR)/$(USER_PRIMARY_APP).elf
USER_ELF_BIN := $(OBJDIR)/$(USER_PRIMARY_APP)_elf.o
USER_APP_BINARIES := $(USER_APPS:%=$(INITRD_BIN_DIR)/%.elf)

# Additional test ELF binary for advanced testing
USER_ELF_TEST := $(USER_OBJDIR)/elf_test.elf

# Userspace uses same cross-compiler as kernel for consistency
USER_CFLAGS := $(ARCH_FLAGS) -c -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
               -Wall -Wextra -g -O0 \
               -I$(SRCDIR)/include -I$(LIBC_DIR)/include -I$(USER_SRCDIR)/include \
               -Ilibs/leafgfx \
               -fno-pic -fno-pie -DUSERSPACE_BUILD -mno-sse -mno-sse2 -mno-mmx -mno-3dnow

# LeafGFX userspace graphics library
LEAFGFX_DIR := libs/leafgfx
LEAFGFX_SRCS := $(LEAFGFX_DIR)/leafgfx.c $(LEAFGFX_DIR)/leafgfx_bmp.c $(LEAFGFX_DIR)/leafgfx_image.c \
                $(LEAFGFX_DIR)/leafgfx_font.c $(LEAFGFX_DIR)/leafgfx_input.c \
                $(LEAFGFX_DIR)/leafgfx_ttf.c \
                $(LEAFGFX_DIR)/leafgfx_ttf_raster.c $(LEAFGFX_DIR)/leafgfx_anim.c
LEAFGFX_OBJECTS := $(LEAFGFX_SRCS:$(LEAFGFX_DIR)/%.c=$(USER_OBJDIR)/leafgfx_%.o)
LEAFGFX_APPS := canopydm canopyde

# Userspace linker script selection
ifeq ($(ARCH),32)
    USER_LINKER_SCRIPT := userspace/link.ld
else ifeq ($(ARCH),64)
    USER_LINKER_SCRIPT := userspace/link64.ld
endif

# Userspace uses cross-compiler linker with proper sysroot
USER_LDFLAGS := $(ARCH_LDFLAGS) -nostdlib -T $(USER_LINKER_SCRIPT) \
                --sysroot=$(FORESTOS_TOOLCHAIN_DIR)/sysroot

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
	@echo "  test-elf    Test ELF loader functionality"
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

all: ensure-toolchain show-config build 
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

.PHONY: ensure-toolchain validate-toolchain
ensure-toolchain: validate-toolchain
	@echo "$(OK_COLOR)Toolchain validation passed for $(FORESTOS_TOOLCHAIN_PREFIX)$(NO_COLOR)"

validate-toolchain:
	@echo "$(INFO_COLOR)Validating Forest OS toolchain...$(NO_COLOR)"
	@if [ "$(FORESTOS_TOOLCHAIN_HAS_64BIT)" = "false" ]; then \
		echo "$(WARN_COLOR)Using fallback host toolchain for 64-bit builds$(NO_COLOR)"; \
		echo "$(INFO_COLOR)Using toolchain: $(FORESTOS_TOOLCHAIN_PREFIX)$(NO_COLOR)"; \
	else \
		if [ ! -d "$(FORESTOS_TOOLCHAIN_DIR)" ]; then \
			echo "$(ERROR_COLOR)Toolchain directory not found: $(FORESTOS_TOOLCHAIN_DIR)$(NO_COLOR)"; \
			echo "$(ERROR_COLOR)Please install Forest OS toolchain (see docs/DEVELOPMENT_GUIDES.md)$(NO_COLOR)"; \
			exit 1; \
		fi; \
		if [ ! -d "$(TOOLCHAIN_ARCH_DIR)" ]; then \
			echo "$(ERROR_COLOR)Architecture toolchain not found: $(TOOLCHAIN_ARCH_DIR)$(NO_COLOR)"; \
			if [ "$(ARCH)" = "64" ]; then \
				echo "$(ERROR_COLOR)Please install x86_64-forestos toolchain (see './build-64bit-toolchain.sh')$(NO_COLOR)"; \
			else \
				echo "$(ERROR_COLOR)Please install $(FORESTOS_TOOLCHAIN_PREFIX) toolchain$(NO_COLOR)"; \
			fi; \
			exit 1; \
		fi; \
	fi
	@if ! command -v $(CC) >/dev/null 2>&1; then \
		echo "$(ERROR_COLOR)Required compiler '$(CC)' not found in PATH.$(NO_COLOR)"; \
		exit 1; \
	fi
	@if ! command -v $(LD) >/dev/null 2>&1; then \
		echo "$(ERROR_COLOR)Required linker '$(LD)' not found in PATH.$(NO_COLOR)"; \
		exit 1; \
	fi
	@if ! command -v $(AS) >/dev/null 2>&1; then \
		echo "$(ERROR_COLOR)Required assembler '$(AS)' not found in PATH.$(NO_COLOR)"; \
		exit 1; \
	fi
	@echo "$(OK_COLOR)Using toolchain: $(FORESTOS_TOOLCHAIN_PREFIX)$(NO_COLOR)"
	@echo "$(INFO_COLOR)GCC version: $(NO_COLOR)"
	@$(CC) --version | head -1
	@echo "$(INFO_COLOR)LD version: $(NO_COLOR)"
	@$(LD) --version | head -1

.PHONY: toolchain-setup
toolchain-setup:
	@echo "$(INFO_COLOR)Forest OS Toolchain Setup$(NO_COLOR)"
	@echo "$(WARN_COLOR)This script helps set up the cross-compiler toolchain.$(NO_COLOR)"
	@echo ""
	@echo "1. Install build dependencies:"
	@echo "   sudo apt install build-essential libgmp-dev libmpfr-dev libmpc-dev texinfo"
	@echo ""
	@echo "2. Choose toolchain source:"
	@echo "   a) Download prebuilt toolchain"
	@echo "   b) Build from source (requires more time)"
	@echo ""
	@echo "3. Toolchain directory: $(FORESTOS_TOOLCHAIN_DIR)"
	@echo ""
	@echo "See docs/DEVELOPMENT_GUIDES.md for detailed instructions."

# =============================================================================
# KERNEL BUILD RULES
# =============================================================================

# UEFI kernel build
ifeq ($(BOOT_MODE),uefi)
$(OUTPUT): $(OUTPUT_ELF)
	@mkdir -p $(EFIDIR)
	@echo "$(OK_COLOR)Converting ELF to UEFI PE32+ binary...$(NO_COLOR)"
	@$(OBJCOPY) -j .multiboot -j .text -j .sdata -j .data -j .dynamic -j .dynsym -j .rel* \
	            -j .rela* -j .reloc -j .bss -j .stack --target=efi-app-$(EFI_ARCH) $< $@
	@echo "$(OK_COLOR)UEFI kernel binary generated: $@$(NO_COLOR)"

$(OUTPUT_ELF): $(ALL_OBJECTS) $(USER_ELF_BIN)
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Linking UEFI ELF binary...$(NO_COLOR)"
	@$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

# BIOS kernel build
else
$(OUTPUT): $(ALL_OBJECTS) $(USER_ELF_BIN)
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Linking BIOS kernel ELF...$(NO_COLOR)"
	@$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)
	@echo "$(OK_COLOR)Kernel ELF binary generated: $@$(NO_COLOR)"
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
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Canopy C compilation (handles subdirectories)
$(OBJDIR)/canopy/%.o: $(SRCDIR)/canopy/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/render/%.o: $(SRCDIR)/canopy/render/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy Render $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/compositor/%.o: $(SRCDIR)/canopy/compositor/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy Compositor $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/wm/%.o: $(SRCDIR)/canopy/wm/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy WM $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/de/%.o: $(SRCDIR)/canopy/de/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy DE $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/theme/%.o: $(SRCDIR)/canopy/theme/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy Theme $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/widgets/%.o: $(SRCDIR)/canopy/widgets/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy Widgets $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/canopy/apps/%.o: $(SRCDIR)/canopy/apps/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Canopy Apps $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Input subsystem compilation
$(OBJDIR)/input/%.o: $(SRCDIR)/input/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling Input subsystem $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Assembly compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.asm
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $<...$(NO_COLOR)"
	@$(NASM) $(NASMFLAGS) -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.s
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $<...$(NO_COLOR)"
	@$(AS) $(ASFLAGS) -o $@ $<

# Disambiguate the dual interrupt stub sources by architecture so the correct
# NASM file is assembled when both .asm and .s variants exist.
ifeq ($(ARCH),64)
$(OBJDIR)/interrupt_stubs.o: $(SRCDIR)/interrupt_stubs.s
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $< with NASM...$(NO_COLOR)"
	@$(NASM) $(NASMFLAGS) -f elf64 -o $@ $<
else
$(OBJDIR)/interrupt_stubs.o: $(SRCDIR)/interrupt_stubs.asm
	@mkdir -p $(OBJDIR)
	@echo "$(OK_COLOR)Assembling $<...$(NO_COLOR)"
	@$(NASM) $(NASMFLAGS) -o $@ $<
endif

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

# USB subsystem compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling USB $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Sound driver requiring SSE for floating point
$(OBJDIR)/sound_sb16.o: $(SRCDIR)/sound_sb16.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling sound_sb16 $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -msse -c -o $@ $<

$(OBJDIR)/sound_sb16_test.o: $(SRCDIR)/sound_sb16_test.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling sound_sb16_test $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -msse -c -o $@ $<

$(OBJDIR)/stb_vorbis_port.o: $(SRCDIR)/stb_vorbis_port.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling stb_vorbis_port $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -msse -c -o $@ $<

$(OBJDIR)/timer_calibration.o: $(SRCDIR)/timer_calibration.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling timer_calibration $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -msse -c -o $@ $<

$(OBJDIR)/graphics/enhanced_cursor.o: $(SRCDIR)/graphics/enhanced_cursor.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling enhanced_cursor $<...$(NO_COLOR)"
	@$(CC) $(CFLAGS) -msse -c -o $@ $<

# Hardware drivers compilation
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	@echo "$(OK_COLOR)Compiling hardware driver $<...$(NO_COLOR)"
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

# Assembly-based crt0 for minimal, correct userspace entry
# Note: Uses USER_ASM_FLAGS instead of ARCH_FLAGS to avoid -mcmodel=kernel
USER_ASM_FLAGS := -m$(ARCH) -ffreestanding -nostdlib
ifeq ($(ARCH),64)
    USER_CRT0_SRC := userspace/crt0_64.S
else
    USER_CRT0_SRC := userspace/crt0.S
endif

$(USER_OBJDIR)/crt0.o: $(USER_CRT0_SRC)
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Assembling userspace crt0 ($(ARCH)-bit)...$(NO_COLOR)"
	@$(CC) $(USER_ASM_FLAGS) -c -o $@ $<

# LeafGFX library compilation
$(USER_OBJDIR)/leafgfx_%.o: $(LEAFGFX_DIR)/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling LeafGFX: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

# Session config library (shared between canopydm and canopyde)
$(USER_OBJDIR)/session_config.o: userspace/session_config.c userspace/session_config.h
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling session_config...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

# CanopyDM modular sources
CANOPYDM_DIR := userspace/canopydm
CANOPYDM_SRCS := $(CANOPYDM_DIR)/canopydm.c
CANOPYDM_OBJECTS := $(CANOPYDM_SRCS:$(CANOPYDM_DIR)/%.c=$(USER_OBJDIR)/canopydm_%.o)

# CanopyDE modular sources (LeafGFX build)
CANOPYDE_DIR := userspace/canopyde
CANOPYDE_SRCS := $(CANOPYDE_DIR)/canopy.c \
                 $(CANOPYDE_DIR)/compositor/canopy_compositor.c \
                 $(CANOPYDE_DIR)/compositor/canopy_blur.c \
                 $(CANOPYDE_DIR)/render/canopy_render.c \
                 $(CANOPYDE_DIR)/render/canopy_shadow.c \
                 $(CANOPYDE_DIR)/theme/canopy_theme_tokens.c \
                 $(CANOPYDE_DIR)/de/canopy_de.c \
                 $(CANOPYDE_DIR)/de/canopy_panel.c \
                 $(CANOPYDE_DIR)/de/canopy_dock.c \
                 $(CANOPYDE_DIR)/de/canopy_overview.c \
                 $(CANOPYDE_DIR)/de/canopy_notifications.c \
                 $(CANOPYDE_DIR)/de/canopy_control_center.c \
                 $(CANOPYDE_DIR)/de/canopy_sleep_screen.c \
                 $(CANOPYDE_DIR)/de/canopy_sounds.c \
                 $(CANOPYDE_DIR)/de/canopy_hybrid_de.c \
                 $(CANOPYDE_DIR)/de/canopy_hybrid_panel.c \
                 $(CANOPYDE_DIR)/de/canopy_hybrid_dock.c \
                 $(CANOPYDE_DIR)/wm/canopy_wm.c \
                 $(CANOPYDE_DIR)/wm/canopy_decorations.c \
                 $(CANOPYDE_DIR)/wm/canopy_snap.c \
                 $(CANOPYDE_DIR)/widgets/canopy_widget.c \
                 $(CANOPYDE_DIR)/widgets/canopy_button.c \
                 $(CANOPYDE_DIR)/widgets/canopy_label.c \
                 $(CANOPYDE_DIR)/widgets/canopy_slider.c \
                 $(CANOPYDE_DIR)/widgets/canopy_toggle.c \
                 $(CANOPYDE_DIR)/widgets/canopy_textinput.c \
                 $(CANOPYDE_DIR)/apps/canopy_apps.c
CANOPYDE_OBJECTS := $(patsubst $(CANOPYDE_DIR)/%.c,$(USER_OBJDIR)/canopyde_%.o,$(subst /,_,$(CANOPYDE_SRCS)))

# Compile CanopyDM modules
$(USER_OBJDIR)/canopydm_%.o: $(CANOPYDM_DIR)/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDM module: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

# Compile CanopyDE modules
$(USER_OBJDIR)/canopyde_canopy.o: $(CANOPYDE_DIR)/canopy.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE: canopy.c...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_compositor_%.o: $(CANOPYDE_DIR)/compositor/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE compositor: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_render_%.o: $(CANOPYDE_DIR)/render/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE render: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_theme_%.o: $(CANOPYDE_DIR)/theme/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE theme: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_de_%.o: $(CANOPYDE_DIR)/de/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE de: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_wm_%.o: $(CANOPYDE_DIR)/wm/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE wm: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_widgets_%.o: $(CANOPYDE_DIR)/widgets/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE widget: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

$(USER_OBJDIR)/canopyde_apps_%.o: $(CANOPYDE_DIR)/apps/%.c
	@mkdir -p $(USER_OBJDIR)
	@echo "$(OK_COLOR)Compiling CanopyDE apps: $<...$(NO_COLOR)"
	@$(CC) $(USER_CFLAGS) -o $@ $<

# CanopyDE object list (explicit)
CANOPYDE_OBJ_LIST := $(USER_OBJDIR)/canopyde_canopy.o \
                     $(USER_OBJDIR)/canopyde_compositor_canopy_compositor.o \
                     $(USER_OBJDIR)/canopyde_compositor_canopy_blur.o \
                     $(USER_OBJDIR)/canopyde_render_canopy_render.o \
                     $(USER_OBJDIR)/canopyde_render_canopy_shadow.o \
                     $(USER_OBJDIR)/canopyde_theme_canopy_theme_tokens.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_de.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_panel.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_dock.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_overview.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_notifications.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_control_center.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_sleep_screen.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_sounds.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_hybrid_de.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_hybrid_panel.o \
                     $(USER_OBJDIR)/canopyde_de_canopy_hybrid_dock.o \
                     $(USER_OBJDIR)/canopyde_wm_canopy_wm.o \
                     $(USER_OBJDIR)/canopyde_wm_canopy_decorations.o \
                     $(USER_OBJDIR)/canopyde_wm_canopy_snap.o \
                     $(USER_OBJDIR)/canopyde_widgets_canopy_widget.o \
                     $(USER_OBJDIR)/canopyde_widgets_canopy_button.o \
                     $(USER_OBJDIR)/canopyde_widgets_canopy_label.o \
                     $(USER_OBJDIR)/canopyde_widgets_canopy_slider.o \
                     $(USER_OBJDIR)/canopyde_widgets_canopy_toggle.o \
                     $(USER_OBJDIR)/canopyde_widgets_canopy_textinput.o \
                     $(USER_OBJDIR)/canopyde_apps_canopy_apps.o

# Special linking rules for LeafGFX-dependent apps (canopydm)
$(USER_OBJDIR)/canopydm.elf: $(USER_OBJDIR)/canopydm.o $(CANOPYDM_OBJECTS) $(USER_OBJDIR)/session_config.o $(USER_SUPPORT_OBJECTS) $(LEAFGFX_OBJECTS) $(USER_OBJDIR)/crt0.o $(USER_LINKER_SCRIPT)
	@echo "$(OK_COLOR)Linking LeafGFX app: canopydm.elf...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/canopydm.o $(CANOPYDM_OBJECTS) $(USER_OBJDIR)/session_config.o $(LEAFGFX_OBJECTS) $(USER_SUPPORT_OBJECTS)

# Special linking rules for canopyDE (LeafGFX build)
$(USER_OBJDIR)/canopyde.elf: $(USER_OBJDIR)/canopyde.o $(CANOPYDE_OBJ_LIST) $(USER_OBJDIR)/session_config.o $(USER_SUPPORT_OBJECTS) $(LEAFGFX_OBJECTS) $(USER_OBJDIR)/crt0.o $(USER_LINKER_SCRIPT)
	@echo "$(OK_COLOR)Linking LeafGFX app: canopyde.elf...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/canopyde.o $(CANOPYDE_OBJ_LIST) $(USER_OBJDIR)/session_config.o $(LEAFGFX_OBJECTS) $(USER_SUPPORT_OBJECTS)

# Special linking rule for Canopy desktop apps
$(USER_OBJDIR)/canopy_app_%.elf: $(USER_OBJDIR)/canopy_app_%.o $(USER_OBJDIR)/session_config.o $(USER_SUPPORT_OBJECTS) $(LEAFGFX_OBJECTS) $(USER_OBJDIR)/crt0.o $(USER_LINKER_SCRIPT)
	@echo "$(OK_COLOR)Linking LeafGFX app: $(@F)...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/canopy_app_$*.o $(USER_OBJDIR)/session_config.o $(LEAFGFX_OBJECTS) $(USER_SUPPORT_OBJECTS)

$(USER_OBJDIR)/test_mouse.elf: $(USER_OBJDIR)/test_mouse.o $(USER_SUPPORT_OBJECTS) $(LEAFGFX_OBJECTS) $(USER_OBJDIR)/crt0.o $(USER_LINKER_SCRIPT)
	@echo "$(OK_COLOR)Linking LeafGFX app: test_mouse.elf...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/test_mouse.o $(LEAFGFX_OBJECTS) $(USER_SUPPORT_OBJECTS)

$(USER_ELFS): $(USER_SUPPORT_OBJECTS) $(USER_OBJDIR)/crt0.o
$(USER_OBJDIR)/%.elf: $(USER_OBJDIR)/%.o $(USER_SUPPORT_OBJECTS) $(USER_OBJDIR)/crt0.o $(USER_LINKER_SCRIPT)
	@echo "$(OK_COLOR)Linking userspace ELF $(@F)...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/$*.o $(USER_SUPPORT_OBJECTS)

$(USER_ELF_BIN): $(USER_PRIMARY_ELF)
	@echo "$(OK_COLOR)Embedding $(USER_PRIMARY_APP) ELF into kernel...$(NO_COLOR)"
	@$(LD) $(ARCH_LDFLAGS) -r -b binary -o $@ $<

$(USER_ELF_TEST): $(USER_OBJDIR)/elf_test.o $(USER_SUPPORT_OBJECTS) $(USER_OBJDIR)/crt0.o
	@echo "$(OK_COLOR)Building test ELF binary...$(NO_COLOR)"
	@$(LD) $(USER_LDFLAGS) -o $@ \
	       $(USER_OBJDIR)/crt0.o $(USER_OBJDIR)/elf_test.o $(USER_SUPPORT_OBJECTS)

# =============================================================================
# INITRD AND LIBRARY BUILD RULES
# =============================================================================

.PHONY: refresh-libc refresh-forestcore prepare-canopy-icons

refresh-libc: refresh-forestcore
	@echo "$(OK_COLOR)Refreshing exported libc sources...$(NO_COLOR)"
	@rm -rf $(LIBC_DIR)/include/libc
	@mkdir -p $(LIBC_DIR)/include
	@cp -r $(SRCDIR)/include/libc $(LIBC_DIR)/include/

refresh-forestcore:
	@echo "$(OK_COLOR)Refreshing ForestCore runtime exports...$(NO_COLOR)"
	@mkdir -p $(FORESTCORE_DIR)/src $(FORESTCORE_DIR)/include
	@rm -f $(FORESTCORE_DIR)/src/*.c $(FORESTCORE_DIR)/include/*.h
	@cp $(SRCDIR)/string.c $(SRCDIR)/util.c $(SRCDIR)/system.c $(SRCDIR)/audio.c $(FORESTCORE_DIR)/src/
	@cp $(SRCDIR)/include/types.h $(SRCDIR)/include/util.h $(SRCDIR)/include/string.h \
	    $(SRCDIR)/include/system.h $(SRCDIR)/include/net.h $(SRCDIR)/include/driver.h $(FORESTCORE_DIR)/include/

prepare-canopy-icons:
	@echo "$(OK_COLOR)Preparing Canopy desktop icons...$(NO_COLOR)"
	@./tools/prepare-canopy-icons.sh

$(INITRD): refresh-libc prepare-canopy-icons $(OUTPUT) $(INITRD_FILES) $(USER_APP_BINARIES)
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

# Conditional ISO creation rules based on BOOT_MODE
ifeq ($(BOOT_MODE),bios)
$(ISO): $(OUTPUT) $(GRUB_CFG) $(INITRD)
	@mkdir -p $(DISTDIR)
	@echo "$(OK_COLOR)Building BIOS ISO image using system GRUB...$(NO_COLOR)"
	@grub-mkrescue -o $@ $(OUTDIR)
	@echo "$(OK_COLOR)ISO created: $@$(NO_COLOR)"
else
# UEFI ISO creation
img: ensure-toolchain $(ISO)

$(ISO): $(OUTPUT) $(INITRD)
	@mkdir -p $(DISTDIR) $(OUTDIR)/iso_root
	@echo "$(OK_COLOR)Creating UEFI ISO...$(NO_COLOR)"
	@# Create EFI directory structure
	@mkdir -p $(OUTDIR)/iso_root/EFI/BOOT
	@echo "Copying EFI file: $(OUTPUT) -> $(OUTDIR)/iso_root/EFI/BOOT/BOOTX64.EFI"
	@cp $(OUTPUT) $(OUTDIR)/iso_root/EFI/BOOT/BOOTX64.EFI
	@-cp $(INITRD) $(OUTDIR)/iso_root/ 2>/dev/null || true
	@# Create startup script for EFI shell
	@echo 'fs0:' > $(OUTDIR)/iso_root/startup.nsh
	@echo 'EFI\BOOT\BOOTX64.EFI' >> $(OUTDIR)/iso_root/startup.nsh
	@# Create GRUB directory structure for BIOS boot
	@mkdir -p $(OUTDIR)/iso_root/boot/grub/i386-pc
	@# Copy GRUB boot images and modules from system installation
	@cp /usr/lib/grub/i386-pc/*.img $(OUTDIR)/iso_root/boot/grub/i386-pc/ 2>/dev/null || true
	@cp /usr/lib/grub/i386-pc/*.lst $(OUTDIR)/iso_root/boot/grub/i386-pc/ 2>/dev/null || true
	@cp /usr/lib/grub/i386-pc/*.mod $(OUTDIR)/iso_root/boot/grub/i386-pc/ 2>/dev/null || true
	@cp /usr/lib/grub/i386-pc/*.lst $(OUTDIR)/iso_root/boot/grub/i386-pc/ 2>/dev/null || true
	@cp /usr/lib/grub/i386-pc/lzma_decompress.img $(OUTDIR)/iso_root/boot/grub/i386-pc/ 2>/dev/null || true
	@cp /usr/lib/grub/i386-pc/boot.img $(OUTDIR)/iso_root/boot/grub/i386-pc/ 2>/dev/null || true
	@# Copy kernel and initrd for GRUB
	@cp $(if $(filter uefi,$(BOOT_MODE)),$(OUTPUT_ELF),$(OUTPUT)) $(OUTDIR)/iso_root/boot/kernel.elf
	@-cp $(INITRD) $(OUTDIR)/iso_root/boot/ 2>/dev/null || true
	@# Create GRUB configuration
	@echo 'set timeout=5' > $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo 'set default=0' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo 'menuentry "Forest OS (BIOS)" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    set gfxpayload=keep' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '    module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '}' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo 'submenu "Resolution selection" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1920x1080" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1920x1080x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1600x900" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1600x900x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1366x768" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1366x768x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1280x720" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1280x720x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1024x768" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1024x768x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "3840x2160 (4K UHD)" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=3840x2160x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "2560x1440 (QHD)" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=2560x1440x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1920x1200" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1920x1200x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1680x1050" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1680x1050x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1440x900" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1440x900x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1280x1024" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1280x1024x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1280x800" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1280x800x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1152x864" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1152x864x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "1024x600" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=1024x600x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "800x600" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=800x600x32' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    menuentry "Auto (Fallback)" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        set gfxpayload=keep' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '        multiboot2 /boot/kernel.elf' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '        module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '    }' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '}' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo 'menuentry "Forest OS (Quiet Splash)" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    set gfxpayload=keep' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    multiboot2 /boot/kernel.elf quiet' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@if [ -f $(INITRD) ]; then \
		echo '    module2 /boot/initrd.tar' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg; \
	fi
	@echo '}' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo 'menuentry "Forest OS (UEFI)" {' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '    chainloader /EFI/BOOT/BOOTX64.EFI' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@echo '}' >> $(OUTDIR)/iso_root/boot/grub/grub.cfg
	@ls -la $(OUTDIR)/iso_root/EFI/BOOT/
	@ls -la $(OUTDIR)/iso_root/boot/
	@# Create the ISO with GRUB for BIOS boot
	@if command -v grub-mkrescue >/dev/null 2>&1; then \
		grub-mkrescue -o $@ $(OUTDIR)/iso_root; \
	elif command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -o $@ -iso-level 3 \
			-eltorito-boot boot/grub/i386-pc/eltorito.img -no-emul-boot \
			-boot-load-size 4 -boot-info-table -volid "FOREST_OS" $(OUTDIR)/iso_root; \
	elif command -v mkisofs >/dev/null 2>&1; then \
		mkisofs -o $@ -b boot/grub/i386-pc/eltorito.img -no-emul-boot \
			-boot-load-size 4 -boot-info-table -volid "FOREST_OS" $(OUTDIR)/iso_root; \
	else \
		echo "$(WARN_COLOR)Warning: No ISO creation tools found$(NO_COLOR)"; \
		tar -cf $@ -C $(OUTDIR)/iso_root .; \
	fi
	@rm -rf $(OUTDIR)/iso_root
	@echo "$(OK_COLOR)UEFI ISO created: $@$(NO_COLOR)"
endif

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

run-uefi: ensure-toolchain $(ISO)
	@echo "$(OK_COLOR)Running $(ARCH)-bit kernel via GRUB in QEMU...$(NO_COLOR)"
	@qemu-system-$(TARGET_ARCH) -cdrom $(ISO) \
	                            -m 1024 -serial stdio -vga std

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
