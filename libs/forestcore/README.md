ForestCore Runtime
==================

ForestCore packages the low-level kernel runtime pieces that do not belong in
the exported C standard library. This includes:

- `include/` – Forest-specific headers such as `types.h`, `system.h`, `net.h`,
  and helpers that wrap MMIO/I/O port access.
- `src/` – freestanding implementations (`audio.c`, `string.c`, `system.c`,
  `util.c`, etc.) that the kernel and boot-time services reuse.

Run `make refresh-libc` to regenerate the exported ForestCore snapshot from the
authoritative sources under `src/`.
