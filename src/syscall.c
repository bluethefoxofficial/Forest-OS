#include "include/syscall.h"
#include "include/interrupt.h"  // Use new interrupt system
#include "include/interrupt_compat.h"  // For legacy IDT functions
#include "include/screen.h"
#include "include/vfs.h"
#include "include/kb.h"
#include "include/string.h"
#include "include/util.h"
#include "include/task.h"
#include "include/net.h"
#include "include/power.h"

extern void isr128(void);  // Assembly stub in syscall_stubs.asm

#define MAX_VFS_HANDLES 16
#define USER_HEAP_BASE  0x01800000

typedef struct {
    bool used;
    const uint8* data;
    uint32 size;
    uint32 offset;
} vfs_handle_t;

static vfs_handle_t vfs_handles[MAX_VFS_HANDLES];
static uint8* program_break = (uint8*)USER_HEAP_BASE;
static uint32 fake_unix_epoch = 1730000000; // Fake epoch for time()

// Error codes (Linux compatible)
#define SYSCALL_ENOSYS  (-38)
#define SYSCALL_EBADF   (-9)
#define SYSCALL_EINVAL  (-22)
#define SYSCALL_ENOENT  (-2)
#define SYSCALL_EACCES  (-13)
#define SYSCALL_ENOMEM  (-12)
#define SYSCALL_EFAULT  (-14)
#define SYSCALL_EPERM   (-1)
#define SYSCALL_ERANGE  (-34)
#define SYSCALL_EMFILE  (-24)

// System call function pointer type
typedef int32 (*syscall_func_t)(uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5, uint32 arg6);

typedef struct {
    syscall_func_t func;
    bool implemented;
} syscall_entry_t;

static syscall_entry_t syscall_table[SYS_MAX];
static bool syscall_warned[SYS_MAX];

// Forward declarations
static int32 sys_dup(uint32 fd);
static int32 sys_dup2(uint32 oldfd, uint32 newfd);

static int32 sys_unimplemented(uint32 arg1, uint32 arg2, uint32 arg3, uint32 arg4, uint32 arg5, uint32 arg6) {
    (void)arg1;
    (void)arg2;
    (void)arg3;
    (void)arg4;
    (void)arg5;
    (void)arg6;
    return SYSCALL_ENOSYS;
}

static void syscall_register(uint32 num, syscall_func_t func) {
    if (num >= SYS_MAX) {
        return;
    }
    syscall_table[num].func = func ? func : sys_unimplemented;
    syscall_table[num].implemented = (func != 0 && func != sys_unimplemented);
}

static int32 sys_write(uint32 fd, uint32 buf_ptr, uint32 len) {
    if (!buf_ptr || len == 0) {
        return 0;
    }

    const char* buf = (const char*)buf_ptr;

    if (fd == 1 || fd == 2) {
        for (uint32 i = 0; i < len; i++) {
            printch(buf[i]);
        }
        return (int32)len;
    }

    // Only stdout/stderr supported for now.
    return SYSCALL_EBADF;
}

static int32 sys_read(uint32 fd, uint32 buf_ptr, uint32 len) {
    if (!buf_ptr || len == 0) {
        return 0;
    }

    char* dest = (char*)buf_ptr;

    if (fd == 0) {
        string line = readStr();
        if (!line) {
            return 0;
        }

        uint32 to_copy = strlength(line);
        if (to_copy > len) {
            to_copy = len;
        }
        for (uint32 i = 0; i < to_copy; i++) {
            dest[i] = line[i];
        }
        if (to_copy < len) {
            dest[to_copy] = '\0';
        }
        free(line);
        return (int32)to_copy;
    }

    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    vfs_handle_t* handle = &vfs_handles[slot];
    uint32 remaining = handle->size - handle->offset;
    if (remaining == 0) {
        return 0;
    }

    uint32 to_copy = (remaining < len) ? remaining : len;
    memory_copy((char*)handle->data + handle->offset, dest, to_copy);
    handle->offset += to_copy;
    return (int32)to_copy;
}

