# Forest OS Advanced Build System

## Overview

Forest OS features a comprehensive, multi-architecture build system that supports:

- **32-bit and 64-bit architectures** (i686, x86_64)
- **BIOS and UEFI boot modes**
- **Multiple build types** (debug, release, optimize)
- **Cross-compilation with custom toolchain**
- **Distribution packaging**
- **Automated testing**

## Quick Start

### Prerequisites

1. **Forest OS Toolchain** - Build the custom toolchain first:
   ```bash
   cd forestos-toolchain
   # Follow instructions in forestos-toolchain/README.md
   ```

2. **System Dependencies**:
   ```bash
   # Ubuntu/Debian
   sudo apt install build-essential nasm grub-common grub-pc-bin grub-efi-amd64-bin xorriso

   # Fedora/RHEL
   sudo dnf install gcc nasm grub2-tools grub2-efi-modules xorriso
   ```

### Simple Build Commands

```bash
# Quick 32-bit BIOS build
make ARCH=32 BOOT_MODE=bios BUILD_TYPE=debug

# Quick 64-bit UEFI build  
make ARCH=64 BOOT_MODE=uefi BUILD_TYPE=release

# Build all configurations
make buildall

# Create distribution package
./build-dist.sh
```

### Build Helper Script

For convenience, use the build helper script:

```bash
# Quick builds
./build-helper.sh quick-32        # 32-bit debug
./build-helper.sh quick-64        # 64-bit debug

# Release builds
./build-helper.sh release-32      # 32-bit release + ISO
./build-helper.sh release-64      # 64-bit release + ISO

# UEFI builds
./build-helper.sh uefi-32         # 32-bit UEFI + IMG
./build-helper.sh uefi-64         # 64-bit UEFI + IMG

# Testing
./build-helper.sh test-32         # Test in QEMU
./build-helper.sh test-64         # Test in QEMU

# Full distribution
./build-helper.sh dist            # Complete distribution
```

## Configuration Variables

### Architecture Selection
```bash
ARCH=32         # Build for i686 (32-bit)
ARCH=64         # Build for x86_64 (64-bit)
```

### Boot Mode Selection
```bash
BOOT_MODE=bios  # Traditional BIOS/Legacy boot
BOOT_MODE=uefi  # Modern UEFI boot
```

### Build Type Selection
```bash
BUILD_TYPE=debug     # Debug build (symbols, no optimization)
BUILD_TYPE=release   # Release build (optimized, stripped)
BUILD_TYPE=optimize  # Maximum optimization (LTO, native tuning)
```

## Directory Structure

```
Forest-OS/
├── build/                      # Built kernels (per configuration)
│   ├── 32bit-bios-debug/
│   ├── 32bit-bios-release/
│   ├── 32bit-uefi-debug/
│   ├── 32bit-uefi-release/
│   ├── 64bit-bios-debug/
│   ├── 64bit-bios-release/
│   ├── 64bit-uefi-debug/
│   └── 64bit-uefi-release/
├── obj/                        # Object files (per configuration)
├── dist/                       # Distribution packages
│   ├── packages/               # Individual package files
│   ├── releases/               # Release documentation
│   └── logs/                   # Build logs
├── Makefile                    # Advanced multi-arch Makefile
├── build-dist.sh              # Distribution build script
├── build-helper.sh            # Quick build helper
└── build-config.mk            # Build configuration options
```

## Advanced Usage

### Custom Configuration

Create a `local-config.mk` file to override default settings:

```makefile
# Enable extra features
ENABLE_SMP = yes
ENABLE_ACPI = yes
ENABLE_GRAPHICS = yes

# Custom optimization
DEBUG_OPTIMIZATION = -Og
RELEASE_OPTIMIZATION = -O3

# Custom toolchain path
FORESTOS_TOOLCHAIN_DIR = /opt/forestos-toolchain
```

Then include it in your build:
```bash
make -f Makefile -f local-config.mk ARCH=64 BUILD_TYPE=release
```

### Parallel Building

The build system automatically detects available CPU cores, but you can override:

```bash
make ARCH=64 BUILD_TYPE=release -j8    # Use 8 parallel jobs
```

### Distribution Building

The distribution script supports extensive customization:

```bash
# Build only specific configurations
ARCHITECTURES="64" BOOT_MODES="uefi" BUILD_TYPES="release" ./build-dist.sh

# Verbose output
./build-dist.sh --verbose

# Skip testing
./build-dist.sh --no-test

# Custom parallel jobs
./build-dist.sh --jobs 16
```

## Build Targets

### Primary Targets

| Target | Description |
|--------|-------------|
| `help` | Show help information |
| `all` | Build kernel and create bootable image |
| `build` | Build kernel binary only |
| `iso` | Create ISO image (BIOS mode) |
| `img` | Create disk image (UEFI mode) |
| `clean` | Clean current configuration |
| `clean-all` | Clean all configurations |

### Architecture-Specific Targets

| Target | Description |
|--------|-------------|
| `build32` | Build 32-bit version |
| `build64` | Build 64-bit version |
| `build32-bios` | Build 32-bit BIOS version |
| `build32-uefi` | Build 32-bit UEFI version |
| `build64-bios` | Build 64-bit BIOS version |
| `build64-uefi` | Build 64-bit UEFI version |
| `buildall` | Build all combinations |

### Testing Targets

| Target | Description |
|--------|-------------|
| `run` | Run in QEMU (BIOS mode) |
| `run-bios` | Run BIOS kernel in QEMU |
| `run-uefi` | Run UEFI kernel in QEMU |
| `debug` | Run with GDB support |
| `test` | Run automated tests |

