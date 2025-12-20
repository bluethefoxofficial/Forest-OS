Forest OS Libraries
===================

The `libs/` directory collects all reusable libraries that ship with Forest OS.

- `forestcore/` – shared helpers intended for low-level kernel/runtime code (Forest OS specific types, MMIO helpers, audio, etc.).
- `libc/` – exported C standard library interface (headers aligned with the ISO C list the kernel ships).
- `uacpi/` – third-party ACPI implementation pulled in as a subtree.

Additional libraries should follow the same layout (`include/`, `src/`, tests, etc.)
to keep the toolchain configuration straightforward.