static int32 sys_open(uint32 path_ptr, uint32 flags, uint32 mode) {
    (void)flags;
    (void)mode;

    if (!path_ptr) {
        return SYSCALL_EINVAL;
    }

    char path_buf[128];
    const char* user_path = (const char*)path_ptr;
    uint32 i = 0;
    while (user_path[i] && i < sizeof(path_buf) - 1) {
        path_buf[i] = user_path[i];
        i++;
    }
    path_buf[i] = '\0';

    const uint8* data = 0;
    uint32 size = 0;
    if (!vfs_read_file(path_buf, &data, &size)) {
        return SYSCALL_ENOENT;
    }

    for (uint32 slot = 0; slot < MAX_VFS_HANDLES; slot++) {
        if (!vfs_handles[slot].used) {
            vfs_handles[slot].used = true;
            vfs_handles[slot].data = data;
            vfs_handles[slot].size = size;
            vfs_handles[slot].offset = 0;
            return (int32)(slot + 3);
        }
    }

    return SYSCALL_EBADF;
}

static int32 sys_close(uint32 fd) {
    if (fd < 3) {
        return 0;
    }

    if (net_is_fd(fd)) {
        return net_close(fd);
    }
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    vfs_handles[slot].used = false;
    vfs_handles[slot].data = 0;
    vfs_handles[slot].size = 0;
    vfs_handles[slot].offset = 0;
    return 0;
}

static int32 sys_lseek(uint32 fd, uint32 offset, uint32 whence) {
    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    vfs_handle_t* handle = &vfs_handles[slot];
    uint32 new_offset = handle->offset;

    switch (whence) {
        case 0: new_offset = offset; break;
        case 1: new_offset += offset; break;
        case 2: new_offset = handle->size + offset; break;
        default: return SYSCALL_EINVAL;
    }

    if (new_offset > handle->size) {
        new_offset = handle->size;
    }

    handle->offset = new_offset;
    return (int32)new_offset;
}

static int32 sys_getpid(void) {
    if (current_task) {
        return (int32)current_task->id;
    }
    return 0;
}

static int32 sys_time(uint32 user_ptr) {
    (void)user_ptr;
    fake_unix_epoch++;
    return (int32)fake_unix_epoch;
}

static int32 sys_brk(uint32 new_break) {
    if (new_break == 0) {
        return (int32)program_break;
    }

    if (new_break < USER_HEAP_BASE) {
        program_break = (uint8*)USER_HEAP_BASE;
        return (int32)program_break;
    }

    program_break = (uint8*)new_break;
    return (int32)program_break;
}

typedef struct {
    uint32 tv_sec;
    uint32 tv_nsec;
} timespec_simple_t;

static void busy_wait(uint32 iterations) {
    for (uint32 i = 0; i < iterations; i++) {
        __asm__ __volatile__("nop");
    }
}

static int32 sys_nanosleep(uint32 req_ptr, uint32 rem_ptr) {
    (void)rem_ptr;

    if (!req_ptr) {
        return SYSCALL_EINVAL;
    }

    timespec_simple_t* req = (timespec_simple_t*)req_ptr;
    uint32 loops = req->tv_sec * 100000 + req->tv_nsec / 1000;
    busy_wait(loops);
    return 0;
}

typedef struct {
    char sysname[32];
    char nodename[32];
    char release[32];
    char version[32];
    char machine[32];
} utsname_t;

static int32 sys_uname(uint32 user_ptr) {
    if (!user_ptr) {
        return SYSCALL_EINVAL;
    }

    utsname_t* info = (utsname_t*)user_ptr;
    const char* sys  = "ForestOS";
    const char* node = "forest-node";
    const char* rel  = "1.0";
    const char* ver  = "thornedge";
    const char* mach = "i386";


    memory_set((uint8*)info, 0, sizeof(utsname_t));

    for (uint32 i = 0; sys[i] && i < sizeof(info->sysname) - 1; i++) info->sysname[i] = sys[i];
    for (uint32 i = 0; node[i] && i < sizeof(info->nodename) - 1; i++) info->nodename[i] = node[i];
    for (uint32 i = 0; rel[i] && i < sizeof(info->release) - 1; i++) info->release[i] = rel[i];
    for (uint32 i = 0; ver[i] && i < sizeof(info->version) - 1; i++) info->version[i] = ver[i];
    for (uint32 i = 0; mach[i] && i < sizeof(info->machine) - 1; i++) info->machine[i] = mach[i];

    return 0;
}