### Utility Targets

| Target | Description |
|--------|-------------|
| `info` | Show build system information |
| `size` | Show binary size information |
| `stats` | Show build statistics |
| `list-objects` | List object files |

## Output Files

### BIOS Mode
- **Kernel Binary**: `build/32bit-bios-debug/boot/kernel.bin`
- **ISO Image**: `dist/packages/forestos_32bit_bios_debug_TIMESTAMP.iso`

### UEFI Mode  
- **EFI Binary**: `build/64bit-uefi-release/BOOTX64.EFI`
- **Disk Image**: `dist/packages/forestos_64bit_uefi_release_TIMESTAMP.img`

## Testing

### QEMU Testing

```bash
# Test BIOS boot
make ARCH=32 BUILD_TYPE=debug run-bios

# Test UEFI boot (requires OVMF)
make ARCH=64 BUILD_TYPE=release run-uefi

# Debug with GDB
make ARCH=32 BUILD_TYPE=debug debug
# In another terminal: gdb build/32bit-bios-debug/boot/kernel.bin
# (gdb) target remote :1234
```

### Real Hardware Testing

```bash
# Flash BIOS ISO to USB
dd if=dist/packages/forestos_32bit_bios_release_*.iso of=/dev/sdX bs=1M

# Flash UEFI image to USB
dd if=dist/packages/forestos_64bit_uefi_release_*.img of=/dev/sdX bs=1M
```

## Troubleshooting

### Common Issues

**Toolchain not found**:
```bash
# Check toolchain status
./build-helper.sh toolchain

# Verify paths
make info
```

**Build failures**:
```bash
# Check build logs
cat dist/logs/build_32bit_bios_debug.log

# Verbose build
make ARCH=32 BUILD_TYPE=debug V=1
```

**Missing dependencies**:
```bash
# Install required packages
sudo apt install build-essential nasm grub-common xorriso

# Check dependencies
./build-dist.sh --help
```

### Performance Tips

1. **Use parallel builds**: `make -j$(nproc)`
2. **Use ccache**: `export USE_CCACHE=yes`
3. **Disable unused features**: Edit `build-config.mk`
4. **Use release builds**: `BUILD_TYPE=release`

## Feature Flags

The build system supports extensive feature configuration via `build-config.mk`:

```makefile
# Core features
ENABLE_SMP = yes                # Symmetric multiprocessing
ENABLE_ACPI = yes              # Advanced Configuration and Power Interface
ENABLE_APIC = yes              # Advanced Programmable Interrupt Controller

# Memory features
ENABLE_PAGING = yes            # Virtual memory paging
ENABLE_SLAB_ALLOCATOR = yes    # SLAB memory allocator
ENABLE_MEMORY_PROTECTION = yes # Memory protection features

# Graphics features
ENABLE_GRAPHICS = yes          # Graphics subsystem
ENABLE_VESA = yes             # VESA graphics support
ENABLE_FRAMEBUFFER = yes      # Framebuffer support

# Debug features
ENABLE_DEBUG_SYMBOLS = yes     # Include debug symbols
ENABLE_KERNEL_DEBUGGING = yes  # Kernel debugging support
ENABLE_MEMORY_DEBUGGING = no   # Memory debugging (slower)

# Security features
ENABLE_SMEP_SMAP = yes        # Supervisor Mode Execution/Access Prevention
ENABLE_NX_BIT = yes           # No-Execute bit support
ENABLE_ASLR = no              # Address Space Layout Randomization
```

## Integration with IDEs

### Visual Studio Code

Add to `.vscode/tasks.json`:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build Forest OS 32-bit",
            "type": "shell",
            "command": "make",
            "args": ["ARCH=32", "BUILD_TYPE=debug"],
            "group": "build"
        },
        {
            "label": "Build Forest OS 64-bit",
            "type": "shell", 
            "command": "make",
            "args": ["ARCH=64", "BUILD_TYPE=debug"],
            "group": "build"
        },
        {
            "label": "Run in QEMU",
            "type": "shell",
            "command": "make",
            "args": ["ARCH=32", "BUILD_TYPE=debug", "run"],
            "group": "test"
        }
    ]
}
```

### CLion/IntelliJ

Configure CMake with custom toolchain:

```cmake
set(CMAKE_C_COMPILER "${FORESTOS_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_ASM_COMPILER "nasm")
set(CMAKE_C_FLAGS "${CFLAGS}")
```

## Continuous Integration

Example GitHub Actions workflow:

```yaml
name: Forest OS Build
on: [push, pull_request]

jobs:
  build:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        arch: [32, 64]
        boot_mode: [bios, uefi]
        build_type: [debug, release]
        
    steps:
    - uses: actions/checkout@v3
    
    - name: Install dependencies
      run: |
        sudo apt update
        sudo apt install build-essential nasm grub-common xorriso
        
    - name: Build toolchain
      run: |
        cd forestos-toolchain
        ./build-toolchain.sh
        
    - name: Build Forest OS
      run: |
        make ARCH=${{ matrix.arch }} BOOT_MODE=${{ matrix.boot_mode }} BUILD_TYPE=${{ matrix.build_type }} all
        
    - name: Test build
      run: |
        ./build-helper.sh test-${{ matrix.arch }}
```

## Contributing

When contributing to the build system:

1. Test all architecture combinations
2. Update documentation for new features  
3. Maintain backward compatibility
4. Follow existing code style
5. Add appropriate error checking

For more information, see the main project README and development documentation.