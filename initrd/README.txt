Forest OS initrd
-----------------
This is a tiny ustar/tar initrd image loaded by GRUB as a multiboot module.
The kernel indexes the archive at boot and exposes a simple ramdisk API.

The `initrd/` directory mirrors a standard Unix filesystem layout (bin, etc,
usr/, var/, and so on). Empty directories are tracked with `.gitkeep` files and
are stripped from the final archive via `tar --exclude='.gitkeep'` so the
ramdisk only contains real payloads. During boot, `src/ramdisk.c` additionally
injects volatile/system-managed directories (e.g., `dev`, `proc`, `sys`, `run`,
and `var/*` state directories) to keep the initrd aligned with the FHS without
shipping fake placeholder files.

Userspace ELFs found in `userspace/*.c` are built automatically and copied into
both `/bin` and `/usr/bin` inside the initrd. The primary shell remains
`/bin/shell.elf`; additional demo programs (e.g., `hello.elf`, `echo.elf`) are
packaged alongside it.

The kernel's libc headers and sources are exported to `/usr/libc` inside the
initrd on every build. They originate from `libs/libc/` so developers compiling
on Forest can include the same libc that shipped with the kernel.