static int32 sys_exit(uint32 code) {
    print("[SYSCALL] exit(");
    print_dec(code);
    print(") - halting task\n");
    while (1) {
        __asm__ __volatile__("hlt");
    }
    return 0;
}

static int32 sys_socket(uint32 domain, uint32 type, uint32 protocol) {
    return net_socket_create(domain, type, protocol);
}

static int32 sys_bind(uint32 fd, uint32 addr_ptr, uint32 addr_len) {
    if (!addr_ptr || addr_len < sizeof(sockaddr_in_t)) {
        return SYSCALL_EINVAL;
    }

    sockaddr_in_t addr;
    memory_copy((char*)addr_ptr, (char*)&addr, sizeof(addr));

    if (addr.sin_family != AF_INET) {
        return SYSCALL_EINVAL;
    }

    uint16 port = ntohs(addr.sin_port);
    return net_bind(fd, port);
}

static int32 sys_sendto(uint32 fd, uint32 buf_ptr, uint32 len, uint32 flags,
                        uint32 addr_ptr, uint32 addr_len) {
    (void)flags;

    if (!buf_ptr || len == 0) {
        return SYSCALL_EINVAL;
    }

    uint32 dest_addr = INADDR_LOOPBACK;
    uint16 dest_port = 0;

    if (addr_ptr) {
        if (addr_len < sizeof(sockaddr_in_t)) {
            return SYSCALL_EINVAL;
        }
        sockaddr_in_t dest;
        memory_copy((char*)addr_ptr, (char*)&dest, sizeof(dest));
        if (dest.sin_family != AF_INET) {
            return SYSCALL_EINVAL;
        }
        dest_addr = dest.sin_addr;
        dest_port = ntohs(dest.sin_port);
    }

    return net_send_datagram(fd, (const uint8*)buf_ptr, len, dest_addr, dest_port);
}

static int32 sys_recvfrom(uint32 fd, uint32 buf_ptr, uint32 len, uint32 flags,
                          uint32 addr_ptr, uint32 addr_len_ptr) {
    (void)flags;

    if (!buf_ptr || len == 0) {
        return SYSCALL_EINVAL;
    }

    uint32 src_addr = 0;
    uint16 src_port = 0;

    int32 received = net_recv_datagram(fd, (uint8*)buf_ptr, len, &src_addr, &src_port);
    if (received < 0) {
        return received;
    }

    if (addr_ptr && addr_len_ptr) {
        socklen_t* user_len = (socklen_t*)addr_len_ptr;
        if (*user_len < sizeof(sockaddr_in_t)) {
            return SYSCALL_EINVAL;
        }
        sockaddr_in_t* user_addr = (sockaddr_in_t*)addr_ptr;
        user_addr->sin_family = AF_INET;
        user_addr->sin_port = htons(src_port);
        user_addr->sin_addr = src_addr;
        memory_set(user_addr->sin_zero, 0, sizeof(user_addr->sin_zero));
        *user_len = sizeof(sockaddr_in_t);
    }

    return received;
}

static int32 sys_netinfo(uint32 buffer_ptr, uint32 max_entries) {
    if (!buffer_ptr || max_entries == 0) {
        return SYSCALL_EINVAL;
    }
    return (int32)net_snapshot((net_socket_info_t*)buffer_ptr, max_entries);
}

// File system operations
static int32 sys_stat(uint32 path_ptr, uint32 stat_ptr) {
    // Basic stat implementation - all files report as regular files
    if (!path_ptr || !stat_ptr) {
        return SYSCALL_EFAULT;
    }
    
    const char* path = (const char*)path_ptr;
    (void)path; // Use path for file lookup if VFS supports it
    
    // Simple stat structure (compatible with POSIX)
    struct {
        uint32 st_dev;     // Device ID
        uint32 st_ino;     // Inode number  
        uint32 st_mode;    // File mode
        uint32 st_nlink;   // Number of hard links
        uint32 st_uid;     // User ID
        uint32 st_gid;     // Group ID
        uint32 st_rdev;    // Device ID (if special file)
        uint32 st_size;    // File size
        uint32 st_blksize; // Block size
        uint32 st_blocks;  // Number of blocks
        uint32 st_atime;   // Access time
        uint32 st_mtime;   // Modify time
        uint32 st_ctime;   // Change time
    }* stat_buf = (void*)stat_ptr;
    
    // Fill with dummy values
    stat_buf->st_dev = 1;
    stat_buf->st_ino = 12345;
    stat_buf->st_mode = 0100644; // Regular file, rw-r--r--
    stat_buf->st_nlink = 1;
    stat_buf->st_uid = 0;
    stat_buf->st_gid = 0;
    stat_buf->st_rdev = 0;
    stat_buf->st_size = 1024;
    stat_buf->st_blksize = 4096;
    stat_buf->st_blocks = 1;
    stat_buf->st_atime = fake_unix_epoch;
    stat_buf->st_mtime = fake_unix_epoch;
    stat_buf->st_ctime = fake_unix_epoch;
    
    return 0;
}

