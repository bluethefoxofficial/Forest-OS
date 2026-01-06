# Repository Guidelines

## Project Structure & Module Organization

- **Kernel core** lives under `src/`, with flat subsystems (interrupts, memory, drivers) plus focused subfolders such as `src/graphics/`, `src/bios/`, and `src/uefi/`. Headers are mirrored in `src/include/` for both kernel and exported libc types.
- **Userspace** programs sit in `userspace/`, sharing a small libc (`userspace/libc/`) and common CRT objects. Finished binaries are staged into `initrd/` during the build.
- **Reusable libraries** live in `libs/` (`forestcore/`, `libc/`, and the vendored `uacpi/` implementation).
- **Build tooling** spans the top-level `Makefile`, helper scripts (`build-helper.sh`, `build-dist.sh`, `conf.sh`), generated objects in `obj/`, artifacts in `build/`, and release payloads in `dist/`.
- **Documentation** for workflows and subsystems is under `docs/`, while platform data (GRUB menus, initrd skeleton) is stored in `Grub/` and `initrd/` respectively.

## Build, Test, and Development Commands

```bash
# Build a specific target
make ARCH=64 BOOT_MODE=uefi BUILD_TYPE=release

# Build everything (all arches/boot modes)
make buildall

# Produce distribution artifacts
./build-dist.sh

# Run configuration TUI (menuconfig-style)
./conf.sh

# Launch QEMU smoke test (helper script)
./build-helper.sh test-32
```

## Coding Style & Naming Conventions

- **Indentation**: 4 spaces, no hard tabs (see `src/mm_buddy.c`, `src/kernel.c`).
- **File naming**: lower_snake_case for C sources (`interrupt_controller_abstraction.c`), UEFI/BIOS-specific code in dedicated folders, and `.asm`/`.s` suffixes for NASM stubs.
- **Function/variable naming**: kernel uses descriptive snake_case; exported structs end in `_t`. Global state often carries a `g_` prefix (e.g., `g_panicui`).
- **Linting/formatting**: No automatic formatter is configured; follow existing style and keep 80-col friendly lines. The vendored `uacpi` subtree enforces the same 4-space rule.

## Testing Guidelines

- **Framework**: No standalone unit runner yet; tests are invoked by compiling dedicated `*_test.c` files (e.g., `bitmap_pmm_test.c`, `memory_corruption_test.c`) and exercising them via QEMU logs.
- **Test files**: Kernel tests live next to the modules they verify; userspace smoke tools reside in `userspace/` and can be run inside the Forest shell.
- **Running tests**: Prefer `./build-helper.sh test-32`/`test-64` for scripted QEMU boots, or `make ARCH=32 BOOT_MODE=bios BUILD_TYPE=debug run` for manual sessions.
- **Coverage**: Not enforced yet—capture serial output (`boot.log`) and ensure regressions are documented.

## Commit & Pull Request Guidelines

- **Commit format**: Follow concise, imperative messages as seen in git history (`"Document build prerequisites"`, `"changes to add proper TTY"`). Group related changes into a single commit.
- **PR process**: Sync with `docs/BUILD_SYSTEM.md`/`docs/DEVELOPMENT_GUIDES.md` when adding features, describe architecture/boot targets tested, and attach QEMU logs for regressions.
- **Branch naming**: Not enforced, but prefer topical branches (`feature/uefi-loader`, `fix/mm-leak`) so CI scripts and reviewers can distinguish scope quickly.

---

# Repository Tour

## 🎯 What This Repository Does

Forest OS (codename **ALDER**) is a pedagogical Unix-like operating system that bundles its own kernel, userland utilities, libc, and build tooling to produce bootable BIOS/UEFI images.

**Key responsibilities:**
- Provide a modular kernel with modern interrupt, memory, graphics, and driver subsystems.
- Ship a busybox-style userspace so the OS can demonstrate syscalls end-to-end.
- Automate cross-compilation, packaging, and QEMU testing across i686/x86_64 targets.

---

## 🏗️ Architecture Overview

### System Context
```
Developer → [Build System (Makefile + helper scripts)] → ISO/IMG
                                            ↓
                                   [Forest OS Kernel + Initrd]
                                            ↓
                                   [Userspace CLI programs]
```

