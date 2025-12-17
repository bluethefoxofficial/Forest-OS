# Forest OS Syscall Interface

This document records Forest OS' syscall surface and how it maps to the Linux
expectation we use when compiling user programs. Linux-compatible numbers and
signatures are preserved to keep binaries runnable on a Linux host for
development, but Forest-specific semantics and gaps are called out below.

## Calling convention

* Syscalls are issued with `int 0x80`.
* Register layout matches Linux i386: `EAX` = syscall number, `EBX` = arg1,
  `ECX` = arg2, `EDX` = arg3, `ESI` = arg4, `EDI` = arg5, `EBP` = arg6.
* The C handler is reached through `isr128`, which saves all general registers
  before invoking `syscall_handle` and returns via `iret`/`iretd`.
* Return values come back in `EAX` without errno translation; negative numbers
  are Linux-style `-errno` codes.

## Errno mapping

Forest returns Linux-compatible negative errno values for both implemented and
unimplemented calls:

* `-ENOSYS` (`-38`) for any syscall not wired up or not recognized.
* `-EBADF` (`-9`) for invalid file/socket descriptors.
* `-EINVAL` (`-22`) for malformed inputs.
* `-ENOENT` (`-2`) for missing VFS files.
* `-EACCES` (`-13`) for permission failures (currently unused in handlers).
* `-ENOMEM` (`-12`) for allocation failures (unused today).
* `-EFAULT` (`-14`) when a user pointer is missing.
* `-EPERM` (`-1`) for unsupported privileged operations (unused today).
* `-ERANGE` (`-34`) for range errors (unused today).

## Implemented syscalls

Only a small subset of the Linux table is hooked up in `syscall_handle`.
Numbers follow the Linux x86_64 list in `include/syscall.h`, even though Forest
runs in 32-bit mode.

### File/VFS primitives

| Number | Name  | Forest semantics |
| --- | --- | --- |
| 0 | `read` | `fd=0` reads a line from the console; other descriptors map to VFS handles opened by `open`. Returns bytes copied or `-EBADF`. |
| 1 | `write` | Only `stdout`/`stderr` (`fd=1/2`) are supported and print characters to the screen. Other descriptors return `-EBADF`. |
| 2 | `open` | Opens a VFS file in read-only mode, copying the provided path into a fixed buffer. Returns descriptors starting at 3 or `-ENOENT`/`-EBADF`. |
| 3 | `close` | Silently succeeds for `fd<3`. Closes VFS handles or delegates to networking via `net_close`. Invalid handles return `-EBADF`. |
| 8 | `lseek` | Adjusts offsets on VFS handles; clamps to file size and errors with `-EINVAL` on bad `whence`. |

### Process and time

| Number | Name | Forest semantics |
| --- | --- | --- |
| 39 | `getpid` | Returns the current task id or 0 if no task is active. |
| 201 | `time` | Returns an incrementing fake epoch value; ignores the user pointer. |
| 12 | `brk` | Tracks a simple program break starting at `0x01800000` without reserving physical pages. |
| 35 | `nanosleep` | Busy-waits based on the requested `timespec` (microsecond-scale loops); ignores `rem`. |
| 63 | `uname` | Fills a `struct utsname` with constant Forest strings and the `i386` machine tag. |
| 60 | `exit` | Logs the code then halts the CPU in a tight loop (no task teardown). |

### Networking (UDP-style)

| Number | Name | Forest semantics |
| --- | --- | --- |
| 41 | `socket` | Calls `net_socket_create(domain, type, protocol)`. |
| 49 | `bind` | Requires `sockaddr_in`, validates `AF_INET`, and binds to the specified port. |
| 44 | `sendto` | Optional `sockaddr_in` destination; defaults to loopback if omitted. Uses `net_send_datagram`. |
| 45 | `recvfrom` | Uses `net_recv_datagram`; optionally writes back a `sockaddr_in` and length. |

## Unimplemented syscalls

Every other Linux number currently routes to the default handler and returns
`-ENOSYS`. Keep Linux-compatible stubs in userland when porting applications so
that binaries can still run on a Linux host for development.

## Compatibility notes and required stubs

* Preserve Linux prototypes and call numbers when adding Forest implementations
  to keep host execution viable.
* Keep user libraries prepared to handle `-ENOSYS` gracefully; many POSIX entry
  points will need no-op stubs when used outside the implemented set.
* Time- and sleep-related calls are synthetic (busy-wait, fake epoch) and may
  run faster or slower than real time; do not rely on wall-clock accuracy.
* `exit` halts the machine; prefer to guard calls in tests running on Linux.
