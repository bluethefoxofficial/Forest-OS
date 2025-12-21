# Forest OS

Forest OS (codename **ALDER**) is a unix-like operating system built as an
educational kernel and userspace stack. The repository contains the kernel
sources, userspace programs, libc, and tooling required to build a bootable ISO.

## Building

1. **Populate the cross-toolchain**  
   Copy the prebuilt Forest OS toolchain into `forestos-toolchain/` so it
   contains `install/` (with `i686-forestos-gcc`, `i686-forestos-ld`, etc.) and
   `sysroot/`. You can also point `FORESTOS_TOOLCHAIN_DIR` to a custom path when
   running `make`.

2. **Build the ISO**  
   ```bash
   make iso
   ```
   The kernel, initrd, and ISO image will be written under `iso/`.

3. **Run in QEMU (optional)**  
   ```bash
   make run
   ```

The `makefile` validates that the toolchain is present before compiling. See
`forestos-toolchain/README.md` and `docs/DEVELOPMENT_GUIDES.md` for more details.