### Key Components
- **Boot & Loader Layer** (`Grub/`, `src/bios/`, `src/uefi/`): Handles GRUB configuration, NASM stubs, and PE/COFF conversion so kernels launch on BIOS or UEFI firmware.
- **Kernel Core** (`src/kernel.c`, `src/interrupt*.c`, `src/mm_*.c`): Implements scheduling hooks, ACPI/uACPI-powered hardware discovery, interrupt routing, memory allocators (buddy, slab, VMA), and SMP scaffolding (`docs/MULTICORE_UEFI.md`).
- **Userspace Stack** (`userspace/`, `libs/libc/`): Builds statically linked CLI tools using the exported libc headers, with binaries embedded into the initrd and optionally into the kernel image via `USER_PRIMARY_ELF`.
- **Build & Configuration Tools** (`Makefile`, `build-config.mk`, `conf.sh`, `build-helper.sh`, `build-dist.sh`): Coordinate multi-architecture builds, expose menuconfig-style toggles, and generate distributable ISOs/IMGs.

### Data Flow
1. Developers tailor features via `conf.sh`, generating `.forestos_config` and `build-config.mk`.
2. `make` (optionally through helper scripts) compiles kernel, libraries, and userspace targets using the Forest toolchain.
3. `initrd/` is repopulated with libc exports and freshly built CLI binaries, then archived.
4. GRUB assets plus kernel/initrd are packaged into ISO (BIOS) or IMG (UEFI) artifacts under `dist/`.
5. QEMU/OVMF boots the artifact, loading the kernel, which mounts the initrd and exposes userspace programs.

---

## 📁 Project Structure [Partial Directory Tree]

```
Forest-OS/
├── Makefile                  # Multi-arch build orchestration
├── build-config.mk           # Generated feature flags
├── conf.sh                   # Menuconfig-style configurator
├── src/                      # Kernel sources (drivers, MM, interrupts)
│   ├── include/              # Shared headers (kernel + libc exports)
│   ├── bios/                 # Legacy boot helpers
│   ├── uefi/                 # PE/COFF entry points
│   └── graphics/             # 2D compositor, font renderer, drivers
├── userspace/                # CLI tools and mini-libc
│   └── libc/                 # Userspace C library implementation
├── libs/                     # Reusable libraries (forestcore, libc, uacpi)
├── docs/                     # Build, syscall, CPU, and dev guides
├── initrd/                   # Rootfs template populated during builds
├── forestos-toolchain/       # Placeholder for cross-toolchain binaries
├── tools/                    # Utility scripts (multi-arch builds, gdb)
├── Grub/                     # GRUB configs for ISO images
├── build/ / obj/             # Generated binaries/objects per config
└── dist/                     # Packaged ISOs/IMGs and logs
```

### Key Files to Know

| File | Purpose | When You'd Touch It |
|------|---------|---------------------|
| `Makefile` | Defines ARCH/BOOT_MODE/BUILD_TYPE matrix, userspace embedding, ISO/IMG targets | Any kernel or userspace change that alters build rules or new targets |
| `build-config.mk` | Generated feature flags consumed by the Makefile | When adding new menu-configurable features (ensure conf.sh emits them) |
| `conf.sh` | Linux-style TUI configurator emitting `.forestos_config` | To introduce new build-time options or adjust defaults |
| `src/kernel.c` | Top-level `kmain` and subsystem bring-up order | Adding new drivers/services to the boot sequence |
| `src/mm_buddy.c` / `src/mm_slab.c` | Core physical & object allocators | Tuning memory policies or debugging OOM paths |
| `src/interrupt*.c` | Interrupt manager, handlers, APIC integrations | Extending SMP, timers, or DMA-driven workflows |
| `userspace/shell.c` & peers | Primary CLI entrypoints compiled into initrd | Shipping new command-line tools or syscall demos |
| `libs/libc/` headers | User-visible libc surface matched to kernel syscalls | Adding syscall wrappers or ensuring ABI compatibility |
| `docs/DEVELOPMENT_GUIDES.md` | Canonical workflow for drivers, syscalls, userspace tooling | Keeping contributor-facing recipes current |
| `forestos-toolchain/README.md` | Explains cross-toolchain requirements | Onboarding new developers or CI nodes |
| `build-helper.sh` / `build-dist.sh` | Wrapper scripts for common build/test patterns | Automating CI jobs or release assembly |

---

## 🔧 Technology Stack

### Core Technologies
- **Languages**: C (kernel, libc, userspace) and NASM assembly (`src/boot*.asm`, `context_switch.asm`) for low-level entry, plus shell scripts for tooling.
- **Build System**: GNU Make with parameterized variables (`ARCH`, `BOOT_MODE`, `BUILD_TYPE`) and helper scripts for distro-style releases (`build-dist.sh`).
- **Firmware Targets**: GRUB-based BIOS boot and UEFI PE/COFF binaries (converted via `objcopy --target=efi-app-*`).
- **Hardware Abstraction**: uACPI (vendored under `libs/uacpi/`) for ACPI parsing, HPET/TSC calibration modules (`src/hpet.c`, `src/tsc_calibration.c`), and SMP topology from MADT.

