# Forest OS Development Guides

This document collects hands-on tutorials for extending Forest OS. Each section
focuses on a concrete workflow that regularly comes up while hacking on the
kernel or userland.

## Tutorial: Writing a Forest Device Driver

Forest's driver manager (`src/driver.c`, header in `src/include/driver.h`)
provides a lightweight registration and event system for hardware and virtual
devices. Drivers advertise a `driver_t` descriptor, run an `init` function at
boot, may emit structured events, and can cleanly shut down.

### 1. Define the public interface

Create a header in `src/include/` that describes the driver-specific APIs and
state. For example, `src/include/ps2_keyboard.h` exposes the functions user
code can call once the keyboard driver is online. Avoid leaking `driver_t`
internals from this header—export only the operations that other subsystems
need.

### 2. Choose a home for the implementation

Kernel drivers live directly in `src/` (or a subdirectory such as
`src/graphics/drivers/`). Organize files next to related hardware code so the
build system already picks them up via the glob rules in `makefile`.

### 3. Implement the driver descriptor

Every driver exposes a single `driver_t` structure. The manager fills the `id`
and `initialized` fields; you supply everything else:

```c
#include "include/driver.h"
#include "include/util.h"

typedef struct {
    // Per-driver state lives here
} loop_state_t;

static loop_state_t g_loop;

static bool loop_driver_init(driver_t* driver) {
    (void)driver;
    // Probe hardware, allocate buffers, and emit READY or FAILURE events
    driver_emit_event(driver->id, driver->driver_class,
                      DRIVER_EVENT_STATUS_READY, 0, 0);
    return true;
}

static void loop_driver_shutdown(driver_t* driver) {
    (void)driver;
    // Free buffers or power down the device
}

static driver_t g_loop_driver = {
    .name = "loopback-net",
    .driver_class = DRIVER_CLASS_NETWORK,
    .init = loop_driver_init,
    .shutdown = loop_driver_shutdown,
    .context = &g_loop,
};
```

Keep the struct in file scope just like `g_loopback_driver` in `src/net.c:114`.
Attach any state you need through `driver->context` instead of using globals
whenever possible.

### 4. Register at boot

Call `driver_register(&g_driver)` from the subsystem that owns the hardware.
`kmain` in `src/kernel.c` sets up the driver manager early, so you can register
drivers from your subsystem's `*_init()` after core services are ready (e.g.
the networking stack registers once memory and I/O helpers are online). Handle
the boolean return to print a helpful error if initialization fails.

### 5. Emit and consume events

Use `driver_emit_event()` to push asynchronous notifications (RX data, status
changes, etc.) into the global queue. Consumers can poll the queue via
`driver_event_pop()` inside their service loop. Limit payloads to
`DRIVER_EVENT_PAYLOAD_MAX` bytes and prefer small structs.

### 6. Integrate with the rest of the kernel

When your driver needs interrupts, set up the vector in `src/interrupt.c` or
reuse existing stubs. For MMIO/PIO access, rely on the helpers in
`src/system.c`. Tie userspace exposure into the syscall layer or the VFS by
reusing the patterns already in `src/syscall.c` and `src/vfs.c`.

### 7. Test the driver

Rebuild and boot QEMU with `./buildandrun.sh` (equivalent to `make clean && make
&& make build && make run`). Watch the boot log for the `[ OK ] Driver core`
line and any `print()` calls inside your driver. Add a quick userspace probe if
the driver exposes syscalls or files so regressions surface immediately.

## Tutorial: Adding a New Syscall

Forest mirrors Linux's syscall numbers so the toolchain can execute binaries on
a Linux host during development. Use this process to wire up a new handler:

1. **Pick the number.** `src/include/syscall.h` already lists the Linux x86_64
   values. Reuse the official number (e.g. `SYS_GETTIMEOFDAY`) so user programs
   stay source-compatible.
2. **Declare the prototype.** If the handler needs helpers, add their forward
   declarations in the appropriate `src/include/*.h`.
3. **Implement the kernel-side body.** Add a `static int32 sys_foo(...)`
   function to `src/syscall.c`. Follow the style used by existing handlers for
   validation and error returns (negative errno values such as `-EINVAL`).
4. **Register the handler.** Inside `syscall_init()` call `syscall_register`
   with the Linux number and your function pointer so `isr128` can dispatch it.
5. **Expose userland stubs.** Add the prototype to
   `libs/libc/include/unistd.h` or the relevant libc header, and wire it to the
   `int 0x80` wrapper used by the other functions in `userspace/libc`.
6. **Document the semantics.** Update `docs/SYSCALLS.md` with the behavior,
   error codes, and any Forest-specific differences to keep the reference
   complete.
7. **Write a regression test.** A quick CLI utility in `userspace/` that calls
   the syscall and prints the result is often enough (mirror `userspace/time.c`
   or `userspace/uname.c` for examples).

## Tutorial: Shipping a Userspace CLI Tool

Forest ships a busybox-style collection of CLI programs compiled with the
cross-toolchain noted in `makefile`. To add another one:

1. **Create the source file.** Drop `<tool>.c` into the `userspace/` directory.
   Include headers from `src/include/libc/` (see `userspace/hello.c`) or from
   `userspace/libc/` for POSIX-style wrappers.
2. **Use the provided CRT and libc.** Linker scripts (`userspace/link.ld`) and
   the startup stub (`userspace/crt0.S`) are already wired into the build. Just
   implement `int main(int argc, char** argv)` and call libc helpers such as
   `printf`, `read`, or your new syscall wrapper.
3. **Build everything.** Running `make build` or the `buildandrun.sh` helper
   re-invokes the Forest toolchain (`i686-forestos-gcc`) on every file under
   `userspace/`, links ELFs at `0x40001000`, and copies them into
   `initrd/bin/<tool>.elf`.
4. **Test inside the shell.** Boot the OS, drop into the built-in shell, and
   execute your new binary (`/bin/<tool>.elf`). Keep logging messages short
   because stdout maps to the kernel console via `sys_write`.
5. **Optional: add manual entries.** If the tool needs documentation, update
   `userspace/doc.c` (Forest's manpage viewer) or start another Markdown file
   under `docs/` that the tool can display with `cat`.

Following these recipes keeps kernel code organized, preserves Linux
compatibility where expected, and ensures new features can be tested quickly in
both kernel and userland contexts.
