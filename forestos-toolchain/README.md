Forest OS Toolchain Placeholder
==============================

This directory is intentionally kept empty in git so the repository stays
under the GitHub file size limits. To build Forest OS you still need the
prebuilt cross-toolchain that provides `i686-forestos-*` binaries and the
matching sysroot.

Steps:
1. Acquire or build the Forest OS toolchain (see `docs/DEVELOPMENT_GUIDES.md` for
   instructions). The resulting directory must contain `install/` (with
   `install/bin/i686-forestos-gcc`, etc.) and `sysroot/`.
2. Copy those directories into this folder so it has the following structure:

```
forestos-toolchain/
  install/
  sysroot/
```

3. Re-run `make` (or `make toolchain-setup` if you have your own automation).

Because the folder is gitignored, none of the compiled binaries will be added to
future commits, but the build system will still look for them under
`forestos-toolchain/` by default. Override `FORESTOS_TOOLCHAIN_DIR` if you keep
it elsewhere.