static int32 sys_fstat(uint32 fd, uint32 stat_ptr) {
    if (fd > 2 || !stat_ptr) { // Only stdin, stdout, stderr
        return SYSCALL_EBADF;
    }
    
    // Same structure as sys_stat
    struct {
        uint32 st_dev, st_ino, st_mode, st_nlink;
        uint32 st_uid, st_gid, st_rdev, st_size;
        uint32 st_blksize, st_blocks;
        uint32 st_atime, st_mtime, st_ctime;
    }* stat_buf = (void*)stat_ptr;
    
    // TTY device for standard streams
    stat_buf->st_dev = 1;
    stat_buf->st_ino = fd + 1;
    stat_buf->st_mode = 0020666; // Character device, rw-rw-rw-
    stat_buf->st_nlink = 1;
    stat_buf->st_uid = 0;
    stat_buf->st_gid = 0;
    stat_buf->st_rdev = 1;
    stat_buf->st_size = 0;
    stat_buf->st_blksize = 1;
    stat_buf->st_blocks = 0;
    stat_buf->st_atime = fake_unix_epoch;
    stat_buf->st_mtime = fake_unix_epoch;
    stat_buf->st_ctime = fake_unix_epoch;
    
    return 0;
}

static int32 sys_access(uint32 path_ptr, uint32 mode) {
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }
    
    // For now, assume all files exist and are accessible
    (void)mode;
    return 0;
}

// Process operations
static int32 sys_getuid(void) {
    return 0; // Root user
}

static int32 sys_getgid(void) {
    return 0; // Root group
}

static int32 sys_geteuid(void) {
    return 0; // Root user (effective)
}

static int32 sys_getegid(void) {
    return 0; // Root group (effective)
}

static int32 sys_getppid(void) {
    return 0; // Parent PID (init)
}

// I/O control and misc
static int32 sys_ioctl(uint32 fd, uint32 request, uint32 arg) {
    if (fd > 2) {
        return SYSCALL_EBADF;
    }
    
    // Basic terminal ioctl support
    switch (request) {
        case 0x5401: // TCGETS - get terminal attributes
            if (!arg) return SYSCALL_EFAULT;
            // Fill with dummy terminal attributes
            memory_set((uint8*)arg, 0, 60); // sizeof(struct termios)
            return 0;
            
        case 0x5402: // TCSETS - set terminal attributes  
            return 0; // Pretend success
            
        default:
            return SYSCALL_ENOSYS;
    }
}

static int32 sys_fcntl(uint32 fd, uint32 cmd, uint32 arg) {
    if (fd > MAX_VFS_HANDLES) {
        return SYSCALL_EBADF;
    }
    
    switch (cmd) {
        case 0: // F_DUPFD
            return sys_dup(fd);
            
        case 1: // F_GETFD  
            return 0; // No flags set
            
        case 2: // F_SETFD
            return 0; // Pretend success
            
        case 3: // F_GETFL
            return 0; // No flags
            
        case 4: // F_SETFL
            (void)arg;
            return 0; // Pretend success
            
        default:
            return SYSCALL_ENOSYS;
    }
}

static int32 sys_dup(uint32 fd) {
    if (fd > MAX_VFS_HANDLES) {
        return SYSCALL_EBADF;
    }
    
    // Find next available descriptor
    for (int i = 0; i < MAX_VFS_HANDLES; i++) {
        if (!vfs_handles[i].used) {
            vfs_handles[i] = vfs_handles[fd]; // Copy the handle
            return i;
        }
    }
    
    return SYSCALL_EMFILE; // Too many open files
}