### Key Libraries
- **uACPI**: Third-party ACPI interpreter powering table discovery and power management (`libs/uacpi/source/`).
- **ForestCore**: Internal helper lib packaging kernel-grade utilities for reuse (`libs/forestcore/`).
- **Mini libc**: Exported headers plus implementations in `libs/libc/` and `userspace/libc/` for POSIX-like APIs.

### Development Tools
- **QEMU + OVMF**: Default virtualization targets for BIOS/UEFI smoke tests (`make run-bios`, `make run-uefi`).
- **NASM/LD/objcopy**: Toolchain components invoked directly by the Makefile for assembly and image conversion.
- **conf.sh TUI**: Menuconfig-style configuration front-end enforcing dependency checks.

---

## 🌐 External Dependencies

### Required Services
- **Forest OS cross-toolchain** (`i686-forestos-*`, `x86_64-forestos-*`): Must be staged inside `forestos-toolchain/` or referenced via `FORESTOS_TOOLCHAIN_DIR` before any build.
- **GRUB tooling** (`grub-mkrescue`, `grub-mkstandalone`, `xorriso`): Generates BIOS ISOs and EFI images.
- **QEMU/OVMF**: Used for automated and manual boot validation; OVMF firmware provides GOP framebuffer for UEFI tests.

### Optional Integrations
- **sfdisk/parted/sgdisk**: Enhance UEFI `img` creation (Makefile falls back gracefully if absent).
- **ccache**: Can be injected via environment variables for faster rebuilds.

---

### Environment Variables

```bash
FORESTOS_TOOLCHAIN_DIR=/opt/forestos-toolchain   # Override default toolchain path
FORESTOS_TOOLCHAIN_PREFIX=i686-forestos-         # Custom compiler prefix (if scripts rely on it)
FORESTOS_SYSROOT=/opt/forestos-sysroot           # Alternate sysroot location
```

---

## 🔄 Common Workflows

### Kernel/Userspace Build & Test
1. Configure feature flags with `./conf.sh` (saves `.forestos_config`).
2. Run `make ARCH=64 BOOT_MODE=uefi BUILD_TYPE=debug all` to compile kernel + userspace and produce an image.
3. Launch `./build-helper.sh test-64` or `make run-uefi` to boot QEMU with captured serial logs.

**Code path:** `conf.sh` → `build-config.mk` → `Makefile` targets → `dist/forestos_*`

### Adding a Driver or Syscall (per `docs/DEVELOPMENT_GUIDES.md`)
1. Create headers in `src/include/`, implement logic under `src/` (or subdirectory) using driver manager APIs.
2. Register the driver/syscall within `src/kernel.c` or `src/syscall.c` and expose user stubs in `libs/libc/`.
3. Rebuild (`make build`) and validate via a purpose-built userspace tool under `userspace/`.

**Code path:** `src/include/*` → `src/<subsystem>.c` → `userspace/<tool>.c`

### Menuconfig-Driven Feature Sets
1. Run `./conf.sh --save <profile>` to capture specialized configs (server, desktop, minimal).
2. Commit the profile (if desired) and regenerate `build-config.mk` before builds or CI runs.

**Code path:** `conf.sh` → `.forestos_config` → `build-config.mk`

---

## 📈 Performance & Scale

### Performance Considerations
- **SMP groundwork** (`smp_init`, ACPI MADT parsing) allows future AP bring-up and balanced interrupt distribution.
- **Memory allocators** (buddy + slab + page cache) are modeled after Linux designs for predictable latency.
- **Timer calibration** via HPET/PIT + TSC ensures timekeeping precision for schedulers and profiling.

### Monitoring
- Serial console output (`boot.log`) captures initialization timing and driver status.
- `make stats` reports counts of compiled objects and userspace apps for quick sanity checks.

---

## 🚨 Things to Be Careful About

### 🔒 Security Considerations
- **Hardware protections**: SMEP/SMAP toggles (`src/smep_smap.c`) and stack canaries (`src/stack_protection.c`) are configurable via `build-config.mk`; ensure they stay enabled for release builds.
- **Syscall surface**: Only a limited Linux-compatible subset is implemented (`docs/SYSCALLS.md`); userland must tolerate `-ENOSYS` responses.
- **Tooling secrets**: Keep toolchain paths outside the repo or protected via environment variables; `forestos-toolchain/` is intentionally gitignored.

*Last updated: 2026-01-05*
