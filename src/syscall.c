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
#include "include/auth.h"

#if ARCH_64BIT
extern void syscall_handler(void);  // From interrupt_stubs.s
typedef uint64 sys_arg_t;
#else
extern void isr128(void);  // Assembly stub in syscall_stubs.asm
typedef uint32 sys_arg_t;
#endif

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
typedef int32 (*syscall_func_t)(sys_arg_t arg1, sys_arg_t arg2, sys_arg_t arg3,
                                sys_arg_t arg4, sys_arg_t arg5, sys_arg_t arg6);

typedef struct {
    syscall_func_t func;
    bool implemented;
} syscall_entry_t;

static syscall_entry_t syscall_table[SYS_MAX];
static bool syscall_warned[SYS_MAX];

// Forward declarations
static int32 sys_dup(sys_arg_t fd);
static int32 sys_dup2(sys_arg_t oldfd, sys_arg_t newfd);

static int32 sys_unimplemented(sys_arg_t arg1, sys_arg_t arg2, sys_arg_t arg3,
                               sys_arg_t arg4, sys_arg_t arg5, sys_arg_t arg6) {
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

static int32 sys_write(sys_arg_t fd, sys_arg_t buf_ptr, sys_arg_t len) {
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

static int32 sys_read(sys_arg_t fd, sys_arg_t buf_ptr, sys_arg_t len) {
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

static int32 sys_open(sys_arg_t path_ptr, sys_arg_t flags, sys_arg_t mode) {
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

static int32 sys_close(sys_arg_t fd) {
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

static int32 sys_lseek(sys_arg_t fd, sys_arg_t offset, sys_arg_t whence) {
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

static int32 sys_time(sys_arg_t user_ptr) {
    fake_unix_epoch++;
    if (user_ptr) {
        uint32* t = (uint32*)user_ptr;
        *t = fake_unix_epoch;
    }
    return (int32)fake_unix_epoch;
}

static int32 sys_brk(sys_arg_t new_break) {
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

static int32 sys_nanosleep(sys_arg_t req_ptr, sys_arg_t rem_ptr) {
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

static int32 sys_uname(sys_arg_t user_ptr) {
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

static int32 sys_exit(sys_arg_t code) {
    // Gracefully terminate the current task and switch away.
    task_exit((int)code, "exit()");
    return 0; // Not reached
}

static int32 sys_socket(sys_arg_t domain, sys_arg_t type, sys_arg_t protocol) {
    return net_socket_create(domain, type, protocol);
}

static int32 sys_bind(sys_arg_t fd, sys_arg_t addr_ptr, sys_arg_t addr_len) {
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

static int32 sys_sendto(sys_arg_t fd, sys_arg_t buf_ptr, sys_arg_t len, sys_arg_t flags,
                        sys_arg_t addr_ptr, sys_arg_t addr_len) {
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

static int32 sys_recvfrom(sys_arg_t fd, sys_arg_t buf_ptr, sys_arg_t len, sys_arg_t flags,
                          sys_arg_t addr_ptr, sys_arg_t addr_len_ptr) {
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

static int32 sys_netinfo(sys_arg_t buffer_ptr, sys_arg_t max_entries) {
    if (!buffer_ptr || max_entries == 0) {
        return SYSCALL_EINVAL;
    }
    return (int32)net_snapshot((net_socket_info_t*)buffer_ptr, max_entries);
}

// File system operations
static int32 sys_stat(sys_arg_t path_ptr, sys_arg_t stat_ptr) {
    // Basic stat implementation - all files report as regular files
    if (!path_ptr || !stat_ptr) {
        return SYSCALL_EFAULT;
    }
    
    const char* path = (const char*)path_ptr;
    const uint8* fdata = 0;
    uint32 fsize = 0;
    if (!vfs_read_file(path, &fdata, &fsize)) {
        return SYSCALL_ENOENT;
    }
    (void)fdata; // Data not needed beyond existence/size
    
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
    stat_buf->st_size = fsize;
    stat_buf->st_blksize = 4096;
    stat_buf->st_blocks = (fsize + stat_buf->st_blksize - 1) / stat_buf->st_blksize;
    stat_buf->st_atime = fake_unix_epoch;
    stat_buf->st_mtime = fake_unix_epoch;
    stat_buf->st_ctime = fake_unix_epoch;
    
    return 0;
}

static int32 sys_fstat(sys_arg_t fd, sys_arg_t stat_ptr) {
    if (!stat_ptr) {
        return SYSCALL_EFAULT;
    }

    // Same structure as sys_stat
    struct {
        uint32 st_dev, st_ino, st_mode, st_nlink;
        uint32 st_uid, st_gid, st_rdev, st_size;
        uint32 st_blksize, st_blocks;
        uint32 st_atime, st_mtime, st_ctime;
    }* stat_buf = (void*)stat_ptr;

    if (fd < 3 || net_is_fd(fd)) {
        // Treat stdio and sockets as character devices
        stat_buf->st_dev = 1;
        stat_buf->st_ino = (uint32)(fd + 1);
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

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    const vfs_handle_t* handle = &vfs_handles[slot];
    stat_buf->st_dev = 1;
    stat_buf->st_ino = (uint32)(fd + 1);
    stat_buf->st_mode = 0100644; // Regular file, rw-r--r--
    stat_buf->st_nlink = 1;
    stat_buf->st_uid = 0;
    stat_buf->st_gid = 0;
    stat_buf->st_rdev = 0;
    stat_buf->st_size = handle->size;
    stat_buf->st_blksize = 4096;
    stat_buf->st_blocks = (handle->size + stat_buf->st_blksize - 1) / stat_buf->st_blksize;
    stat_buf->st_atime = fake_unix_epoch;
    stat_buf->st_mtime = fake_unix_epoch;
    stat_buf->st_ctime = fake_unix_epoch;

    return 0;
}

static int32 sys_access(sys_arg_t path_ptr, sys_arg_t mode) {
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }
    
    (void)mode;
    if (!vfs_read_file((const char*)path_ptr, 0, 0)) {
        return SYSCALL_ENOENT;
    }
    return 0; // Read-only filesystem; treat as accessible if present
}

// Process operations
static int32 sys_getuid(void) {
    if (current_task) {
        return (int32)current_task->uid;
    }
    return 0;
}

static int32 sys_getgid(void) {
    if (current_task) {
        return (int32)current_task->gid;
    }
    return 0;
}

static int32 sys_geteuid(void) {
    if (current_task) {
        return (int32)current_task->uid;
    }
    return 0;
}

static int32 sys_getegid(void) {
    if (current_task) {
        return (int32)current_task->gid;
    }
    return 0;
}

static int32 sys_getppid(void) {
    return 0; // Parent PID (init)
}

// I/O control and misc
static int32 sys_ioctl(sys_arg_t fd, sys_arg_t request, sys_arg_t arg) {
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

static int32 sys_fcntl(sys_arg_t fd, sys_arg_t cmd, sys_arg_t arg) {
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

static int32 sys_dup(sys_arg_t fd) {
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

static int32 sys_dup2(sys_arg_t oldfd, sys_arg_t newfd) {
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

static int32 sys_power(sys_arg_t request) {
    if (request == POWER_ACTION_REBOOT) {
        power_reboot();
    } else {
        power_shutdown();
    }

    // Should never return
    return SYSCALL_EPERM;
}

static int32 sys_user(sys_arg_t op, sys_arg_t arg2, sys_arg_t arg3, sys_arg_t arg4, sys_arg_t arg5, sys_arg_t arg6) {
    const char* user = (const char*)arg2;
    const char* pass = (const char*)arg3;
    const char* aux  = (const char*)arg4;
    auth_user_info_t* out_info = (auth_user_info_t*)arg5;
    uint32 extra = arg6;
    auth_result_t res = AUTH_ERR_INVALID;

    switch (op) {
        case USER_OP_LOGIN: {
            auth_user_info_t info = {0};
            res = auth_login(user, pass, &info);
            if (res == AUTH_OK && current_task) {
                current_task->uid = info.uid;
                current_task->gid = info.gid;
                current_task->groups_mask = info.groups_mask;
            }
            if (out_info) {
                memory_copy((const char*)&info, (char*)out_info, sizeof(auth_user_info_t));
            }
            break;
        }
        case USER_OP_SIGNUP:
            res = auth_signup(user, pass, aux, false);
            break;
        case USER_OP_PASSWD:
            res = auth_change_password(user, pass);
            break;
        case USER_OP_GROUP:
            res = auth_add_group(aux, 0);
            break;
        case USER_OP_CURRENT:
            res = auth_get_current(out_info);
            break;
        case USER_OP_LIST:
            res = auth_list(out_info, extra, (uint32*)aux);
            break;
        case USER_OP_LOGOUT:
            res = auth_logout();
            if (current_task) {
                current_task->uid = 0;
                current_task->gid = 0;
                current_task->groups_mask = 1;
            }
            break;
        default:
            return SYSCALL_EINVAL;
    }
    return (int32)res;
}

static int32 sys_unlink(sys_arg_t path_ptr) {
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }
    const char* path = (const char*)path_ptr;
    if (!vfs_read_file(path, 0, 0)) {
        return SYSCALL_ENOENT;
    }
    // Initrd is read-only, so unlink is not supported yet.
    return SYSCALL_EPERM;
}

void syscall_init(void) {
#if ARCH_64BIT
    set_idt_gate_flags(SYSCALL_VECTOR, (uintptr_t)syscall_handler, IDT_GATE_USER_INT);
    print("[SYSCALL] Installed 64-bit software interrupt handler at 0x80\n");
#else
    set_idt_gate_flags(SYSCALL_VECTOR, (uintptr_t)isr128, IDT_GATE_USER_INT);
    print("[SYSCALL] Installed software interrupt handler at 0x80\n");
#endif
}

void syscall_handle(syscall_frame_t* frame) {
    if (!frame) {
        return;
    }

    // Note the syscall as activity for watchdogs/debugging.
    task_mark_active();

#if ARCH_64BIT
    sys_arg_t num  = frame->rax;
    sys_arg_t arg1 = frame->rbx;  // First argument
    sys_arg_t arg2 = frame->rcx;  // Second argument
    sys_arg_t arg3 = frame->rdx;  // Third argument
    sys_arg_t arg4 = frame->rsi;  // Fourth argument
    sys_arg_t arg5 = frame->rdi;  // Fifth argument
    sys_arg_t arg6 = frame->rbp;  // Sixth argument
#else
    sys_arg_t num  = frame->eax;
    sys_arg_t arg1 = frame->ebx;  // First argument
    sys_arg_t arg2 = frame->ecx;  // Second argument  
    sys_arg_t arg3 = frame->edx;  // Third argument
    sys_arg_t arg4 = frame->esi;  // Fourth argument
    sys_arg_t arg5 = frame->edi;  // Fifth argument
    sys_arg_t arg6 = frame->ebp;  // Sixth argument
#endif
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
        case SYS_UNLINK:     result = sys_unlink(arg1); break;
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
        case SYS_USERCTL:    result = sys_user(arg1, arg2, arg3, arg4, arg5, arg6); break;
        
        default:             result = SYSCALL_ENOSYS; break;
    }

#if ARCH_64BIT
    frame->rax = (sys_arg_t)result;
#else
    frame->eax = (uint32)result;
#endif
}