static int32 sys_dup2(uint32 oldfd, uint32 newfd) {
    if (oldfd > MAX_VFS_HANDLES || newfd > MAX_VFS_HANDLES) {
        return SYSCALL_EBADF;
    }
    
    if (oldfd == newfd) {
        return newfd;
    }
    
    // Close newfd if it's open
    if (vfs_handles[newfd].used) {
        vfs_handles[newfd].used = false;
    }
    
    // Copy oldfd to newfd
    vfs_handles[newfd] = vfs_handles[oldfd];
    return newfd;
}

static int32 sys_power(uint32 request) {
    if (request == POWER_ACTION_REBOOT) {
        power_reboot();
    } else {
        power_shutdown();
    }

    // Should never return
    return SYSCALL_EPERM;
}

void syscall_init(void) {
    set_idt_gate_flags(SYSCALL_VECTOR, (uint32)isr128, 0xEE);
    print("[SYSCALL] Installed software interrupt handler at 0x80\n");
}

void syscall_handle(syscall_frame_t* frame) {
    if (!frame) {
        return;
    }

    uint32 num = frame->eax;
    uint32 arg1 = frame->ebx;  // First argument
    uint32 arg2 = frame->ecx;  // Second argument  
    uint32 arg3 = frame->edx;  // Third argument
    uint32 arg4 = frame->esi;  // Fourth argument
    uint32 arg5 = frame->edi;  // Fifth argument
    uint32 arg6 = frame->ebp;  // Sixth argument
    int32 result = SYSCALL_ENOSYS;

    switch (num) {
        case SYS_WRITE:      result = sys_write(arg1, arg2, arg3); break;
        case SYS_READ:       result = sys_read(arg1, arg2, arg3); break;
        case SYS_OPEN:       result = sys_open(arg1, arg2, arg3); break;
        case SYS_CLOSE:      result = sys_close(arg1); break;
        case SYS_LSEEK:      result = sys_lseek(arg1, arg2, arg3); break;
        case SYS_GETPID:     result = sys_getpid(); break;
        case SYS_TIME:       result = sys_time(arg1); break;
        case SYS_BRK:        result = sys_brk(arg1); break;
        case SYS_NANOSLEEP:  result = sys_nanosleep(arg1, arg2); break;
        case SYS_UNAME:      result = sys_uname(arg1); break;
        case SYS_EXIT:       result = sys_exit(arg1); break;
        case SYS_EXIT_GROUP: result = sys_exit(arg1); break;  // Same as exit for now
        case SYS_SOCKET:     result = sys_socket(arg1, arg2, arg3); break;
        case SYS_BIND:       result = sys_bind(arg1, arg2, arg3); break;
        case SYS_SENDTO:     result = sys_sendto(arg1, arg2, arg3, arg4, arg5, arg6); break;
        case SYS_RECVFROM:   result = sys_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6); break;
        case SYS_NETINFO:    result = sys_netinfo(arg1, arg2); break;
        case SYS_REBOOT:     result = sys_power(POWER_ACTION_REBOOT); break;
        case SYS_POWER:      result = sys_power(arg1); break;
        
        // File system operations
        case SYS_STAT:       result = sys_stat(arg1, arg2); break;
        case SYS_FSTAT:      result = sys_fstat(arg1, arg2); break;
        case SYS_ACCESS:     result = sys_access(arg1, arg2); break;
        
        // Process operations  
        case SYS_GETUID:     result = sys_getuid(); break;
        case SYS_GETGID:     result = sys_getgid(); break;
        case SYS_GETEUID:    result = sys_geteuid(); break;
        case SYS_GETEGID:    result = sys_getegid(); break;
        case SYS_GETPPID:    result = sys_getppid(); break;
        
        // I/O control and misc
        case SYS_IOCTL:      result = sys_ioctl(arg1, arg2, arg3); break;
        case SYS_FCNTL:      result = sys_fcntl(arg1, arg2, arg3); break;
        case SYS_DUP:        result = sys_dup(arg1); break;
        case SYS_DUP2:       result = sys_dup2(arg1, arg2); break;
        
        default:             result = SYSCALL_ENOSYS; break;
    }

    frame->eax = (uint32)result;
}
