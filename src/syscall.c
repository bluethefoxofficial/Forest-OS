#include "include/syscall.h"
#include "include/interrupt.h"  // Use new interrupt system
#include "include/interrupt_compat.h"  // For legacy IDT functions
#include "include/screen.h"
#include "include/vfs.h"
#include "include/ramdisk.h"
#include "include/kb.h"
#include "include/string.h"
#include "include/util.h"
#include "include/task.h"
#include "include/net.h"
#include "include/power.h"
#include "include/device_fs.h"
#include "include/auth.h"
#include "include/memory_safe.h"
#include "include/memory.h"
#include "include/smep_smap.h"
#include "include/framebuffer.h"
#include "include/debuglog.h"
#include "include/graphics/graphics_manager.h"
#include "include/devfs.h"
#include "include/input_event.h"
#include "include/input_ring.h"
#include "include/sound.h"
#include "include/ioctl.h"
#include "include/timer.h"
#include "include/libc/stdlib.h"
#include "include/libc/stdio.h"
#include "include/tty.h"

 // Time structures are defined in syscall.h

// Forward declarations for framebuffer syscalls
extern long sys_mmap_fb(void);
extern long sys_munmap_fb(void* addr);
extern long sys_get_fb_info(fb_info_t* user_info);
extern long sys_start_fb_watcher(void);
extern long sys_stop_fb_watcher(void);
extern long sys_fb_flush(void);

// Input event reading syscalls
static long sys_read_kbd_event(void* user_event);
static long sys_read_mouse_event(void* user_event);
static long sys_poll_input(void);

#if ARCH_64BIT
extern void syscall_handler(void);  // From interrupt_stubs.s
typedef uint64 sys_arg_t;
#else
extern void isr128(void);  // Assembly stub in syscall_stubs.asm
typedef uint32 sys_arg_t;
#endif

#define MAX_VFS_HANDLES 16
#define MAX_PIPES 16
#define PIPE_BUFFER_SIZE 4096
#define MAX_UNIX_SOCKETPAIRS 8
#define UNIX_SOCKET_BUFFER_SIZE 4096
#define MAX_PTYS 16
#define PTY_BUFFER_SIZE 4096
#define MAX_UNIX_PATH_SOCKETS 16
#define MAX_UNIX_PATH_CONNECTIONS 16
#define MAX_UNIX_PATH_PENDING 8
#define UNIX_PATH_SOCKET_BUFFER_SIZE 4096
#define MAX_SHM_SEGMENTS 32
#define MAX_SHM_ATTACHMENTS 64
#define EXECVE_USER_STACK_PAGES 4

typedef struct {
    bool used;
    const uint8* data;
    uint32 size;
    uint32 offset;
} vfs_handle_t;

typedef struct {
    bool used;
    vfs_node_t* node;
} vfs_device_handle_t;

static vfs_handle_t vfs_handles[MAX_VFS_HANDLES];
static vfs_device_handle_t vfs_device_handles[MAX_VFS_HANDLES];

typedef struct {
    bool used;
    uint8 buffer[PIPE_BUFFER_SIZE];
    uint32 read_pos;
    uint32 write_pos;
    int read_fd;
    int write_fd;
    bool read_closed;
    bool write_closed;
} pipe_t;

typedef struct {
    bool used;
    int fd_a;
    int fd_b;
    uint8 a_to_b[UNIX_SOCKET_BUFFER_SIZE];
    uint32 a_to_b_read_pos;
    uint32 a_to_b_write_pos;
    uint8 b_to_a[UNIX_SOCKET_BUFFER_SIZE];
    uint32 b_to_a_read_pos;
    uint32 b_to_a_write_pos;
    bool a_closed;
    bool b_closed;
} unix_socketpair_t;

typedef struct {
    bool used;
    int index;
    int master_fd;
    int slave_fd;
    bool master_open;
    bool slave_open;
    bool slave_locked;
    uint8 master_to_slave[PTY_BUFFER_SIZE];
    uint32 m2s_read_pos;
    uint32 m2s_write_pos;
    uint8 slave_to_master[PTY_BUFFER_SIZE];
    uint32 s2m_read_pos;
    uint32 s2m_write_pos;
    uint8 termios_blob[60];
    uint16 rows;
    uint16 cols;
} pty_t;

typedef struct {
    bool used;
    int fd_a;
    int fd_b;
    bool a_closed;
    bool b_closed;
    uint8 a_to_b[UNIX_PATH_SOCKET_BUFFER_SIZE];
    uint32 a2b_read_pos;
    uint32 a2b_write_pos;
    uint8 b_to_a[UNIX_PATH_SOCKET_BUFFER_SIZE];
    uint32 b2a_read_pos;
    uint32 b2a_write_pos;
} unix_path_connection_t;

typedef struct {
    bool used;
    int fd;
    int type;
    bool bound;
    bool listening;
    bool connected;
    int connection_id;
    char path[108];
    char peer_path[108];
    int pending_conn_ids[MAX_UNIX_PATH_PENDING];
    uint8 pending_head;
    uint8 pending_tail;
    uint8 pending_count;
} unix_path_socket_t;

typedef struct {
    bool used;
    int32 key;
    int32 shmid;
    uint32 size;
    uint8* data;
    bool marked_for_delete;
    uint32 attach_count;
} shm_segment_t;

typedef struct {
    bool used;
    uint32 task_id;
    int32 shmid;
    uint32 addr;
    uint32 size;
} shm_attachment_t;

static vfs_handle_t vfs_handles[MAX_VFS_HANDLES];
static pipe_t pipes[MAX_PIPES];
static unix_socketpair_t unix_socketpairs[MAX_UNIX_SOCKETPAIRS];
static pty_t ptys[MAX_PTYS];
static unix_path_socket_t unix_path_sockets[MAX_UNIX_PATH_SOCKETS];
static unix_path_connection_t unix_path_connections[MAX_UNIX_PATH_CONNECTIONS];
static shm_segment_t shm_segments[MAX_SHM_SEGMENTS];
static shm_attachment_t shm_attachments[MAX_SHM_ATTACHMENTS];
static uint32 fake_unix_epoch = 1730000000; // Fake epoch for time()

static inline task_t* current_user_task_ptr(void) {
    if (current_task && current_task->page_directory && current_task->elf_info.valid) {
        return current_task;
    }
    return NULL;
}

static inline uint32 heap_align_up(uint32 value) {
    return memory_align_up(value, MEMORY_PAGE_SIZE);
}

static inline uint32 heap_align_down(uint32 value) {
    return memory_align_down(value, MEMORY_PAGE_SIZE);
}

static void unmap_user_range(page_directory_t* dir, uint32 start, uint32 end);

static bool map_user_range(page_directory_t* dir, uint32 start, uint32 end) {
    if (!dir || start >= end) {
        return false;
    }

    uint32 aligned_start = heap_align_down(start);
    uint32 aligned_end = heap_align_up(end);
    uint32 mapped_end = aligned_start;

    for (uint32 va = aligned_start; va < aligned_end; va += MEMORY_PAGE_SIZE) {
        uint32 frame = pmm_alloc_frame();
        if (!frame) {
            unmap_user_range(dir, aligned_start, mapped_end);
            return false;
        }

        memory_result_t res = vmm_map_page(dir,
                                           va,
                                           frame,
                                           PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
        if (res == MEMORY_ERROR_ALREADY_MAPPED) {
            pmm_free_frame(frame);
            mapped_end = va + MEMORY_PAGE_SIZE;
            continue;
        }

        if (res != MEMORY_OK) {
            pmm_free_frame(frame);
            unmap_user_range(dir, aligned_start, mapped_end);
            return false;
        }
        mapped_end = va + MEMORY_PAGE_SIZE;
    }

    return true;
}

static void unmap_user_range(page_directory_t* dir, uint32 start, uint32 end) {
    if (!dir || start >= end) {
        return;
    }

    uint32 aligned_start = heap_align_down(start);
    uint32 aligned_end = heap_align_up(end);

    for (uint32 va = aligned_start; va < aligned_end; va += MEMORY_PAGE_SIZE) {
        uint32 phys = vmm_get_physical_addr(dir, va);
        vmm_unmap_page(dir, va);
        if (phys) {
            pmm_free_frame(phys);
        }
    }
}

typedef struct {
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
} stat_stub_t;

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
#define SYSCALL_ENFILE  (-23)
#define SYSCALL_ENOEXEC (-8)
#define SYSCALL_ECHILD  (-10)
#define SYSCALL_ESRCH   (-3)
#define SYSCALL_ENOTDIR (-20)
#define SYSCALL_ENOTTY  (-25)
#define SYSCALL_ENODEV  (-19)
#define SYSCALL_EIO     (-5)
#define SYSCALL_EPIPE   (-32)
#define SYSCALL_EBUSY   (-16)
#define SYSCALL_EAGAIN  (-11)

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
static int32 sys_stat(sys_arg_t path_ptr, sys_arg_t stat_ptr);
static int32 sys_access(sys_arg_t path_ptr, sys_arg_t mode);
static int32 sys_mkdir(sys_arg_t pathname, sys_arg_t mode);
static int32 sys_unlink(sys_arg_t path_ptr);
static int32 sys_munmap(sys_arg_t addr, sys_arg_t length);
static inline uint32 pipe_available_bytes(pipe_t* pipe);
static inline uint32 pipe_free_bytes(pipe_t* pipe);
static pipe_t* find_pipe_by_fd(int fd, bool* is_write_end);
static inline uint32 ring_available(uint32 read_pos, uint32 write_pos, uint32 capacity);
static inline uint32 ring_free(uint32 read_pos, uint32 write_pos, uint32 capacity);
static unix_socketpair_t* find_unix_socketpair_by_fd(int fd, bool* is_a_side);
static pty_t* find_pty_by_fd(int fd, bool* is_master);
static pty_t* find_pty_by_index(int index);
static unix_path_socket_t* find_unix_path_socket_by_fd(int fd);
static unix_path_socket_t* find_unix_path_listener_by_path(const char* path);
static int alloc_local_fd_slot(void);
static void free_local_fd_slot(int fd);
static int alloc_unix_path_connection(void);
static int unix_path_enqueue_pending(unix_path_socket_t* sock, int conn_id);
static int unix_path_dequeue_pending(unix_path_socket_t* sock);
static int32 unix_path_send(int fd, const uint8* data, uint32 len);
static int32 unix_path_recv(int fd, uint8* data, uint32 len);
static shm_segment_t* find_shm_segment_by_id(int32 shmid);
static shm_segment_t* find_shm_segment_by_key(int32 key);
static shm_attachment_t* find_shm_attachment(uint32 task_id, uint32 addr);
static int32 sys_shmget(sys_arg_t key, sys_arg_t size, sys_arg_t shmflg);
static int32 sys_shmat(sys_arg_t shmid, sys_arg_t shmaddr, sys_arg_t shmflg);
static int32 sys_shmctl(sys_arg_t shmid, sys_arg_t cmd, sys_arg_t buf);
static int32 sys_shmdt(sys_arg_t shmaddr);

static bool user_buffer_readable(const void* ptr, size_t len) {
    if (!ptr || len == 0) {
        return false;
    }
    return memory_probe_user_buffer(ptr, len) >= len;
}

static bool user_buffer_writable(void* ptr, size_t len) {
    if (!ptr || len == 0) {
        return false;
    }
    return memory_probe_user_buffer(ptr, len) >= len;
}

static bool user_copy_string(char* dst, size_t dst_size, const char* src) {
    if (!dst || !src || dst_size == 0) {
        return false;
    }

    size_t span = memory_probe_user_buffer(src, dst_size);
    if (span == 0) {
        return false;
    }

    size_t limit = (span < dst_size) ? span : dst_size;
    size_t i = 0;
    for (; i + 1 < limit; i++) {
        dst[i] = src[i];
        if (src[i] == '\0') {
            return true;
        }
    }

    dst[limit - 1] = '\0';
    return src[i] == '\0';
}

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

    if (!user_buffer_readable((const void*)buf_ptr, len)) {
        return SYSCALL_EFAULT;
    }

    const char* buf = (const char*)buf_ptr;

    if (fd == 1 || fd == 2) {
        for (uint32 i = 0; i < len; i++) {
            if (debuglog_is_ready()) {
                debuglog_write_char(buf[i]);
            }
        }

        return (int32)len;
    }

    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    bool pipe_is_write_end = false;
    pipe_t* pipe = find_pipe_by_fd((int)fd, &pipe_is_write_end);
    if (pipe) {
        if (!pipe_is_write_end) {
            return SYSCALL_EBADF;
        }
        if (pipe->read_closed) {
            return SYSCALL_EPIPE;
        }

        uint32 free_space = pipe_free_bytes(pipe);
        if (free_space == 0) {
            return 0;
        }

        uint32 to_write = (len < free_space) ? len : free_space;
        for (uint32 i = 0; i < to_write; i++) {
            pipe->buffer[pipe->write_pos] = (uint8)buf[i];
            pipe->write_pos = (pipe->write_pos + 1) % PIPE_BUFFER_SIZE;
        }
        return (int32)to_write;
    }

    bool unix_is_a_side = false;
    unix_socketpair_t* unix_pair = find_unix_socketpair_by_fd((int)fd, &unix_is_a_side);
    if (unix_pair) {
        uint8* out_buf = unix_is_a_side ? unix_pair->a_to_b : unix_pair->b_to_a;
        uint32* out_read_pos = unix_is_a_side ? &unix_pair->a_to_b_read_pos : &unix_pair->b_to_a_read_pos;
        uint32* out_write_pos = unix_is_a_side ? &unix_pair->a_to_b_write_pos : &unix_pair->b_to_a_write_pos;
        bool peer_closed = unix_is_a_side ? unix_pair->b_closed : unix_pair->a_closed;

        if (peer_closed) {
            return SYSCALL_EPIPE;
        }

        uint32 free_space = ring_free(*out_read_pos, *out_write_pos, UNIX_SOCKET_BUFFER_SIZE);
        if (free_space == 0) {
            return 0;
        }

        uint32 to_write = (len < free_space) ? len : free_space;
        for (uint32 i = 0; i < to_write; i++) {
            out_buf[*out_write_pos] = (uint8)buf[i];
            *out_write_pos = (*out_write_pos + 1) % UNIX_SOCKET_BUFFER_SIZE;
        }
        return (int32)to_write;
    }

    bool pty_is_master = false;
    pty_t* pty = find_pty_by_fd((int)fd, &pty_is_master);
    if (pty) {
        uint8* out_buf = pty_is_master ? pty->master_to_slave : pty->slave_to_master;
        uint32* out_read_pos = pty_is_master ? &pty->m2s_read_pos : &pty->s2m_read_pos;
        uint32* out_write_pos = pty_is_master ? &pty->m2s_write_pos : &pty->s2m_write_pos;
        bool peer_open = pty_is_master ? pty->slave_open : pty->master_open;
        if (!peer_open) {
            return SYSCALL_EPIPE;
        }

        uint32 free_space = ring_free(*out_read_pos, *out_write_pos, PTY_BUFFER_SIZE);
        if (free_space == 0) {
            return 0;
        }
        uint32 to_write = (len < free_space) ? len : free_space;
        for (uint32 i = 0; i < to_write; i++) {
            out_buf[*out_write_pos] = (uint8)buf[i];
            *out_write_pos = (*out_write_pos + 1) % PTY_BUFFER_SIZE;
        }
        return (int32)to_write;
    }

    uint32 slot = (uint32)fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    if (vfs_device_handles[slot].used && vfs_device_handles[slot].node) {
        vfs_node_t* node = vfs_device_handles[slot].node;
        uint32 bytes_written = vfs_write(node, vfs_handles[slot].offset, len, (uint8*)buf);
        if (bytes_written > 0) {
            vfs_handles[slot].offset += bytes_written;
        }
        return (int32)bytes_written;
    }

    // Initrd-backed regular files are currently read-only.
    return SYSCALL_EPERM;
}

static int32 sys_read(sys_arg_t fd, sys_arg_t buf_ptr, sys_arg_t len) {
    if (!buf_ptr || len == 0) {
        return 0;
    }

    if (!user_buffer_writable((void*)buf_ptr, len)) {
        return SYSCALL_EFAULT;
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

    bool pipe_is_write_end = false;
    pipe_t* pipe = find_pipe_by_fd((int)fd, &pipe_is_write_end);
    if (pipe) {
        if (pipe_is_write_end) {
            return SYSCALL_EBADF;
        }

        uint32 available = pipe_available_bytes(pipe);
        if (available == 0) {
            return 0;
        }

        uint32 to_read = (len < available) ? len : available;
        for (uint32 i = 0; i < to_read; i++) {
            dest[i] = (char)pipe->buffer[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % PIPE_BUFFER_SIZE;
        }
        return (int32)to_read;
    }

    bool unix_is_a_side = false;
    unix_socketpair_t* unix_pair = find_unix_socketpair_by_fd((int)fd, &unix_is_a_side);
    if (unix_pair) {
        uint8* in_buf = unix_is_a_side ? unix_pair->b_to_a : unix_pair->a_to_b;
        uint32* in_read_pos = unix_is_a_side ? &unix_pair->b_to_a_read_pos : &unix_pair->a_to_b_read_pos;
        uint32* in_write_pos = unix_is_a_side ? &unix_pair->b_to_a_write_pos : &unix_pair->a_to_b_write_pos;

        uint32 available = ring_available(*in_read_pos, *in_write_pos, UNIX_SOCKET_BUFFER_SIZE);
        if (available == 0) {
            return 0;
        }

        uint32 to_read = (len < available) ? len : available;
        for (uint32 i = 0; i < to_read; i++) {
            dest[i] = (char)in_buf[*in_read_pos];
            *in_read_pos = (*in_read_pos + 1) % UNIX_SOCKET_BUFFER_SIZE;
        }
        return (int32)to_read;
    }

    bool pty_is_master = false;
    pty_t* pty = find_pty_by_fd((int)fd, &pty_is_master);
    if (pty) {
        uint8* in_buf = pty_is_master ? pty->slave_to_master : pty->master_to_slave;
        uint32* in_read_pos = pty_is_master ? &pty->s2m_read_pos : &pty->m2s_read_pos;
        uint32* in_write_pos = pty_is_master ? &pty->s2m_write_pos : &pty->m2s_write_pos;
        uint32 available = ring_available(*in_read_pos, *in_write_pos, PTY_BUFFER_SIZE);
        if (available == 0) {
            return 0;
        }
        uint32 to_read = (len < available) ? len : available;
        for (uint32 i = 0; i < to_read; i++) {
            dest[i] = (char)in_buf[*in_read_pos];
            *in_read_pos = (*in_read_pos + 1) % PTY_BUFFER_SIZE;
        }
        return (int32)to_read;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    if (vfs_device_handles[slot].used && vfs_device_handles[slot].node) {
        vfs_node_t* node = vfs_device_handles[slot].node;
        uint32 bytes_read = vfs_read(node, vfs_handles[slot].offset, len, (uint8_t*)dest);
        if (bytes_read > 0) {
            vfs_handles[slot].offset += bytes_read;
        }
        return (int32)bytes_read;
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
    if (!user_copy_string(path_buf, sizeof(path_buf), user_path)) {
        return SYSCALL_EFAULT;
    }

    if (strcmp(path_buf, "/dev/ptmx") == 0 || strcmp(path_buf, "dev/ptmx") == 0) {
        int pty_idx = -1;
        for (uint32 i = 0; i < MAX_PTYS; i++) {
            if (!ptys[i].used) {
                pty_idx = (int)i;
                break;
            }
        }
        if (pty_idx < 0) {
            return SYSCALL_ENFILE;
        }

        int master_slot = -1;
        for (uint32 i = 0; i < MAX_VFS_HANDLES; i++) {
            if (!vfs_handles[i].used && !vfs_device_handles[i].used) {
                master_slot = (int)i;
                break;
            }
        }
        if (master_slot < 0) {
            return SYSCALL_EMFILE;
        }

        pty_t* pty = &ptys[pty_idx];
        memory_set((uint8*)pty, 0, sizeof(*pty));
        pty->used = true;
        pty->index = pty_idx;
        pty->master_fd = master_slot + 3;
        pty->slave_fd = -1;
        pty->master_open = true;
        pty->slave_open = false;
        pty->slave_locked = true;
        pty->rows = 25;
        pty->cols = 80;

        vfs_handles[master_slot].used = true;
        vfs_handles[master_slot].data = NULL;
        vfs_handles[master_slot].size = 0;
        vfs_handles[master_slot].offset = 0;
        vfs_device_handles[master_slot].used = false;
        vfs_device_handles[master_slot].node = NULL;

        return pty->master_fd;
    }

    if (strncmp(path_buf, "/dev/pts/", 9) == 0 || strncmp(path_buf, "dev/pts/", 8) == 0) {
        const char* idx_str = (path_buf[0] == '/') ? (path_buf + 9) : (path_buf + 8);
        int idx = 0;
        if (!idx_str || !*idx_str) {
            return SYSCALL_EINVAL;
        }
        for (const char* p = idx_str; *p; p++) {
            if (*p < '0' || *p > '9') {
                return SYSCALL_EINVAL;
            }
            idx = idx * 10 + (*p - '0');
            if (idx >= MAX_PTYS) {
                return SYSCALL_ENOENT;
            }
        }
        pty_t* pty = find_pty_by_index(idx);
        if (!pty) {
            return SYSCALL_ENOENT;
        }
        if (pty->slave_locked) {
            return SYSCALL_EACCES;
        }
        if (pty->slave_open) {
            return SYSCALL_EBUSY;
        }

        int slave_slot = -1;
        for (uint32 i = 0; i < MAX_VFS_HANDLES; i++) {
            if (!vfs_handles[i].used && !vfs_device_handles[i].used) {
                slave_slot = (int)i;
                break;
            }
        }
        if (slave_slot < 0) {
            return SYSCALL_EMFILE;
        }

        pty->slave_fd = slave_slot + 3;
        pty->slave_open = true;

        vfs_handles[slave_slot].used = true;
        vfs_handles[slave_slot].data = NULL;
        vfs_handles[slave_slot].size = 0;
        vfs_handles[slave_slot].offset = 0;
        vfs_device_handles[slave_slot].used = false;
        vfs_device_handles[slave_slot].node = NULL;

        return pty->slave_fd;
    }

    vfs_node_t* node = vfs_open(path_buf, flags);
    if (!node) {
        return SYSCALL_ENOENT;
    }

    for (uint32 slot = 0; slot < MAX_VFS_HANDLES; slot++) {
        if (!vfs_handles[slot].used && !vfs_device_handles[slot].used) {
            vfs_handles[slot].used = true;
            vfs_handles[slot].offset = 0;
            
            // Extract file data and size from the VFS node
            // For initrd files, internal_data points to ramdisk_file_t
            if (node->internal_data && !(node->flags & VFS_DIRECTORY)) {
                const ramdisk_file_t* file = (const ramdisk_file_t*)node->internal_data;
                vfs_handles[slot].data = file->data;
                vfs_handles[slot].size = file->size;
            } else {
                // For directories or device nodes, use node length
                vfs_handles[slot].data = NULL;
                vfs_handles[slot].size = node->length;
            }
            
            vfs_device_handles[slot].used = true;
            vfs_device_handles[slot].node = node;
            return (int32)(slot + 3);
        }
    }

    vfs_close(node);
    return SYSCALL_EMFILE;
}

static int32 sys_close(sys_arg_t fd) {
    if (fd < 3) {
        return 0;
    }

    if (net_is_fd(fd)) {
        return net_close(fd);
    }

    bool pipe_is_write_end = false;
    pipe_t* pipe = find_pipe_by_fd((int)fd, &pipe_is_write_end);
    if (pipe) {
        if (pipe_is_write_end) {
            pipe->write_closed = true;
        } else {
            pipe->read_closed = true;
        }
        if (pipe->read_closed && pipe->write_closed) {
            memory_set(pipe->buffer, 0, PIPE_BUFFER_SIZE);
            pipe->used = false;
        }
    }

    bool unix_is_a_side = false;
    unix_socketpair_t* unix_pair = find_unix_socketpair_by_fd((int)fd, &unix_is_a_side);
    if (unix_pair) {
        if (unix_is_a_side) {
            unix_pair->a_closed = true;
        } else {
            unix_pair->b_closed = true;
        }
        if (unix_pair->a_closed && unix_pair->b_closed) {
            memory_set((uint8*)unix_pair, 0, sizeof(*unix_pair));
        }
    }

    bool pty_is_master = false;
    pty_t* pty = find_pty_by_fd((int)fd, &pty_is_master);
    if (pty) {
        if (pty_is_master) {
            pty->master_open = false;
            pty->master_fd = -1;
        } else {
            pty->slave_open = false;
            pty->slave_fd = -1;
        }
        if (!pty->master_open && !pty->slave_open) {
            memory_set((uint8*)pty, 0, sizeof(*pty));
        }
    }

    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (usock->connection_id >= 0 && usock->connection_id < MAX_UNIX_PATH_CONNECTIONS) {
            unix_path_connection_t* conn = &unix_path_connections[usock->connection_id];
            if (conn->used) {
                if (conn->fd_a == usock->fd) {
                    conn->a_closed = true;
                    conn->fd_a = -1;
                } else if (conn->fd_b == usock->fd) {
                    conn->b_closed = true;
                    conn->fd_b = -1;
                }
                if ((conn->fd_a < 0 && conn->fd_b < 0) || (conn->a_closed && conn->b_closed)) {
                    memory_set((uint8*)conn, 0, sizeof(*conn));
                }
            }
        }
        if (usock->bound && usock->path[0]) {
            for (uint32 i = 0; i < MAX_UNIX_PATH_SOCKETS; i++) {
                if (&unix_path_sockets[i] != usock &&
                    unix_path_sockets[i].used &&
                    unix_path_sockets[i].connected &&
                    strcmp(unix_path_sockets[i].peer_path, usock->path) == 0) {
                    unix_path_sockets[i].peer_path[0] = '\0';
                }
            }
        }
        memory_set((uint8*)usock, 0, sizeof(*usock));
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    vfs_handles[slot].used = false;
    if (vfs_device_handles[slot].used && vfs_device_handles[slot].node) {
        vfs_close(vfs_device_handles[slot].node);
        vfs_device_handles[slot].node = NULL;
        vfs_device_handles[slot].used = false;
    }
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

static int32 sys_getpgrp(void) {
    if (current_task) {
        return (int32)current_task->pgrp;
    }
    return 0;
}

static int32 sys_getpgid(sys_arg_t pid) {
    if (pid == 0) {
        if (current_task) {
            return (int32)current_task->pgrp;
        }
        return SYSCALL_ESRCH;
    }
    
    task_t* target_task = ready_queue_head;
    if (target_task) {
        do {
            if (target_task->id == (uint32)pid) {
                return (int32)target_task->pgrp;
            }
            target_task = target_task->next;
        } while (target_task != ready_queue_head);
    }
    
    return SYSCALL_ESRCH;
}

static int32 sys_tcgetpgrp(sys_arg_t fd) {
    if (!current_task) {
        return SYSCALL_EPERM;
    }

    // For now, we just return the current process group if fd is a terminal
    // In a real implementation, we would check if fd is a terminal and get its process group
    if (fd == 0 || fd == 1 || fd == 2) { // Standard input/output/error
        return (int32)current_task->pgrp;
    }

    return SYSCALL_ENOTTY;
}

static int32 sys_tcsetpgrp(sys_arg_t fd, sys_arg_t pgrp) {
    if (!current_task) {
        return SYSCALL_EPERM;
    }

    // For now, we just allow setting the process group if fd is a terminal
    // In a real implementation, we would check if fd is a terminal and set its process group
    if (fd == 0 || fd == 1 || fd == 2) { // Standard input/output/error
        // Check if the process group exists and is in the same session
        task_t* target_task = ready_queue_head;
        bool pgrp_exists = false;
        if (target_task) {
            do {
                if (target_task->pgrp == (uint32)pgrp && target_task->session == current_task->session) {
                    pgrp_exists = true;
                    break;
                }
                target_task = target_task->next;
            } while (target_task != ready_queue_head);
        }

        if (!pgrp_exists) {
            return SYSCALL_EINVAL;
        }

        return 0;
    }

    return SYSCALL_ENOTTY;
}

static int32 sys_setpgid(sys_arg_t pid, sys_arg_t pgid) {
    if (!current_task) {
        return SYSCALL_EPERM;
    }

    task_t* target_task = NULL;

    if (pid == 0) {
        target_task = current_task;
    } else {
        // Find the task with the given PID
        task_t* task = ready_queue_head;
        if (task) {
            do {
                if (task->id == (uint32)pid) {
                    target_task = task;
                    break;
                }
                task = task->next;
            } while (task != ready_queue_head);
        }

        if (!target_task) {
            return SYSCALL_ESRCH;
        }

        // Only the process itself or its parent can set its process group
        if (target_task != current_task && target_task->id != current_task->id) {
            return SYSCALL_EPERM;
        }
    }

    if (pgid == 0) {
        pgid = target_task->id;
    }

    // Check if pgid is valid (must refer to an existing process in the same session)
    task_t* pg_task = ready_queue_head;
    bool pgid_exists = false;
    if (pg_task) {
        do {
            if (pg_task->id == (uint32)pgid && pg_task->session == target_task->session) {
                pgid_exists = true;
                break;
            }
            pg_task = pg_task->next;
        } while (pg_task != ready_queue_head);
    }

    if (!pgid_exists) {
        return SYSCALL_EINVAL;
    }

    // Set the process group
    target_task->pgrp = (uint32)pgid;
    return 0;
}

static int32 sys_setsid(void) {
    if (!current_task) {
        return SYSCALL_EPERM;
    }

    // If process is already a process group leader, return error
    if (current_task->id == current_task->pgrp) {
        return SYSCALL_EPERM;
    }

    // Create new session and process group
    current_task->session = current_task->id;
    current_task->pgrp = current_task->id;
    current_task->tty_fd = -1;  // Lose controlling terminal

    return (int32)current_task->id;
}

static int32 sys_getsid(sys_arg_t pid) {
    task_t* target_task = NULL;

    if (pid == 0) {
        target_task = current_task;
    } else {
        // Find the task with the given PID
        task_t* task = ready_queue_head;
        if (task) {
            do {
                if (task->id == (uint32)pid) {
                    target_task = task;
                    break;
                }
                task = task->next;
            } while (task != ready_queue_head);
        }

        if (!target_task) {
            return SYSCALL_ESRCH;
        }
    }

    return (int32)target_task->session;
}

static int32 sys_time(sys_arg_t user_ptr) {
    fake_unix_epoch++;
    if (user_ptr) {
        uint32* t = (uint32*)user_ptr;
        if (!user_buffer_writable(t, sizeof(uint32))) {
            return SYSCALL_EFAULT;
        }
        *t = fake_unix_epoch;
    }
    return (int32)fake_unix_epoch;
}

static int32 sys_brk(sys_arg_t new_break) {
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_ENOMEM;
    }

    uint32 heap_base = task->user_heap_base ? task->user_heap_base : MEMORY_USER_START;
    uint32 heap_limit = task->user_heap_limit ?
                        task->user_heap_limit :
                        (USER_STACK_TOP - USER_HEAP_GUARD_PAGES * MEMORY_PAGE_SIZE);

    if (heap_base >= heap_limit) {
        return SYSCALL_ENOMEM;
    }

    if (new_break == 0) {
        uint32 current = task->user_brk ? task->user_brk : heap_base;
        return (int32)current;
    }

    uint32 requested = (uint32)new_break;
    if (requested < heap_base) {
        requested = heap_base;
    }
    if (requested > heap_limit) {
        return SYSCALL_ENOMEM;
    }

    uint32 old_brk = task->user_brk ? task->user_brk : heap_base;
    uint32 old_aligned = heap_align_up(old_brk);
    uint32 new_aligned = heap_align_up(requested);

    if (new_aligned > old_aligned) {
        if (!map_user_range(task->page_directory, old_aligned, new_aligned)) {
            return SYSCALL_ENOMEM;
        }
    } else if (new_aligned < old_aligned) {
        unmap_user_range(task->page_directory, new_aligned, old_aligned);
    }

    task->user_brk = requested;
    return (int32)task->user_brk;
}

typedef struct {
    uint32 tv_sec;
    uint32 tv_nsec;
} timespec_simple_t;

// POSIX timeval structure for gettimeofday
typedef struct {
    uint32 tv_sec;  // Seconds since epoch
    uint32 tv_usec; // Microseconds
} timeval_t;

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

    if (!user_buffer_readable((const void*)req_ptr, sizeof(timespec_simple_t))) {
        return SYSCALL_EFAULT;
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

    if (!user_buffer_writable((void*)user_ptr, sizeof(utsname_t))) {
        return SYSCALL_EFAULT;
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
    if (!current_task) {
        // Kernel task exiting - this is unusual but allowed
        return 0;
    }

   // print("[SYSCALL] sys_exit called\n");
    
    // Use the proper task_exit function which handles cleanup and never returns
    task_exit((int)code, "Normal exit");

    // Should never reach here - task_exit has an infinite halt loop as failsafe
   // print("[SYSCALL] ERROR: task_exit returned! Halting.\n");
    while (1) {
        __asm__ __volatile__("hlt");
    }
    return 0; // Unreachable, but silences compiler warning
}

static int32 sys_socket(sys_arg_t domain, sys_arg_t type, sys_arg_t protocol) {
    if (domain == AF_UNIX || domain == AF_LOCAL) {
        if (!(type == SOCK_STREAM || type == SOCK_DGRAM)) {
            return SYSCALL_EINVAL;
        }
        int fd = alloc_local_fd_slot();
        if (fd < 0) {
            return SYSCALL_EMFILE;
        }
        for (uint32 i = 0; i < MAX_UNIX_PATH_SOCKETS; i++) {
            if (!unix_path_sockets[i].used) {
                memory_set((uint8*)&unix_path_sockets[i], 0, sizeof(unix_path_socket_t));
                unix_path_sockets[i].used = true;
                unix_path_sockets[i].fd = fd;
                unix_path_sockets[i].type = (int)type;
                unix_path_sockets[i].connection_id = -1;
                return fd;
            }
        }
        free_local_fd_slot(fd);
        return SYSCALL_ENFILE;
    }
    return net_socket_create(domain, type, protocol);
}

static int32 sys_bind(sys_arg_t fd, sys_arg_t addr_ptr, sys_arg_t addr_len) {
    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (!addr_ptr || addr_len < sizeof(sockaddr_un_t)) {
            return SYSCALL_EINVAL;
        }
        if (!user_buffer_readable((const void*)addr_ptr, sizeof(sockaddr_un_t))) {
            return SYSCALL_EFAULT;
        }
        sockaddr_un_t uaddr;
        memory_copy((char*)addr_ptr, (char*)&uaddr, sizeof(uaddr));
        if (!(uaddr.sun_family == AF_UNIX || uaddr.sun_family == AF_LOCAL)) {
            return SYSCALL_EINVAL;
        }
        if (usock->bound) {
            return SYSCALL_EINVAL;
        }
        if (!uaddr.sun_path[0]) {
            return SYSCALL_EINVAL;
        }
        for (uint32 i = 0; i < MAX_UNIX_PATH_SOCKETS; i++) {
            if (unix_path_sockets[i].used && unix_path_sockets[i].bound &&
                strcmp(unix_path_sockets[i].path, uaddr.sun_path) == 0) {
                return SYSCALL_EINVAL;
            }
        }
        memory_copy((const char*)uaddr.sun_path, usock->path, sizeof(usock->path));
        usock->bound = true;
        return 0;
    }

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

    if (!user_buffer_readable((const void*)buf_ptr, len)) {
        return SYSCALL_EFAULT;
    }

    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        (void)addr_len;
        if (!usock->connected) {
            if (!addr_ptr) {
                return SYSCALL_EINVAL;
            }
            if (!user_buffer_readable((const void*)addr_ptr, sizeof(sockaddr_un_t))) {
                return SYSCALL_EFAULT;
            }
            sockaddr_un_t uaddr;
            memory_copy((char*)addr_ptr, (char*)&uaddr, sizeof(uaddr));
            if (!(uaddr.sun_family == AF_UNIX || uaddr.sun_family == AF_LOCAL)) {
                return SYSCALL_EINVAL;
            }
            unix_path_socket_t* listener = find_unix_path_listener_by_path(uaddr.sun_path);
            if (!listener) {
                return SYSCALL_ENOENT;
            }
            int conn_id = alloc_unix_path_connection();
            if (conn_id < 0) {
                return SYSCALL_ENFILE;
            }
            unix_path_connection_t* conn = &unix_path_connections[conn_id];
            conn->fd_a = usock->fd;
            conn->fd_b = -1;
            usock->connected = true;
            usock->connection_id = conn_id;
            memory_copy(listener->path, usock->peer_path, sizeof(usock->peer_path));
            if (unix_path_enqueue_pending(listener, conn_id) != 0) {
                memory_set((uint8*)conn, 0, sizeof(*conn));
                usock->connected = false;
                usock->connection_id = -1;
                return SYSCALL_EAGAIN;
            }
        }
        return unix_path_send((int)fd, (const uint8*)buf_ptr, (uint32)len);
    }

    uint32 dest_addr = INADDR_LOOPBACK;
    uint16 dest_port = 0;

    if (addr_ptr) {
        if (addr_len < sizeof(sockaddr_in_t)) {
            return SYSCALL_EINVAL;
        }
        if (!user_buffer_readable((const void*)addr_ptr, sizeof(sockaddr_in_t))) {
            return SYSCALL_EFAULT;
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

    if (!user_buffer_writable((void*)buf_ptr, len)) {
        return SYSCALL_EFAULT;
    }

    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        int32 received = unix_path_recv((int)fd, (uint8*)buf_ptr, (uint32)len);
        if (received < 0) {
            return received;
        }
        if (addr_ptr && addr_len_ptr) {
            if (!user_buffer_writable((void*)addr_len_ptr, sizeof(socklen_t))) {
                return SYSCALL_EFAULT;
            }
            socklen_t user_len = *(socklen_t*)addr_len_ptr;
            if (user_len >= sizeof(sockaddr_un_t) &&
                user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_un_t))) {
                sockaddr_un_t* u = (sockaddr_un_t*)addr_ptr;
                u->sun_family = AF_UNIX;
                memory_set((uint8*)u->sun_path, 0, sizeof(u->sun_path));
                if (usock->peer_path[0]) {
                    memory_copy((const char*)usock->peer_path, (char*)u->sun_path, sizeof(u->sun_path));
                }
                *(socklen_t*)addr_len_ptr = sizeof(sockaddr_un_t);
            }
        }
        return received;
    }

    uint32 src_addr = 0;
    uint16 src_port = 0;
    socklen_t user_len = 0;

    if (addr_len_ptr) {
        if (!user_buffer_readable((const void*)addr_len_ptr, sizeof(socklen_t)) ||
            !user_buffer_writable((void*)addr_len_ptr, sizeof(socklen_t))) {
            return SYSCALL_EFAULT;
        }
        user_len = *(socklen_t*)addr_len_ptr;
    }

    if (addr_ptr && !user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_in_t))) {
        return SYSCALL_EFAULT;
    }

    int32 received = net_recv_datagram(fd, (uint8*)buf_ptr, len, &src_addr, &src_port);
    if (received < 0) {
        return received;
    }

    if (addr_ptr && addr_len_ptr) {
        if (user_len < sizeof(sockaddr_in_t)) {
            return SYSCALL_EINVAL;
        }
        sockaddr_in_t* user_addr = (sockaddr_in_t*)addr_ptr;
        user_addr->sin_family = AF_INET;
        user_addr->sin_port = htons(src_port);
        user_addr->sin_addr = src_addr;
        memory_set(user_addr->sin_zero, 0, sizeof(user_addr->sin_zero));
        *(socklen_t*)addr_len_ptr = sizeof(sockaddr_in_t);
    }

    return received;
}

static int32 sys_netinfo(sys_arg_t buffer_ptr, sys_arg_t max_entries) {
    if (!buffer_ptr || max_entries == 0) {
        return SYSCALL_EINVAL;
    }
    size_t total_bytes = (size_t)max_entries * sizeof(net_socket_info_t);
    if (max_entries > 0 && total_bytes / sizeof(net_socket_info_t) != (size_t)max_entries) {
        return SYSCALL_ERANGE;
    }
    if (!user_buffer_writable((void*)buffer_ptr, total_bytes)) {
        return SYSCALL_EFAULT;
    }
    return (int32)net_snapshot((net_socket_info_t*)buffer_ptr, max_entries);
}

// File system operations
static int32 sys_stat(sys_arg_t path_ptr, sys_arg_t stat_ptr) {
    // Basic stat implementation - all files report as regular files
    if (!path_ptr || !stat_ptr) {
        return SYSCALL_EFAULT;
    }

    char path_buf[128];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }

    if (!user_buffer_writable((void*)stat_ptr, sizeof(stat_stub_t))) {
        return SYSCALL_EFAULT;
    }

    const uint8* fdata = 0;
    uint32 fsize = 0;
    if (!vfs_read_file(path_buf, &fdata, &fsize)) {
        return SYSCALL_ENOENT;
    }
    (void)fdata; // Data not needed beyond existence/size

    // Simple stat structure (compatible with POSIX)
    stat_stub_t* stat_buf = (void*)stat_ptr;
    
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

    if (!user_buffer_writable((void*)stat_ptr, sizeof(stat_stub_t))) {
        return SYSCALL_EFAULT;
    }

    // Same structure as sys_stat
    stat_stub_t* stat_buf = (void*)stat_ptr;

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

// iovec structure for vectorized I/O
typedef struct {
    void* iov_base;  // Starting address of buffer
    size_t iov_len;  // Number of bytes to transfer
} iovec_t;

// File system operations - vectorized I/O
static int32 sys_readv(sys_arg_t fd, sys_arg_t iov, sys_arg_t iovcnt) {
    if (!iov || iovcnt == 0) {
        return SYSCALL_EINVAL;
    }

    // Validate user buffer for iovec array
    size_t iovec_array_size = (size_t)iovcnt * sizeof(iovec_t);
    if (iovcnt > 1024 || iovec_array_size / sizeof(iovec_t) != (size_t)iovcnt) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)iov, iovec_array_size)) {
        return SYSCALL_EFAULT;
    }

    int32 total_read = 0;
    iovec_t* user_iov = (iovec_t*)iov;

    for (uint32 i = 0; i < iovcnt; i++) {
        // Check if we can read this iovec entry
        if (!user_buffer_readable((const void*)&user_iov[i], sizeof(iovec_t))) {
            return total_read > 0 ? total_read : SYSCALL_EFAULT;
        }

        void* buf = user_iov[i].iov_base;
        size_t len = user_iov[i].iov_len;
        
        if (len == 0) {
            continue;
        }

        if (!buf) {
            return total_read > 0 ? total_read : SYSCALL_EFAULT;
        }

        // Call sys_read for this chunk
        int32 result = sys_read(fd, (sys_arg_t)buf, (sys_arg_t)len);
        if (result < 0) {
            return total_read > 0 ? total_read : result;
        }
        
        total_read += result;
        
        // If we read less than requested, we're done
        if ((size_t)result < len) {
            break;
        }
    }

    return total_read;
}

static int32 sys_writev(sys_arg_t fd, sys_arg_t iov, sys_arg_t iovcnt) {
    if (!iov || iovcnt == 0) {
        return SYSCALL_EINVAL;
    }

    // Validate user buffer for iovec array
    size_t iovec_array_size = (size_t)iovcnt * sizeof(iovec_t);
    if (iovcnt > 1024 || iovec_array_size / sizeof(iovec_t) != (size_t)iovcnt) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)iov, iovec_array_size)) {
        return SYSCALL_EFAULT;
    }

    int32 total_written = 0;
    iovec_t* user_iov = (iovec_t*)iov;

    for (uint32 i = 0; i < iovcnt; i++) {
        // Check if we can read this iovec entry
        if (!user_buffer_readable((const void*)&user_iov[i], sizeof(iovec_t))) {
            return total_written > 0 ? total_written : SYSCALL_EFAULT;
        }

        const void* buf = user_iov[i].iov_base;
        size_t len = user_iov[i].iov_len;
        
        if (len == 0) {
            continue;
        }

        if (!buf) {
            return total_written > 0 ? total_written : SYSCALL_EFAULT;
        }

        // Call sys_write for this chunk
        int32 result = sys_write(fd, (sys_arg_t)buf, (sys_arg_t)len);
        if (result < 0) {
            return total_written > 0 ? total_written : result;
        }
        
        total_written += result;
        
        // If we wrote less than requested, we're done
        if ((size_t)result < len) {
            break;
        }
    }

    return total_written;
}

static int32 sys_pread64(sys_arg_t fd, sys_arg_t buf, sys_arg_t count, sys_arg_t offset) {
    if (!buf || count == 0) {
        return 0;
    }

    if (!user_buffer_writable((void*)buf, count)) {
        return SYSCALL_EFAULT;
    }

    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    vfs_handle_t* handle = &vfs_handles[slot];
    
    // Save current offset
    uint32 saved_offset = handle->offset;
    
    // Set to requested offset
    if ((uint32)offset <= handle->size) {
        handle->offset = (uint32)offset;
    } else {
        return SYSCALL_EINVAL;
    }
    
    // Perform read
    int32 result = sys_read(fd, buf, count);
    
    // Restore original offset
    handle->offset = saved_offset;
    
    return result;
}

static int32 sys_pwrite64(sys_arg_t fd, sys_arg_t buf, sys_arg_t count, sys_arg_t offset) {
    if (!buf || count == 0) {
        return 0;
    }

    if (!user_buffer_readable((const void*)buf, count)) {
        return SYSCALL_EFAULT;
    }

    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    vfs_handle_t* handle = &vfs_handles[slot];
    
    // Save current offset
    uint32 saved_offset = handle->offset;
    
    // Set to requested offset
    if ((uint32)offset <= handle->size) {
        handle->offset = (uint32)offset;
    } else {
        return SYSCALL_EINVAL;
    }
    
    // Perform write
    int32 result = sys_write(fd, buf, count);
    
    // Restore original offset
    handle->offset = saved_offset;
    
    return result;
}

// File system operations - extended
static int32 sys_fsync(sys_arg_t fd) {
    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    // For our read-only VFS, fsync is a no-op but should succeed
    // In a real filesystem, this would flush buffers to disk
    return 0;
}

static int32 sys_fdatasync(sys_arg_t fd) {
    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    // For our read-only VFS, fdatasync is a no-op but should succeed
    // In a real filesystem, this would flush data (not metadata) to disk
    return 0;
}

static int32 sys_ftruncate(sys_arg_t fd, sys_arg_t length) {
    if (fd < 3) {
        return SYSCALL_EBADF;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    vfs_handle_t* handle = &vfs_handles[slot];
    
    // Our VFS is read-only, so truncation is not supported
    // In a writable filesystem, this would shrink/extend the file
    return SYSCALL_EPERM;
}

// Global current working directory (per-system, should be per-process)
static char current_working_directory[256] = "/";

static int32 sys_getcwd(sys_arg_t buf, sys_arg_t size) {
    if (!buf || size == 0) {
        return SYSCALL_EINVAL;
    }

    if (!user_buffer_writable((void*)buf, size)) {
        return SYSCALL_EFAULT;
    }

    // Return current working directory
    uint32 len = strlen(current_working_directory) + 1;

    if (size < len) {
        return SYSCALL_ERANGE;
    }

    char* user_buf = (char*)buf;
    memory_copy(current_working_directory, user_buf, len);

    // Return pointer to buffer (as address) on success
    return (int32)buf;
}

static bool resolve_at_path(sys_arg_t dirfd, sys_arg_t path_ptr, char* out, uint32 out_size) {
    if (!path_ptr || !out || out_size == 0) {
        return false;
    }
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return false;
    }
    if (path_buf[0] == '/') {
        strncpy(out, path_buf, out_size - 1);
        out[out_size - 1] = '\0';
        return true;
    }

    // Support AT_FDCWD only.
    if ((int32)dirfd != -100) {
        return false;
    }

    if (strcmp(current_working_directory, "/") == 0) {
        snprintf(out, out_size, "/%s", path_buf);
    } else {
        snprintf(out, out_size, "%s/%s", current_working_directory, path_buf);
    }
    return true;
}

static int32 sys_openat(sys_arg_t dirfd, sys_arg_t pathname, sys_arg_t flags, sys_arg_t mode) {
    char resolved[256];
    if (!resolve_at_path(dirfd, pathname, resolved, sizeof(resolved))) {
        return SYSCALL_EINVAL;
    }
    return sys_open((sys_arg_t)resolved, flags, mode);
}

static int32 sys_mkdirat(sys_arg_t dirfd, sys_arg_t pathname, sys_arg_t mode) {
    char resolved[256];
    if (!resolve_at_path(dirfd, pathname, resolved, sizeof(resolved))) {
        return SYSCALL_EINVAL;
    }
    return sys_mkdir((sys_arg_t)resolved, mode);
}

static int32 sys_unlinkat(sys_arg_t dirfd, sys_arg_t pathname, sys_arg_t flags) {
    (void)flags;
    char resolved[256];
    if (!resolve_at_path(dirfd, pathname, resolved, sizeof(resolved))) {
        return SYSCALL_EINVAL;
    }
    return sys_unlink((sys_arg_t)resolved);
}

static int32 sys_newfstatat(sys_arg_t dirfd, sys_arg_t pathname, sys_arg_t statbuf, sys_arg_t flags) {
    (void)flags;
    char resolved[256];
    if (!resolve_at_path(dirfd, pathname, resolved, sizeof(resolved))) {
        return SYSCALL_EINVAL;
    }
    return sys_stat((sys_arg_t)resolved, statbuf);
}

static int32 sys_faccessat(sys_arg_t dirfd, sys_arg_t pathname, sys_arg_t mode, sys_arg_t flags) {
    (void)flags;
    char resolved[256];
    if (!resolve_at_path(dirfd, pathname, resolved, sizeof(resolved))) {
        return SYSCALL_EINVAL;
    }
    return sys_access((sys_arg_t)resolved, mode);
}

static int32 sys_chdir(sys_arg_t path) {
    if (!path) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path)) {
        return SYSCALL_EFAULT;
    }
    
    // Handle empty path
    if (path_buf[0] == '\0') {
        return SYSCALL_ENOENT;
    }
    
    // Normalize path
    char normalized[256];
    if (path_buf[0] == '/') {
        // Absolute path
        strncpy(normalized, path_buf, sizeof(normalized) - 1);
        normalized[sizeof(normalized) - 1] = '\0';
    } else {
        // Relative path - combine with current directory
        if (strcmp(current_working_directory, "/") == 0) {
            snprintf(normalized, sizeof(normalized), "/%s", path_buf);
        } else {
            snprintf(normalized, sizeof(normalized), "%s/%s", current_working_directory, path_buf);
        }
    }
    
    // Remove trailing slashes (except for root)
    uint32 len = strlen(normalized);
    while (len > 1 && normalized[len - 1] == '/') {
        normalized[--len] = '\0';
    }
    
    // Handle . and ..
    // Simple implementation: just validate the path exists in VFS
    vfs_node_t* node = vfs_open(normalized, VFS_READ);
    
    // Accept root directory even if VFS is empty
    if (strcmp(normalized, "/") == 0) {
        if (node) vfs_close(node);
        strncpy(current_working_directory, normalized, sizeof(current_working_directory) - 1);
        current_working_directory[sizeof(current_working_directory) - 1] = '\0';
        return 0;
    }
    
    // Check if path exists and is a directory
    if (!node) {
        // Try with trailing slash for directory check
        char dir_path[260];
        snprintf(dir_path, sizeof(dir_path), "%s/", normalized);
        node = vfs_open(dir_path, VFS_READ);
        
        if (!node) {
            return SYSCALL_ENOENT;
        }
    }
    
    // Close the node since we just checked existence
    if (node) vfs_close(node);
    
    // Verify it's a directory (check if it has children or type flag)
    // For our simple VFS, accept if it exists
    
    // Update current directory
    strncpy(current_working_directory, normalized, sizeof(current_working_directory) - 1);
    current_working_directory[sizeof(current_working_directory) - 1] = '\0';
    
    return 0;
}

static int32 sys_fchdir(sys_arg_t fd) {
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    // For our simple VFS, we don't have directory file descriptors
    // In a real filesystem, we'd check if fd refers to a directory
    return SYSCALL_ENOTDIR; // Not a directory
}

static int32 sys_rename(sys_arg_t oldpath, sys_arg_t newpath) {
    if (!oldpath || !newpath) {
        return SYSCALL_EFAULT;
    }
    
    char old_buf[256], new_buf[256];
    if (!user_copy_string(old_buf, sizeof(old_buf), (const char*)oldpath) ||
        !user_copy_string(new_buf, sizeof(new_buf), (const char*)newpath)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so rename is not supported
    return SYSCALL_EPERM;
}

static int32 sys_mkdir(sys_arg_t pathname, sys_arg_t mode) {
    if (!pathname) {
        return SYSCALL_EFAULT;
    }
    
    (void)mode; // Mode is ignored for our read-only VFS
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so mkdir is not supported
    return SYSCALL_EPERM;
}

static int32 sys_rmdir(sys_arg_t pathname) {
    if (!pathname) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so rmdir is not supported
    return SYSCALL_EPERM;
}

static int32 sys_creat(sys_arg_t pathname, sys_arg_t mode) {
    if (!pathname) {
        return SYSCALL_EFAULT;
    }
    
    (void)mode; // Mode is ignored for our read-only VFS
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so creat is not supported
    return SYSCALL_EPERM;
}

static int32 sys_link(sys_arg_t oldpath, sys_arg_t newpath) {
    if (!oldpath || !newpath) {
        return SYSCALL_EFAULT;
    }
    
    char old_buf[256], new_buf[256];
    if (!user_copy_string(old_buf, sizeof(old_buf), (const char*)oldpath) ||
        !user_copy_string(new_buf, sizeof(new_buf), (const char*)newpath)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so link is not supported
    return SYSCALL_EPERM;
}

static int32 sys_symlink(sys_arg_t target, sys_arg_t linkpath) {
    if (!target || !linkpath) {
        return SYSCALL_EFAULT;
    }
    
    char target_buf[256], link_buf[256];
    if (!user_copy_string(target_buf, sizeof(target_buf), (const char*)target) ||
        !user_copy_string(link_buf, sizeof(link_buf), (const char*)linkpath)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so symlink is not supported
    return SYSCALL_EPERM;
}

static int32 sys_readlink(sys_arg_t path, sys_arg_t buf, sys_arg_t bufsiz) {
    if (!path || !buf || bufsiz == 0) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_writable((void*)buf, bufsiz)) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS doesn't support symlinks, so return ENOENT
    return SYSCALL_ENOENT;
}

static int32 sys_chmod(sys_arg_t pathname, sys_arg_t mode) {
    if (!pathname) {
        return SYSCALL_EFAULT;
    }
    
    (void)mode; // Mode is ignored for our read-only VFS
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so chmod is not supported
    return SYSCALL_EPERM;
}

static int32 sys_fchmod(sys_arg_t fd, sys_arg_t mode) {
    (void)mode; // Mode is ignored for our read-only VFS
    
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    // Our VFS is read-only, so fchmod is not supported
    return SYSCALL_EPERM;
}

static int32 sys_chown(sys_arg_t pathname, sys_arg_t owner, sys_arg_t group) {
    if (!pathname) {
        return SYSCALL_EFAULT;
    }
    
    (void)owner; // Owner is ignored for our read-only VFS
    (void)group; // Group is ignored for our read-only VFS
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so chown is not supported
    return SYSCALL_EPERM;
}

static int32 sys_fchown(sys_arg_t fd, sys_arg_t owner, sys_arg_t group) {
    (void)owner; // Owner is ignored for our read-only VFS
    (void)group; // Group is ignored for our read-only VFS
    
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    // Our VFS is read-only, so fchown is not supported
    return SYSCALL_EPERM;
}

static int32 sys_lchown(sys_arg_t pathname, sys_arg_t owner, sys_arg_t group) {
    if (!pathname) {
        return SYSCALL_EFAULT;
    }
    
    (void)owner; // Owner is ignored for our read-only VFS
    (void)group; // Group is ignored for our read-only VFS
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname)) {
        return SYSCALL_EFAULT;
    }
    
    // Our VFS is read-only, so lchown is not supported
    return SYSCALL_EPERM;
}

static int32 sys_umask(sys_arg_t mask) {
    // For our simple implementation, just return the previous umask (default 022)
    // In a real implementation, we'd track the current umask per process
    static int32 current_umask = 022; // Default umask
    
    int32 old_umask = current_umask;
    current_umask = (int32)mask & 0777; // Only keep permission bits
    
    return old_umask;
}

// Linux directory entry structure for getdents
typedef struct {
    uint32 d_ino;      // Inode number
    uint32 d_off;      // Offset to next entry
    uint16 d_reclen;   // Length of this record
    char d_name[256];  // Filename
} linux_dirent_t;

static int32 sys_getdents(sys_arg_t fd, sys_arg_t dirp, sys_arg_t count) {
    if (!dirp || count == 0) {
        return 0;
    }

    if (!user_buffer_writable((void*)dirp, count)) {
        return SYSCALL_EFAULT;
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }

    if (!vfs_device_handles[slot].used || !vfs_device_handles[slot].node) {
        return SYSCALL_ENOTDIR;
    }

    vfs_node_t* dir_node = vfs_device_handles[slot].node;
    if (!(dir_node->flags & VFS_DIRECTORY)) {
        return SYSCALL_ENOTDIR;
    }

    char* buffer = (char*)dirp;
    char* buffer_end = buffer + count;
    uint32 total_bytes = 0;
    uint32 offset = 0;

    // Read directory entries one by one
    for (uint32 i = 0; i < 100; i++) { // Limit to 100 entries to prevent infinite loop
        vfs_dirent_t entry;
        if (!vfs_readdir(dir_node, i, &entry)) {
            break; // No more entries
        }
        
        // Calculate record size
        uint16 reclen = offsetof(linux_dirent_t, d_name) + strlen(entry.name) + 1;
        if (buffer + reclen > buffer_end) {
            break; // Not enough space in buffer
        }

        linux_dirent_t* linux_entry = (linux_dirent_t*)buffer;
        linux_entry->d_ino = entry.inode;
        linux_entry->d_off = offset;
        linux_entry->d_reclen = reclen;
        

        
        strncpy(linux_entry->d_name, entry.name, sizeof(linux_entry->d_name) - 1);
        linux_entry->d_name[sizeof(linux_entry->d_name) - 1] = '\0';

        buffer += reclen;
        total_bytes += reclen;
        offset += reclen;
    }

    return (int32)total_bytes;
}



typedef struct {
    uint64 d_ino;      // Inode number
    uint64 d_off;      // Offset to next entry
    uint16 d_reclen;   // Length of this record
    uint8  d_type;     // File type
    char   d_name[256]; // Filename
} linux_dirent64_t;



#define SPAWN_FLAG_BACKGROUND 0x1u
#define SPAWN_FLAG_FOREGROUND 0x2u

static int32 sys_spawn_task(sys_arg_t path_ptr, sys_arg_t flags) {
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }

    char path[256];
    if (!user_copy_string(path, sizeof(path), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }

    const uint8* elf_data = NULL;
    uint32 elf_size = 0;
    if (!vfs_read_file(path, &elf_data, &elf_size) || !elf_data || elf_size == 0) {
        return SYSCALL_ENOENT;
    }

    task_t* task = task_create_elf(elf_data, elf_size, path);
    if (!task) {
        return SYSCALL_ENOEXEC;
    }

    if (flags & SPAWN_FLAG_BACKGROUND) {
        task->is_background = true;
    }
    if (flags & SPAWN_FLAG_FOREGROUND) {
        task_set_foreground(task);
    }

    return (int32)task->id;
}

// Process management
static int32 sys_fork(void) {
    // Check if this is a child process returning from fork
    if (current_task && current_task->exit_code == FORK_CHILD_RETURN) {
        current_task->exit_code = 0; // Reset for normal exit
        return 0; // Child returns 0
    }

    task_t* child_task = task_clone_current();
    if (!child_task) {
        return SYSCALL_ENOMEM; // Out of memory
    }

    // Mark child as background task - it won't steal foreground from parent
    child_task->is_background = true;

    // Return child PID to parent
    return child_task->id;
}

static int32 sys_vfork(void) {
    // For simplicity, implement vfork as fork
    // In a real implementation, vfork would share address space with parent
    return sys_fork();
}

static int32 sys_execve(sys_arg_t filename, sys_arg_t argv, sys_arg_t envp) {
    if (!filename) {
        return SYSCALL_EFAULT;
    }

    // Validate filename pointer
    if (!user_buffer_readable((const void*)filename, 1)) {
        return SYSCALL_EFAULT;
    }

    // Read the executable file
    const uint8* elf_data;
    uint32 elf_size;
    if (!vfs_read_file((const char*)filename, &elf_data, &elf_size)) {
        return SYSCALL_ENOENT; // File not found
    }

    // Load the ELF executable
    elf_load_info_t elf_info;
    int status = elf_load_executable(elf_data, elf_size, &elf_info);
    if (status != 0 || !elf_info.valid || elf_info.entry_point == 0) {
        return SYSCALL_ENOEXEC; // Exec format error
    }

    // Collect argv/envp from caller address space before replacing task state.
    char argv_copy[64][256];
    char envp_copy[64][256];
    int argc = 0;
    int envc = 0;
    if (argv) {
        for (int i = 0; i < 64; i++) {
            char* const* user_argv = (char* const*)argv;
            if (!user_buffer_readable((const void*)&user_argv[i], sizeof(char*))) {
                return SYSCALL_EFAULT;
            }
            char* p = user_argv[i];
            if (!p) {
                break;
            }
            if (!user_copy_string(argv_copy[argc], sizeof(argv_copy[argc]), p)) {
                return SYSCALL_EFAULT;
            }
            argc++;
        }
    }
    if (envp) {
        for (int i = 0; i < 64; i++) {
            char* const* user_envp = (char* const*)envp;
            if (!user_buffer_readable((const void*)&user_envp[i], sizeof(char*))) {
                return SYSCALL_EFAULT;
            }
            char* p = user_envp[i];
            if (!p) {
                break;
            }
            if (!user_copy_string(envp_copy[envc], sizeof(envp_copy[envc]), p)) {
                return SYSCALL_EFAULT;
            }
            envc++;
        }
    }

    // Update current task's ELF info and stack.
    if (current_task) {
        memory_copy((const char*)&elf_info, (char*)&current_task->elf_info, sizeof(elf_load_info_t));
        current_task->page_directory = (page_directory_t*)elf_info.page_directory;
        current_task->usermode_entry_point = elf_info.entry_point;

        page_directory_t* old_dir = vmm_get_current_page_directory();
        vmm_switch_page_directory(current_task->page_directory);

        uint32 stack_bottom = USER_STACK_TOP - (EXECVE_USER_STACK_PAGES * MEMORY_PAGE_SIZE);
        (void)map_user_range(current_task->page_directory, stack_bottom, USER_STACK_TOP);

        uint32 arg_ptrs[64];
        uint32 env_ptrs[64];
        uint32 sp = USER_STACK_TOP;

        for (int i = envc - 1; i >= 0; i--) {
            uint32 slen = (uint32)strlength(envp_copy[i]) + 1;
            sp -= slen;
            memory_copy((const char*)envp_copy[i], (char*)sp, slen);
            env_ptrs[i] = sp;
        }
        for (int i = argc - 1; i >= 0; i--) {
            uint32 slen = (uint32)strlength(argv_copy[i]) + 1;
            sp -= slen;
            memory_copy((const char*)argv_copy[i], (char*)sp, slen);
            arg_ptrs[i] = sp;
        }

        sp &= ~0x0Fu;
        uint32 frame_words = (uint32)(1 + (argc + 1) + (envc + 1));
        sp -= frame_words * sizeof(uint32);
        uint32* stack_words = (uint32*)sp;
        stack_words[0] = (uint32)argc;
        for (int i = 0; i < argc; i++) {
            stack_words[1 + i] = arg_ptrs[i];
        }
        stack_words[1 + argc] = 0;
        for (int i = 0; i < envc; i++) {
            stack_words[1 + argc + 1 + i] = env_ptrs[i];
        }
        stack_words[1 + argc + 1 + envc] = 0;

        current_task->usermode_stack_top = sp;
        current_task->needs_usermode_entry = true;

        vmm_switch_page_directory(old_dir);
    }

    // Success - scheduler will handle the transition to new executable
    return 0;
}

static int32 sys_wait4(sys_arg_t pid, sys_arg_t stat_addr, sys_arg_t options, sys_arg_t rusage) {
    if (!current_task) {
        return SYSCALL_EINVAL;
    }
    
    (void)options; // Options are ignored for now
    (void)rusage;  // Resource usage is not supported yet

    // For a simple implementation, check if stat_addr is writable
    if (stat_addr && !user_buffer_writable((void*)stat_addr, sizeof(int32))) {
        return SYSCALL_EFAULT;
    }

    if (pid > 0) {
        int32 code = task_wait_pid((uint32)pid);
        if (code < 0) {
            return SYSCALL_ECHILD;
        }
        if (stat_addr) {
            *(int32*)stat_addr = (code & 0xFF) << 8;
        }
        return (int32)pid;
    }

    if (pid == 0 || (int32)pid == -1) {
        // Wait for any terminated task (coarse implementation).
        for (uint32 probe = 1; probe < 4096; probe++) {
            int32 code = task_get_exit_code(probe);
            if (code >= 0) {
                if (stat_addr) {
                    *(int32*)stat_addr = (code & 0xFF) << 8;
                }
                return (int32)probe;
            }
        }
        return SYSCALL_ECHILD;
    }

    return SYSCALL_ECHILD;
}

static int32 sys_kill(sys_arg_t pid, sys_arg_t sig) {
    if (sig < 0 || sig > 31) {
        return SYSCALL_EINVAL;
    }

    if (pid == 0) {
        // Send to current process group
        if (!current_task) {
            return SYSCALL_EPERM;
        }

        uint32 pgrp = current_task->pgrp;
        task_t* task = ready_queue_head;
        if (task) {
            do {
                if (task->pgrp == pgrp) {
                    if (sig != 0) {
                        task->pending_signals |= (1 << sig);
                    }
                }
                task = task->next;
            } while (task != ready_queue_head);
        }
        return 0;
    }

    if (pid < 0) {
        // Send to process group (absolute value of pid)
        uint32 pgrp = (uint32)-pid;
        task_t* task = ready_queue_head;
        if (task) {
            do {
                if (task->pgrp == pgrp) {
                    if (sig != 0) {
                        task->pending_signals |= (1 << sig);
                    }
                }
                task = task->next;
            } while (task != ready_queue_head);
        }
        return 0;
    }

    // Find the target task
    task_t* target_task = ready_queue_head;
    if (target_task) {
        do {
            if (target_task->id == (uint32)pid) {
                if (sig == 0) {
                    // Signal 0 just checks if process exists
                    return 0;
                }
                // Set pending signal
                target_task->pending_signals |= (1 << sig);
                return 0;
            }
            target_task = target_task->next;
        } while (target_task != ready_queue_head);
    }

    return SYSCALL_ESRCH; // No such process
}

static int32 sys_tkill(sys_arg_t tid, sys_arg_t sig) {
    // tkill sends a signal to a specific thread
    if (sig < 0 || sig > 31) {
        return SYSCALL_EINVAL;
    }
    
    // In our simple implementation, tid == pid (single-threaded processes)
    if (current_task && current_task->id == (uint32)tid) {
        if (sig == 0) {
            return 0; // Signal 0 just checks existence
        }
        current_task->pending_signals |= (1 << sig);
        return 0;
    }
    
    return SYSCALL_ESRCH; // No such thread
}

static int32 sys_tgkill(sys_arg_t tgid, sys_arg_t tid, sys_arg_t sig) {
    // tgkill sends signal to thread tid in thread group tgid
    if (sig < 0 || sig > 31) {
        return SYSCALL_EINVAL;
    }
    
    // In our implementation, tgid == pid and tid == pid (single-threaded)
    if (current_task && current_task->id == (uint32)tid && current_task->id == (uint32)tgid) {
        if (sig == 0) {
            return 0;
        }
        current_task->pending_signals |= (1 << sig);
        return 0;
    }
    
    return SYSCALL_ESRCH;
}

static int32 sys_gettid(void) {
    // Return thread ID (same as PID in our single-threaded implementation)
    if (current_task) {
        return (int32)current_task->id;
    }
    return 0;
}



static int32 sys_setuid(sys_arg_t uid) {
    // Set user ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // Only root (uid 0) can change to any uid
    // Non-root can only change to their own uid
    if (current_task->uid != 0 && current_task->uid != (uint32)uid) {
        return SYSCALL_EPERM;
    }
    
    current_task->uid = (uint32)uid;
    return 0;
}

static int32 sys_setgid(sys_arg_t gid) {
    // Set group ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // Only root can change to any gid
    if (current_task->uid != 0 && current_task->gid != (uint32)gid) {
        return SYSCALL_EPERM;
    }
    
    current_task->gid = (uint32)gid;
    return 0;
}

static int32 sys_setreuid(sys_arg_t ruid, sys_arg_t euid) {
    // Set real and effective user ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // -1 means don't change
    if (ruid != (sys_arg_t)-1) {
        if (current_task->uid != 0 && current_task->uid != (uint32)ruid) {
            return SYSCALL_EPERM;
        }
    }
    
    if (euid != (sys_arg_t)-1) {
        if (current_task->uid != 0 && current_task->uid != (uint32)euid) {
            return SYSCALL_EPERM;
        }
    }
    
    // In our simple implementation, we only track one uid
    if (euid != (sys_arg_t)-1) {
        current_task->uid = (uint32)euid;
    } else if (ruid != (sys_arg_t)-1) {
        current_task->uid = (uint32)ruid;
    }
    
    return 0;
}

static int32 sys_setregid(sys_arg_t rgid, sys_arg_t egid) {
    // Set real and effective group ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    if (rgid != (sys_arg_t)-1) {
        if (current_task->uid != 0 && current_task->gid != (uint32)rgid) {
            return SYSCALL_EPERM;
        }
    }
    
    if (egid != (sys_arg_t)-1) {
        if (current_task->uid != 0 && current_task->gid != (uint32)egid) {
            return SYSCALL_EPERM;
        }
    }
    
    if (egid != (sys_arg_t)-1) {
        current_task->gid = (uint32)egid;
    } else if (rgid != (sys_arg_t)-1) {
        current_task->gid = (uint32)rgid;
    }
    
    return 0;
}

static int32 sys_setresuid(sys_arg_t ruid, sys_arg_t euid, sys_arg_t suid) {
    // Set real, effective, and saved user ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // Only root can set arbitrary UIDs
    if (current_task->uid != 0) {
        // Non-root can only set to current uid
        if ((ruid != (sys_arg_t)-1 && (uint32)ruid != current_task->uid) ||
            (euid != (sys_arg_t)-1 && (uint32)euid != current_task->uid) ||
            (suid != (sys_arg_t)-1 && (uint32)suid != current_task->uid)) {
            return SYSCALL_EPERM;
        }
    }
    
    // Apply changes (we only track one uid)
    if (euid != (sys_arg_t)-1) {
        current_task->uid = (uint32)euid;
    } else if (ruid != (sys_arg_t)-1) {
        current_task->uid = (uint32)ruid;
    }
    
    return 0;
}

static int32 sys_getresuid(sys_arg_t ruid_ptr, sys_arg_t euid_ptr, sys_arg_t suid_ptr) {
    // Get real, effective, and saved user ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    uint32 uid = current_task->uid;
    
    if (ruid_ptr && !user_buffer_writable((void*)ruid_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    if (euid_ptr && !user_buffer_writable((void*)euid_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    if (suid_ptr && !user_buffer_writable((void*)suid_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    if (ruid_ptr) *(uint32*)ruid_ptr = uid;
    if (euid_ptr) *(uint32*)euid_ptr = uid;
    if (suid_ptr) *(uint32*)suid_ptr = uid;
    
    return 0;
}

static int32 sys_setresgid(sys_arg_t rgid, sys_arg_t egid, sys_arg_t sgid) {
    // Set real, effective, and saved group ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    if (current_task->uid != 0) {
        if ((rgid != (sys_arg_t)-1 && (uint32)rgid != current_task->gid) ||
            (egid != (sys_arg_t)-1 && (uint32)egid != current_task->gid) ||
            (sgid != (sys_arg_t)-1 && (uint32)sgid != current_task->gid)) {
            return SYSCALL_EPERM;
        }
    }
    
    if (egid != (sys_arg_t)-1) {
        current_task->gid = (uint32)egid;
    } else if (rgid != (sys_arg_t)-1) {
        current_task->gid = (uint32)rgid;
    }
    
    return 0;
}

static int32 sys_getresgid(sys_arg_t rgid_ptr, sys_arg_t egid_ptr, sys_arg_t sgid_ptr) {
    // Get real, effective, and saved group ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    uint32 gid = current_task->gid;
    
    if (rgid_ptr && !user_buffer_writable((void*)rgid_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    if (egid_ptr && !user_buffer_writable((void*)egid_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    if (sgid_ptr && !user_buffer_writable((void*)sgid_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    if (rgid_ptr) *(uint32*)rgid_ptr = gid;
    if (egid_ptr) *(uint32*)egid_ptr = gid;
    if (sgid_ptr) *(uint32*)sgid_ptr = gid;
    
    return 0;
}

static int32 sys_setfsuid(sys_arg_t uid) {
    // Set filesystem user ID (used for filesystem access checks)
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    uint32 old_uid = current_task->uid;
    
    // Can set fsuid to current uid, euid, or suid (all same in our impl)
    // Root can set to anything
    if (current_task->uid == 0 || current_task->uid == (uint32)uid) {
        // In full implementation, would track separate fsuid
        // For now, return old fsuid
        return (int32)old_uid;
    }
    
    return (int32)old_uid; // Return old fsuid on failure too
}

static int32 sys_setfsgid(sys_arg_t gid) {
    // Set filesystem group ID
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    uint32 old_gid = current_task->gid;
    
    if (current_task->uid == 0 || current_task->gid == (uint32)gid) {
        return (int32)old_gid;
    }
    
    return (int32)old_gid;
}

static int32 sys_getgroups(sys_arg_t size, sys_arg_t list) {
    // Get supplementary group IDs
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // Count groups in bitmask
    uint32 mask = current_task->groups_mask;
    int32 count = 0;
    for (int i = 0; i < 32; i++) {
        if (mask & (1u << i)) count++;
    }
    
    if (size == 0) {
        return count; // Just return count
    }
    
    if (!user_buffer_writable((void*)list, size * sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32* groups = (uint32*)list;
    int32 written = 0;
    for (int i = 0; i < 32 && written < (int32)size; i++) {
        if (mask & (1u << i)) {
            groups[written++] = (uint32)i;
        }
    }
    
    return written;
}

static int32 sys_setgroups(sys_arg_t size, sys_arg_t list) {
    // Set supplementary group IDs (requires root)
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    if (current_task->uid != 0) {
        return SYSCALL_EPERM; // Only root can set groups
    }
    
    if (size > 32) {
        return SYSCALL_EINVAL; // Our bitmask only supports 32 groups
    }
    
    if (size > 0 && !user_buffer_readable((void*)list, size * sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32 new_mask = 0;
    const uint32* groups = (const uint32*)list;
    for (uint32 i = 0; i < size; i++) {
        if (groups[i] < 32) {
            new_mask |= (1u << groups[i]);
        }
    }
    
    current_task->groups_mask = new_mask;
    return 0;
}

// Memory management
static int32 sys_mmap(sys_arg_t addr, sys_arg_t length, sys_arg_t prot, sys_arg_t flags, sys_arg_t fd, sys_arg_t offset) {
    (void)prot; // Page permission bits are coarse in current pager.

    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_ENOMEM;
    }

    uint32 va_hint = (uint32)addr;
    uint32 size = (uint32)length;
    if (size == 0) {
        return SYSCALL_EINVAL;
    }

    uint32 aligned_size = memory_align_up(size, MEMORY_PAGE_SIZE);
    uint32 va_start;

    if (va_hint == 0) {
        // Find a free address, start from 0x10000000
        va_start = 0x10000000;
        // Simple check, assume it's free
    } else {
        va_start = memory_align_down(va_hint, MEMORY_PAGE_SIZE);
    }

    uint32 va_end = va_start + aligned_size;

    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }

    // Check if range is free (simple check)
    for (uint32 va = va_start; va < va_end; va += MEMORY_PAGE_SIZE) {
        if (vmm_get_physical_addr(task->page_directory, va)) {
            return SYSCALL_EINVAL; // Overlap
        }
    }

    // Allocate and map pages.
    for (uint32 va = va_start; va < va_end; va += MEMORY_PAGE_SIZE) {
        uint32 frame = pmm_alloc_frame();
        if (!frame) {
            // Unmap previous
            for (uint32 uva = va_start; uva < va; uva += MEMORY_PAGE_SIZE) {
                uint32 phys = vmm_get_physical_addr(task->page_directory, uva);
                vmm_unmap_page(task->page_directory, uva);
                if (phys) pmm_free_frame(phys);
            }
            return SYSCALL_ENOMEM;
        }

        int page_flags = PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE;
        memory_result_t res = vmm_map_page(task->page_directory, va, frame, page_flags);
        if (res != MEMORY_OK) {
            pmm_free_frame(frame);
            // Unmap previous
            return SYSCALL_ENOMEM;
        }
    }

    // Optional file-backed preload (MAP_ANONYMOUS uses zeroed frames).
    if (!(flags & MAP_ANONYMOUS) && (int32)fd >= 3) {
        uint32 slot = (uint32)fd - 3;
        if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
            sys_munmap(va_start, aligned_size);
            return SYSCALL_EBADF;
        }
        const vfs_handle_t* h = &vfs_handles[slot];
        if (!h->data) {
            sys_munmap(va_start, aligned_size);
            return SYSCALL_EINVAL;
        }
        uint32 file_off = (uint32)offset;
        if (file_off < h->size) {
            uint32 remaining = h->size - file_off;
            uint32 to_copy = (remaining < aligned_size) ? remaining : aligned_size;
            if (user_buffer_writable((void*)va_start, to_copy)) {
                memory_copy((const char*)(h->data + file_off), (char*)va_start, to_copy);
            }
        }
    } else if (!(flags & MAP_ANONYMOUS) && (int32)fd < 0) {
        sys_munmap(va_start, aligned_size);
        return SYSCALL_EINVAL;
    }

    return (int32)va_start;
}

static int32 sys_munmap(sys_arg_t addr, sys_arg_t length) {
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }

    uint32 va_start = memory_align_down((uint32)addr, MEMORY_PAGE_SIZE);
    uint32 size = (uint32)length;
    uint32 va_end = memory_align_up(va_start + size, MEMORY_PAGE_SIZE);

    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP || va_start >= va_end) {
        return SYSCALL_EINVAL;
    }

    // Unmap the pages
    for (uint32 va = va_start; va < va_end; va += MEMORY_PAGE_SIZE) {
        uint32 phys = vmm_get_physical_addr(task->page_directory, va);
        vmm_unmap_page(task->page_directory, va);
        if (phys) {
            pmm_free_frame(phys);
        }
    }

    return 0;
}

static int32 sys_mprotect(sys_arg_t addr, sys_arg_t len, sys_arg_t prot) {
    if (len == 0) {
        return 0;
    }
    
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }
    
    uint32 va_start = memory_align_down((uint32)addr, MEMORY_PAGE_SIZE);
    uint32 va_end = memory_align_up((uint32)addr + len, MEMORY_PAGE_SIZE);
    
    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }
    
    // For our simple implementation, just validate the range exists
    for (uint32 va = va_start; va < va_end; va += MEMORY_PAGE_SIZE) {
        uint32 phys = vmm_get_physical_addr(task->page_directory, va);
        if (!phys) {
            return SYSCALL_ENOMEM; // Page not mapped
        }
        // In a real implementation, we would update page protection bits
        // For now, just return success
    }
    
    return 0;
}

static int32 sys_madvise(sys_arg_t addr, sys_arg_t length, sys_arg_t advice) {
    if (length == 0) {
        return 0;
    }
    
    (void)addr; // Address is unused in our simple implementation
    (void)advice; // Advice is ignored
    
    // For our simple memory manager, madvise is a no-op but succeeds
    return 0;
}

static int32 sys_msync(sys_arg_t addr, sys_arg_t length, sys_arg_t flags) {
    if (length == 0) {
        return 0;
    }
    
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }
    
    // Validate flags
    if (flags & ~(0x01 | 0x02)) { // MS_ASYNC | MS_INVALIDATE
        return SYSCALL_EINVAL;
    }
    
    uint32 va_start = memory_align_down((uint32)addr, MEMORY_PAGE_SIZE);
    uint32 va_end = memory_align_up((uint32)addr + length, MEMORY_PAGE_SIZE);
    
    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }
    
    // Our memory manager doesn't have a backing store, so sync is a no-op
    return 0;
}

// SysV shared memory (minimal in-kernel segment table)
#define IPC_CREAT  01000
#define IPC_EXCL   02000
#define IPC_RMID   0
#define IPC_SET    1
#define IPC_STAT   2

typedef struct {
    uint32 shm_segsz;
    uint32 shm_nattch;
    uint32 shm_perm;
    uint32 shm_atime;
    uint32 shm_dtime;
    uint32 shm_ctime;
} shmid_ds_stub_t;

static int32 sys_shmget(sys_arg_t key, sys_arg_t size, sys_arg_t shmflg) {
    int32 k = (int32)key;
    uint32 seg_size = (uint32)size;
    if (seg_size == 0) {
        return SYSCALL_EINVAL;
    }

    shm_segment_t* existing = find_shm_segment_by_key(k);
    if (existing) {
        if ((shmflg & IPC_EXCL) && (shmflg & IPC_CREAT)) {
            return SYSCALL_EINVAL;
        }
        return existing->shmid;
    }

    if (!(shmflg & IPC_CREAT)) {
        return SYSCALL_ENOENT;
    }

    for (uint32 i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (!shm_segments[i].used) {
            shm_segments[i].data = (uint8*)malloc(seg_size);
            if (!shm_segments[i].data) {
                return SYSCALL_ENOMEM;
            }
            memory_set(shm_segments[i].data, 0, seg_size);
            shm_segments[i].used = true;
            shm_segments[i].key = k;
            shm_segments[i].shmid = (int32)(i + 1);
            shm_segments[i].size = seg_size;
            shm_segments[i].marked_for_delete = false;
            shm_segments[i].attach_count = 0;
            return shm_segments[i].shmid;
        }
    }
    return SYSCALL_ENFILE;
}

static int32 sys_shmat(sys_arg_t shmid, sys_arg_t shmaddr, sys_arg_t shmflg) {
    (void)shmaddr;
    (void)shmflg;
    shm_segment_t* seg = find_shm_segment_by_id((int32)shmid);
    if (!seg) {
        return SYSCALL_EINVAL;
    }

    int32 mapped = sys_mmap(0, seg->size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, (sys_arg_t)-1, 0);
    if (mapped < 0) {
        return mapped;
    }
    if (!user_buffer_writable((void*)mapped, seg->size)) {
        sys_munmap((sys_arg_t)mapped, seg->size);
        return SYSCALL_EFAULT;
    }
    memory_copy((const char*)seg->data, (char*)mapped, seg->size);

    uint32 task_id = current_task ? current_task->id : 0;
    for (uint32 i = 0; i < MAX_SHM_ATTACHMENTS; i++) {
        if (!shm_attachments[i].used) {
            shm_attachments[i].used = true;
            shm_attachments[i].task_id = task_id;
            shm_attachments[i].shmid = (int32)shmid;
            shm_attachments[i].addr = (uint32)mapped;
            shm_attachments[i].size = seg->size;
            seg->attach_count++;
            return mapped;
        }
    }

    sys_munmap((sys_arg_t)mapped, seg->size);
    return SYSCALL_ENFILE;
}

static int32 sys_shmctl(sys_arg_t shmid, sys_arg_t cmd, sys_arg_t buf) {
    shm_segment_t* seg = find_shm_segment_by_id((int32)shmid);
    if (!seg) {
        return SYSCALL_EINVAL;
    }

    if ((int32)cmd == IPC_RMID) {
        seg->marked_for_delete = true;
        if (seg->attach_count == 0) {
            if (seg->data) {
                free(seg->data);
            }
            memory_set((uint8*)seg, 0, sizeof(*seg));
        }
        return 0;
    }

    if ((int32)cmd == IPC_STAT) {
        if (!buf || !user_buffer_writable((void*)buf, sizeof(shmid_ds_stub_t))) {
            return SYSCALL_EFAULT;
        }
        shmid_ds_stub_t* ds = (shmid_ds_stub_t*)buf;
        ds->shm_segsz = seg->size;
        ds->shm_nattch = seg->attach_count;
        ds->shm_perm = 0;
        ds->shm_atime = fake_unix_epoch;
        ds->shm_dtime = fake_unix_epoch;
        ds->shm_ctime = fake_unix_epoch;
        return 0;
    }

    if ((int32)cmd == IPC_SET) {
        return 0;
    }

    return SYSCALL_EINVAL;
}

static int32 sys_shmdt(sys_arg_t shmaddr) {
    uint32 task_id = current_task ? current_task->id : 0;
    shm_attachment_t* att = find_shm_attachment(task_id, (uint32)shmaddr);
    if (!att) {
        return SYSCALL_EINVAL;
    }
    shm_segment_t* seg = find_shm_segment_by_id(att->shmid);
    if (seg && seg->data &&
        user_buffer_readable((const void*)att->addr, att->size) &&
        att->size <= seg->size) {
        memory_copy((const char*)att->addr, (char*)seg->data, att->size);
    }
    sys_munmap(att->addr, att->size);
    if (seg && seg->attach_count > 0) {
        seg->attach_count--;
    }
    if (seg && seg->marked_for_delete && seg->attach_count == 0) {
        if (seg->data) {
            free(seg->data);
        }
        memory_set((uint8*)seg, 0, sizeof(*seg));
    }
    memory_set((uint8*)att, 0, sizeof(*att));
    return 0;
}

static int32 sys_mlock(sys_arg_t addr, sys_arg_t len) {
    if (len == 0) {
        return 0;
    }
    
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }
    
    uint32 va_start = memory_align_down((uint32)addr, MEMORY_PAGE_SIZE);
    uint32 va_end = memory_align_up((uint32)addr + len, MEMORY_PAGE_SIZE);
    
    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }
    
    // For our simple implementation, mlock is a no-op but succeeds
    // In a real implementation, we would prevent page swapping
    return 0;
}

static int32 sys_munlock(sys_arg_t addr, sys_arg_t len) {
    if (len == 0) {
        return 0;
    }
    
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }
    
    uint32 va_start = memory_align_down((uint32)addr, MEMORY_PAGE_SIZE);
    uint32 va_end = memory_align_up((uint32)addr + len, MEMORY_PAGE_SIZE);
    
    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }
    
    // For our simple implementation, munlock is a no-op but succeeds
    // In a real implementation, we would allow page swapping again
    return 0;
}

static int32 sys_mlockall(sys_arg_t flags) {
    (void)flags; // Flags are ignored in our simple implementation
    
    // For our simple implementation, mlockall is a no-op but succeeds
    // In a real implementation, we would lock all current and future pages
    return 0;
}

static int32 sys_munlockall(void) {
    // For our simple implementation, munlockall is a no-op but succeeds
    // In a real implementation, we would unlock all locked pages
    return 0;
}

static int32 sys_mremap(sys_arg_t old_addr, sys_arg_t old_size, sys_arg_t new_size, sys_arg_t flags, sys_arg_t new_addr) {
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }
    
    // For simplicity, only support MREMAP_MAYMOVE
    if (flags & ~0x01) { // MREMAP_MAYMOVE
        return SYSCALL_EINVAL;
    }
    
    uint32 va_old = (uint32)old_addr;
    uint32 size_old = (uint32)old_size;
    uint32 size_new = (uint32)new_size;
    
    if (size_old == 0 || va_old < MEMORY_USER_START) {
        return SYSCALL_EINVAL;
    }
    
    uint32 va_old_end = va_old + memory_align_up(size_old, MEMORY_PAGE_SIZE);
    if (va_old_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }
    
    // For our simple implementation, just return ENOSYS
    // Real mremap implementation is complex and requires VMM changes
    return SYSCALL_ENOSYS;
}

static int32 sys_mincore(sys_arg_t addr, sys_arg_t length, sys_arg_t vec) {
    if (length == 0 || !vec) {
        return SYSCALL_EINVAL;
    }
    
    task_t* task = current_user_task_ptr();
    if (!task) {
        return SYSCALL_EINVAL;
    }
    
    uint32 va_start = memory_align_down((uint32)addr, MEMORY_PAGE_SIZE);
    uint32 va_end = memory_align_up((uint32)addr + length, MEMORY_PAGE_SIZE);
    
    if (va_start < MEMORY_USER_START || va_end > USER_STACK_TOP) {
        return SYSCALL_EINVAL;
    }
    
    // Calculate number of pages
    uint32 page_count = (va_end - va_start) / MEMORY_PAGE_SIZE;
    
    // Validate output buffer
    if (!user_buffer_writable((void*)vec, page_count)) {
        return SYSCALL_EFAULT;
    }
    
    uint8* result_vec = (uint8*)vec;
    
    // For each page, check if it's resident in memory
    for (uint32 i = 0; i < page_count; i++) {
        uint32 va = va_start + (i * MEMORY_PAGE_SIZE);
        uint32 phys = vmm_get_physical_addr(task->page_directory, va);
        result_vec[i] = (phys != 0) ? 1 : 0; // Set bit if page is mapped
    }
    
    return 0;
}

// Signal action structure (simplified)
typedef struct {
    void (*sa_handler)(int);
    uint32 sa_flags;
    void (*sa_restorer)(void);
    uint32 sa_mask[2]; // 64-bit signal mask
} sigaction_t;

// Signal set manipulation helpers
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

// Per-task signal state (simplified - uses pending_signals from task struct)
static uint32 blocked_signals = 0;  // Global for now, should be per-task
static sigaction_t signal_handlers[32]; // Signal handlers

// Signals
static int32 sys_pause(void) {
    // Suspend until a signal is received
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // In a real implementation, we'd block until a signal arrives
    // For now, just yield and check for pending signals
    while (1) {
        if (current_task->pending_signals & ~blocked_signals) {
            // A signal is pending and not blocked
            return -4; // EINTR - interrupted by signal
        }
        task_schedule(); // Yield to other tasks
        
        // Check again after yielding
        if (current_task->pending_signals & ~blocked_signals) {
            return -4;
        }
        
        // Brief busy-wait to prevent tight loop
        for (volatile int i = 0; i < 10000; i++);
    }
}

static int32 sys_rt_sigaction(sys_arg_t signum, sys_arg_t act, sys_arg_t oldact, sys_arg_t sigsetsize) {
    // Set/get signal handler
    if (signum < 1 || signum >= 32) {
        return SYSCALL_EINVAL;
    }
    
    // SIGKILL (9) and SIGSTOP (19) cannot be caught
    if (signum == 9 || signum == 19) {
        return SYSCALL_EINVAL;
    }
    
    // Return old action if requested
    if (oldact) {
        if (!user_buffer_writable((void*)oldact, sizeof(sigaction_t))) {
            return SYSCALL_EFAULT;
        }
        memory_copy((const char*)&signal_handlers[signum], (char*)oldact, sizeof(sigaction_t));
    }
    
    // Set new action if provided
    if (act) {
        if (!user_buffer_readable((const void*)act, sizeof(sigaction_t))) {
            return SYSCALL_EFAULT;
        }
        memory_copy((const char*)act, (char*)&signal_handlers[signum], sizeof(sigaction_t));
    }
    
    return 0;
}

static int32 sys_rt_sigprocmask(sys_arg_t how, sys_arg_t set_ptr, sys_arg_t oldset_ptr, sys_arg_t sigsetsize) {
    // Modify blocked signal mask
    (void)sigsetsize;
    
    // Return old mask if requested
    if (oldset_ptr) {
        if (!user_buffer_writable((void*)oldset_ptr, sizeof(uint32))) {
            return SYSCALL_EFAULT;
        }
        *(uint32*)oldset_ptr = blocked_signals;
    }
    
    // Modify mask if set provided
    if (set_ptr) {
        if (!user_buffer_readable((const void*)set_ptr, sizeof(uint32))) {
            return SYSCALL_EFAULT;
        }
        uint32 set = *(const uint32*)set_ptr;
        
        switch (how) {
            case SIG_BLOCK:
                blocked_signals |= set;
                break;
            case SIG_UNBLOCK:
                blocked_signals &= ~set;
                break;
            case SIG_SETMASK:
                blocked_signals = set;
                break;
            default:
                return SYSCALL_EINVAL;
        }
        
        // SIGKILL and SIGSTOP cannot be blocked
        blocked_signals &= ~((1u << 9) | (1u << 19));
    }
    
    return 0;
}

static int32 sys_rt_sigpending(sys_arg_t set_ptr, sys_arg_t sigsetsize) {
    // Return set of pending signals
    (void)sigsetsize;
    
    if (!set_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)set_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    if (current_task) {
        *(uint32*)set_ptr = current_task->pending_signals & blocked_signals;
    } else {
        *(uint32*)set_ptr = 0;
    }
    
    return 0;
}

static int32 sys_rt_sigtimedwait(sys_arg_t uthese, sys_arg_t uinfo, sys_arg_t uts, sys_arg_t sigsetsize) {
    // Wait for one of the signals in set
    (void)sigsetsize;
    
    if (!uthese) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_readable((const void*)uthese, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32 waitset = *(const uint32*)uthese;
    
    if (!current_task) {
        return SYSCALL_EPERM;
    }
    
    // Check if any of the specified signals is already pending
    uint32 pending = current_task->pending_signals & waitset;
    if (pending) {
        // Find first pending signal
        for (int sig = 1; sig < 32; sig++) {
            if (pending & (1u << sig)) {
                current_task->pending_signals &= ~(1u << sig); // Clear it
                return sig;
            }
        }
    }
    
    // Would need to wait - for now just return EAGAIN
    return -11; // EAGAIN
}

static int32 sys_rt_sigqueueinfo(sys_arg_t pid, sys_arg_t sig, sys_arg_t uinfo) {
    // Queue a signal to a process
    (void)uinfo; // Signal info not fully supported
    
    if (sig < 1 || sig >= 32) {
        return SYSCALL_EINVAL;
    }
    
    if (current_task && (pid == 0 || current_task->id == (uint32)pid)) {
        current_task->pending_signals |= (1u << sig);
        return 0;
    }
    
    return SYSCALL_ESRCH;
}

static int32 sys_rt_sigsuspend(sys_arg_t unewset_ptr, sys_arg_t sigsetsize) {
    // Temporarily replace signal mask and suspend
    (void)sigsetsize;
    
    if (!unewset_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_readable((const void*)unewset_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32 old_mask = blocked_signals;
    uint32 new_mask = *(const uint32*)unewset_ptr;
    
    // SIGKILL and SIGSTOP cannot be blocked
    new_mask &= ~((1u << 9) | (1u << 19));
    
    blocked_signals = new_mask;
    
    // Wait for a signal (simplified - just check once and return)
    if (current_task && (current_task->pending_signals & ~blocked_signals)) {
        blocked_signals = old_mask;
        return -4; // EINTR
    }
    
    // Would block here in real implementation
    task_schedule();
    
    blocked_signals = old_mask;
    return -4; // EINTR - always returns EINTR
}

static int32 sys_sigaltstack(sys_arg_t uss_ptr, sys_arg_t uoss_ptr) {
    // Set/get alternate signal stack
    // Our simple implementation doesn't use alternate stacks
    
    typedef struct {
        void* ss_sp;
        int ss_flags;
        uint32 ss_size;
    } stack_t;
    
    if (uoss_ptr) {
        if (!user_buffer_writable((void*)uoss_ptr, sizeof(stack_t))) {
            return SYSCALL_EFAULT;
        }
        // Return "no alternate stack"
        stack_t* oss = (stack_t*)uoss_ptr;
        oss->ss_sp = NULL;
        oss->ss_flags = 2; // SS_DISABLE
        oss->ss_size = 0;
    }
    
    if (uss_ptr) {
        // Ignore new stack setting - not supported
        if (!user_buffer_readable((const void*)uss_ptr, sizeof(stack_t))) {
            return SYSCALL_EFAULT;
        }
    }
    
    return 0;
}

// Scheduling policies
#define SCHED_NORMAL    0
#define SCHED_FIFO      1
#define SCHED_RR        2
#define SCHED_BATCH     3
#define SCHED_IDLE      5

// Scheduler parameter structure
typedef struct {
    int32 sched_priority;
} sched_param_t;

// Scheduling
static int32 sys_sched_yield(void) {
    // Yield to the scheduler - allow other tasks to run
    task_schedule();
    return 0;
}

static int32 sys_sched_get_priority_max(sys_arg_t policy) {
    // Return maximum priority for scheduling policy
    switch (policy) {
        case SCHED_NORMAL:
        case SCHED_BATCH:
        case SCHED_IDLE:
            return 0; // Non-realtime policies have priority 0
        case SCHED_FIFO:
        case SCHED_RR:
            return 99; // Real-time priorities 1-99
        default:
            return SYSCALL_EINVAL;
    }
}

static int32 sys_sched_get_priority_min(sys_arg_t policy) {
    // Return minimum priority for scheduling policy
    switch (policy) {
        case SCHED_NORMAL:
        case SCHED_BATCH:
        case SCHED_IDLE:
            return 0;
        case SCHED_FIFO:
        case SCHED_RR:
            return 1; // Real-time minimum is 1
        default:
            return SYSCALL_EINVAL;
    }
}

static int32 sys_sched_setparam(sys_arg_t pid, sys_arg_t param_ptr) {
    // Set scheduling parameters
    if (!param_ptr) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)param_ptr, sizeof(sched_param_t))) {
        return SYSCALL_EFAULT;
    }
    
    const sched_param_t* param = (const sched_param_t*)param_ptr;
    
    // Only allow setting for current process (pid 0 or own pid)
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    if (!current_task) {
        return SYSCALL_ESRCH;
    }
    
    // Validate priority (0-99 for realtime, 0 for normal)
    if (param->sched_priority < 0 || param->sched_priority > 99) {
        return SYSCALL_EINVAL;
    }
    
    // Map to our internal priority system
    current_task->priority = (uint32)param->sched_priority;
    
    return 0;
}

static int32 sys_sched_getparam(sys_arg_t pid, sys_arg_t param_ptr) {
    // Get scheduling parameters
    if (!param_ptr) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_writable((void*)param_ptr, sizeof(sched_param_t))) {
        return SYSCALL_EFAULT;
    }
    
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    if (!current_task) {
        return SYSCALL_ESRCH;
    }
    
    sched_param_t* param = (sched_param_t*)param_ptr;
    param->sched_priority = (int32)current_task->priority;
    
    return 0;
}

static int32 sys_sched_setscheduler(sys_arg_t pid, sys_arg_t policy, sys_arg_t param_ptr) {
    // Set scheduling policy and parameters
    if (policy < 0 || policy > 5) {
        return SYSCALL_EINVAL;
    }
    
    if (!param_ptr) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)param_ptr, sizeof(sched_param_t))) {
        return SYSCALL_EFAULT;
    }
    
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    if (!current_task) {
        return SYSCALL_ESRCH;
    }
    
    const sched_param_t* param = (const sched_param_t*)param_ptr;
    
    // Validate priority for policy
    if (policy == SCHED_FIFO || policy == SCHED_RR) {
        if (param->sched_priority < 1 || param->sched_priority > 99) {
            return SYSCALL_EINVAL;
        }
        // Need CAP_SYS_NICE for realtime policies
        if (current_task->uid != 0) {
            return SYSCALL_EPERM;
        }
    } else {
        if (param->sched_priority != 0) {
            return SYSCALL_EINVAL;
        }
    }
    
    current_task->priority = (uint32)param->sched_priority;
    
    return 0;
}

static int32 sys_sched_getscheduler(sys_arg_t pid) {
    // Get scheduling policy
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    if (!current_task) {
        return SYSCALL_ESRCH;
    }
    
    // Return SCHED_NORMAL (our default policy)
    // In a full implementation, we'd track policy per-task
    return SCHED_NORMAL;
}

static int32 sys_sched_setaffinity(sys_arg_t pid, sys_arg_t len, sys_arg_t user_mask_ptr) {
    // Set CPU affinity mask (not fully supported - single CPU system)
    if (!user_mask_ptr || len == 0) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)user_mask_ptr, len)) {
        return SYSCALL_EFAULT;
    }
    
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    // Our system is single-CPU, so accept any mask that includes CPU 0
    const uint8* mask = (const uint8*)user_mask_ptr;
    if (len >= 1 && (mask[0] & 1)) {
        return 0; // CPU 0 is in the mask, accept it
    }
    
    return SYSCALL_EINVAL; // Must include at least CPU 0
}

static int32 sys_sched_getaffinity(sys_arg_t pid, sys_arg_t len, sys_arg_t user_mask_ptr) {
    // Get CPU affinity mask
    if (!user_mask_ptr || len == 0) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_writable((void*)user_mask_ptr, len)) {
        return SYSCALL_EFAULT;
    }
    
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    // Return mask with only CPU 0 set (single CPU system)
    uint8* mask = (uint8*)user_mask_ptr;
    memory_set((char*)mask, 0, len);
    if (len >= 1) {
        mask[0] = 1; // CPU 0 bit set
    }
    
    // Return size of kernel cpu mask (8 bytes for up to 64 CPUs)
    return 8;
}

static int32 sys_sched_rr_get_interval(sys_arg_t pid, sys_arg_t interval_ptr) {
    // Get round-robin time quantum
    if (!interval_ptr) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_writable((void*)interval_ptr, sizeof(struct timespec))) {
        return SYSCALL_EFAULT;
    }
    
    if (pid != 0 && (!current_task || current_task->id != (uint32)pid)) {
        return SYSCALL_ESRCH;
    }
    
    // Return our time slice (typically 10ms = 10000000 nanoseconds)
    struct timespec* ts = (struct timespec*)interval_ptr;
    ts->tv_sec = 0;
    ts->tv_nsec = 10000000; // 10ms
    
    return 0;
}

// Resource limit structure
typedef struct {
    uint32 rlim_cur;  // Soft limit
    uint32 rlim_max;  // Hard limit
} rlimit_t;

// Resource usage structure
typedef struct {
    struct timeval ru_utime;  // User CPU time used
    struct timeval ru_stime;  // System CPU time used
    int32 ru_maxrss;          // Maximum resident set size
    int32 ru_ixrss;           // Integral shared memory size
    int32 ru_idrss;           // Integral unshared data size
    int32 ru_isrss;           // Integral unshared stack size
    int32 ru_minflt;          // Page reclaims (soft page faults)
    int32 ru_majflt;          // Page faults (hard page faults)
    int32 ru_nswap;           // Swaps
    int32 ru_inblock;         // Block input operations
    int32 ru_oublock;         // Block output operations
    int32 ru_msgsnd;          // IPC messages sent
    int32 ru_msgrcv;          // IPC messages received
    int32 ru_nsignals;        // Signals received
    int32 ru_nvcsw;           // Voluntary context switches
    int32 ru_nivcsw;          // Involuntary context switches
} rusage_t;

// System info structure
typedef struct {
    int32 uptime;             // Seconds since boot
    uint32 loads[3];          // 1, 5, and 15 minute load averages
    uint32 totalram;          // Total usable main memory size
    uint32 freeram;           // Available memory size
    uint32 sharedram;         // Amount of shared memory
    uint32 bufferram;         // Memory used by buffers
    uint32 totalswap;         // Total swap space size
    uint32 freeswap;          // Available swap space
    uint16 procs;             // Number of current processes
    uint16 pad;               // Padding
    uint32 totalhigh;         // Total high memory size
    uint32 freehigh;          // Available high memory size
    uint32 mem_unit;          // Memory unit size in bytes
} sysinfo_t;

// Process times structure
typedef struct {
    uint32 tms_utime;   // User CPU time
    uint32 tms_stime;   // System CPU time
    uint32 tms_cutime;  // User CPU time of children
    uint32 tms_cstime;  // System CPU time of children
} tms_t;

// Resource limits
#define RLIMIT_CPU        0
#define RLIMIT_FSIZE      1
#define RLIMIT_DATA       2
#define RLIMIT_STACK      3
#define RLIMIT_CORE       4
#define RLIMIT_RSS        5
#define RLIMIT_NPROC      6
#define RLIMIT_NOFILE     7
#define RLIMIT_MEMLOCK    8
#define RLIMIT_AS         9
#define RLIMIT_LOCKS      10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE   12
#define RLIMIT_NICE       13
#define RLIMIT_RTPRIO     14
#define RLIMIT_RTTIME     15
#define RLIM_NLIMITS      16

#define RLIM_INFINITY     0xFFFFFFFF

// Priority targets
#define PRIO_PROCESS  0
#define PRIO_PGRP     1
#define PRIO_USER     2

// Global system state
static char system_hostname[64] = "forestos";
static char system_domainname[64] = "(none)";

// System information
static int32 sys_gettimeofday(sys_arg_t tv, sys_arg_t tz) {
    (void)tz; // Timezone not supported yet

    if (tv && !user_buffer_writable((void*)tv, sizeof(struct timeval))) {
        return SYSCALL_EFAULT;
    }

    if (tv) {
        struct timeval* tv_ptr = (struct timeval*)tv;
        uint32 ticks = timer_get_ticks();
        tv_ptr->tv_sec = fake_unix_epoch + (ticks / 1000);
        tv_ptr->tv_usec = (ticks % 1000) * 1000;
    }

    return 0;
}

static int32 sys_clock_gettime(sys_arg_t clock_id, sys_arg_t tp) {
    (void)clock_id;
    if (!tp) {
        return SYSCALL_EFAULT;
    }
    if (!user_buffer_writable((void*)tp, sizeof(struct timespec))) {
        return SYSCALL_EFAULT;
    }

    struct timespec* ts_ptr = (struct timespec*)tp;
    uint32 ticks = timer_get_ticks();
    ts_ptr->tv_sec = fake_unix_epoch + (ticks / 1000);
    ts_ptr->tv_nsec = (ticks % 1000) * 1000000;
    return 0;
}

static int32 sys_settimeofday(sys_arg_t tv, sys_arg_t tz) {
    // Set system time (requires root)
    (void)tz;
    
    if (!current_task || current_task->uid != 0) {
        return SYSCALL_EPERM;
    }
    
    if (!tv) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)tv, sizeof(struct timeval))) {
        return SYSCALL_EFAULT;
    }
    
    const struct timeval* tv_ptr = (const struct timeval*)tv;
    fake_unix_epoch = tv_ptr->tv_sec;
    
    return 0;
}

static int32 sys_getrlimit(sys_arg_t resource, sys_arg_t rlim_ptr) {
    // Get resource limits
    if (resource >= RLIM_NLIMITS) {
        return SYSCALL_EINVAL;
    }
    
    if (!rlim_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)rlim_ptr, sizeof(rlimit_t))) {
        return SYSCALL_EFAULT;
    }
    
    rlimit_t* rlim = (rlimit_t*)rlim_ptr;
    
    // Return reasonable defaults
    switch (resource) {
        case RLIMIT_NOFILE:
            rlim->rlim_cur = 1024;
            rlim->rlim_max = 4096;
            break;
        case RLIMIT_STACK:
            rlim->rlim_cur = 8 * 1024 * 1024;  // 8 MB
            rlim->rlim_max = RLIM_INFINITY;
            break;
        case RLIMIT_NPROC:
            rlim->rlim_cur = 1024;
            rlim->rlim_max = 4096;
            break;
        case RLIMIT_AS:
        case RLIMIT_DATA:
        case RLIMIT_RSS:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
        case RLIMIT_CORE:
            rlim->rlim_cur = 0;  // Core dumps disabled
            rlim->rlim_max = RLIM_INFINITY;
            break;
        case RLIMIT_CPU:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
        default:
            rlim->rlim_cur = RLIM_INFINITY;
            rlim->rlim_max = RLIM_INFINITY;
            break;
    }
    
    return 0;
}

static int32 sys_setrlimit(sys_arg_t resource, sys_arg_t rlim_ptr) {
    // Set resource limits
    if (resource >= RLIM_NLIMITS) {
        return SYSCALL_EINVAL;
    }
    
    if (!rlim_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_readable((const void*)rlim_ptr, sizeof(rlimit_t))) {
        return SYSCALL_EFAULT;
    }
    
    const rlimit_t* rlim = (const rlimit_t*)rlim_ptr;
    
    // Soft limit cannot exceed hard limit
    if (rlim->rlim_cur > rlim->rlim_max) {
        return SYSCALL_EINVAL;
    }
    
    // Non-root cannot raise hard limits
    // For simplicity, we accept any valid limit but don't actually enforce them
    (void)rlim;
    
    return 0;
}

static int32 sys_getrusage(sys_arg_t who, sys_arg_t usage_ptr) {
    // Get resource usage
    if (!usage_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)usage_ptr, sizeof(rusage_t))) {
        return SYSCALL_EFAULT;
    }
    
    rusage_t* usage = (rusage_t*)usage_ptr;
    memory_set((char*)usage, 0, sizeof(rusage_t));
    
    // Fill in basic usage info
    if (current_task) {
        uint32 ticks = timer_get_ticks();
        usage->ru_utime.tv_sec = ticks / 1000;
        usage->ru_utime.tv_usec = (ticks % 1000) * 1000;
        usage->ru_maxrss = 4096; // Placeholder RSS in KB
    }
    
    return 0;
}

static int32 sys_sysinfo(sys_arg_t info_ptr) {
    // Get system info
    if (!info_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)info_ptr, sizeof(sysinfo_t))) {
        return SYSCALL_EFAULT;
    }
    
    sysinfo_t* info = (sysinfo_t*)info_ptr;
    memory_set((char*)info, 0, sizeof(sysinfo_t));
    
    uint32 ticks = timer_get_ticks();
    info->uptime = ticks / 1000;
    
    // Load averages (scaled by 65536)
    info->loads[0] = 65536;  // 1.0
    info->loads[1] = 32768;  // 0.5
    info->loads[2] = 16384;  // 0.25
    
    // Memory info (in mem_unit bytes)
    info->mem_unit = 1;
    info->totalram = 128 * 1024 * 1024;   // 128 MB placeholder
    info->freeram = 64 * 1024 * 1024;     // 64 MB free
    info->sharedram = 0;
    info->bufferram = 0;
    info->totalswap = 0;
    info->freeswap = 0;
    info->procs = 1;  // At least one process running
    info->totalhigh = 0;
    info->freehigh = 0;
    
    return 0;
}

static int32 sys_times(sys_arg_t tbuf_ptr) {
    // Get process times
    if (!tbuf_ptr) {
        // Return current clock ticks if no buffer
        return (int32)timer_get_ticks();
    }
    
    if (!user_buffer_writable((void*)tbuf_ptr, sizeof(tms_t))) {
        return SYSCALL_EFAULT;
    }
    
    tms_t* tbuf = (tms_t*)tbuf_ptr;
    uint32 ticks = timer_get_ticks();
    
    tbuf->tms_utime = ticks;     // User time
    tbuf->tms_stime = 0;         // System time
    tbuf->tms_cutime = 0;        // Children user time
    tbuf->tms_cstime = 0;        // Children system time
    
    return (int32)ticks;
}

static int32 sys_getpriority(sys_arg_t which, sys_arg_t who) {
    // Get scheduling priority
    if (which > PRIO_USER) {
        return SYSCALL_EINVAL;
    }
    
    if (!current_task) {
        return SYSCALL_ESRCH;
    }
    
    // For simplicity, only support PRIO_PROCESS with who=0 (current)
    if (which == PRIO_PROCESS && (who == 0 || who == current_task->id)) {
        // Nice values range from -20 to 19, we return 20 - priority
        // to convert to nice value space
        int32 nice = 20 - (int32)current_task->priority;
        if (nice < -20) nice = -20;
        if (nice > 19) nice = 19;
        // getpriority returns 20 - nice to avoid ambiguity with -1 error
        return 20 - nice;
    }
    
    return SYSCALL_ESRCH;
}

static int32 sys_setpriority(sys_arg_t which, sys_arg_t who, sys_arg_t prio) {
    // Set scheduling priority (nice value)
    if (which > PRIO_USER) {
        return SYSCALL_EINVAL;
    }
    
    if (!current_task) {
        return SYSCALL_ESRCH;
    }
    
    // Nice values: -20 (highest priority) to 19 (lowest)
    int32 nice = (int32)prio;
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    
    // Only root can decrease nice value (increase priority)
    if (nice < 0 && current_task->uid != 0) {
        return SYSCALL_EACCES;
    }
    
    if (which == PRIO_PROCESS && (who == 0 || who == current_task->id)) {
        // Convert nice to internal priority
        current_task->priority = (uint32)(20 - nice);
        return 0;
    }
    
    return SYSCALL_ESRCH;
}

static int32 sys_sethostname(sys_arg_t name_ptr, sys_arg_t len) {
    // Set system hostname (requires root)
    if (!current_task || current_task->uid != 0) {
        return SYSCALL_EPERM;
    }
    
    if (!name_ptr || len == 0 || len > 63) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)name_ptr, len)) {
        return SYSCALL_EFAULT;
    }
    
    memory_copy((const char*)name_ptr, system_hostname, len);
    system_hostname[len] = '\0';
    
    return 0;
}

static int32 sys_setdomainname(sys_arg_t name_ptr, sys_arg_t len) {
    // Set NIS/YP domain name (requires root)
    if (!current_task || current_task->uid != 0) {
        return SYSCALL_EPERM;
    }
    
    if (!name_ptr || len == 0 || len > 63) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)name_ptr, len)) {
        return SYSCALL_EFAULT;
    }
    
    memory_copy((const char*)name_ptr, system_domainname, len);
    system_domainname[len] = '\0';
    
    return 0;
}

// Filesystem statistics structure
typedef struct {
    uint32 f_type;      // Type of filesystem
    uint32 f_bsize;     // Optimal transfer block size
    uint32 f_blocks;    // Total data blocks in filesystem
    uint32 f_bfree;     // Free blocks in filesystem
    uint32 f_bavail;    // Free blocks available to unprivileged user
    uint32 f_files;     // Total file nodes in filesystem
    uint32 f_ffree;     // Free file nodes in filesystem
    uint32 f_fsid[2];   // Filesystem ID
    uint32 f_namelen;   // Maximum length of filenames
    uint32 f_frsize;    // Fragment size
    uint32 f_flags;     // Mount flags
    uint32 f_spare[4];  // Padding
} statfs_t;

// Time modification structures
typedef struct {
    uint32 actime;   // Access time
    uint32 modtime;  // Modification time
} utimbuf_t;

// File system attributes
static int32 sys_statfs(sys_arg_t path_ptr, sys_arg_t buf_ptr) {
    // Get filesystem statistics
    if (!path_ptr || !buf_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)buf_ptr, sizeof(statfs_t))) {
        return SYSCALL_EFAULT;
    }
    
    // Check if path exists (simple check)
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    statfs_t* buf = (statfs_t*)buf_ptr;
    memory_set((char*)buf, 0, sizeof(statfs_t));
    
    // Return info for our RAM-based VFS
    buf->f_type = 0x01021994;   // RAMFS magic number
    buf->f_bsize = 4096;        // Block size
    buf->f_frsize = 4096;       // Fragment size
    buf->f_blocks = 1024;       // Total blocks (4 MB)
    buf->f_bfree = 512;         // Free blocks
    buf->f_bavail = 512;        // Available blocks
    buf->f_files = 100;         // Total inodes
    buf->f_ffree = 50;          // Free inodes
    buf->f_namelen = 255;       // Max filename length
    buf->f_fsid[0] = 0;         // Filesystem ID
    buf->f_fsid[1] = 0;
    buf->f_flags = 1;           // ST_RDONLY - read-only filesystem
    
    return 0;
}

static int32 sys_fstatfs(sys_arg_t fd, sys_arg_t buf_ptr) {
    // Get filesystem statistics by file descriptor
    if (!buf_ptr) {
        return SYSCALL_EFAULT;
    }
    
    // Check if fd is valid (stdin/stdout/stderr don't count)
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    if (!user_buffer_writable((void*)buf_ptr, sizeof(statfs_t))) {
        return SYSCALL_EFAULT;
    }
    
    statfs_t* buf = (statfs_t*)buf_ptr;
    memory_set((char*)buf, 0, sizeof(statfs_t));
    
    // Same info as statfs - we have one unified VFS
    buf->f_type = 0x01021994;
    buf->f_bsize = 4096;
    buf->f_frsize = 4096;
    buf->f_blocks = 1024;
    buf->f_bfree = 512;
    buf->f_bavail = 512;
    buf->f_files = 100;
    buf->f_ffree = 50;
    buf->f_namelen = 255;
    buf->f_flags = 1;
    
    return 0;
}

static int32 sys_utime(sys_arg_t filename_ptr, sys_arg_t times_ptr) {
    // Set file access and modification times (legacy interface)
    if (!filename_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)filename_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    // Check if file exists
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    // Our VFS is read-only, but we pretend success
    // In a writable FS, we'd update the node's timestamps
    if (times_ptr) {
        if (!user_buffer_readable((const void*)times_ptr, sizeof(utimbuf_t))) {
            return SYSCALL_EFAULT;
        }
    }
    
    // Read-only filesystem - times aren't actually updated
    return 0;
}

static int32 sys_utimes(sys_arg_t filename_ptr, sys_arg_t times_ptr) {
    // Set file times with microsecond precision
    if (!filename_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)filename_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    if (times_ptr) {
        if (!user_buffer_readable((const void*)times_ptr, 2 * sizeof(struct timeval))) {
            return SYSCALL_EFAULT;
        }
    }
    
    // Read-only - pretend success
    return 0;
}

static int32 sys_utimensat(sys_arg_t dirfd, sys_arg_t pathname_ptr, sys_arg_t times_ptr, sys_arg_t flags) {
    // Set file times with nanosecond precision
    (void)dirfd;   // We don't support AT_FDCWD yet
    (void)flags;   // AT_SYMLINK_NOFOLLOW not needed
    
    if (!pathname_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)pathname_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    if (times_ptr) {
        if (!user_buffer_readable((const void*)times_ptr, 2 * sizeof(struct timespec))) {
            return SYSCALL_EFAULT;
        }
    }
    
    return 0;
}

// Extended attributes - our filesystem doesn't support xattrs yet
// These return ENOTSUP (operation not supported) or ENODATA (attribute not found)
#define SYSCALL_ENOTSUP (-95)  // Operation not supported
#define SYSCALL_ENODATA (-61)  // No data available

// Extended attributes
static int32 sys_setxattr(sys_arg_t path_ptr, sys_arg_t name_ptr, sys_arg_t value_ptr, sys_arg_t size, sys_arg_t flags) {
    // Set extended attribute on file
    (void)value_ptr;
    (void)size;
    (void)flags;
    
    if (!path_ptr || !name_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    // Check if file exists
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    // Extended attributes not supported on our read-only VFS
    return SYSCALL_ENOTSUP;
}

static int32 sys_getxattr(sys_arg_t path_ptr, sys_arg_t name_ptr, sys_arg_t value_ptr, sys_arg_t size) {
    // Get extended attribute value
    (void)value_ptr;
    (void)size;
    
    if (!path_ptr || !name_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    // No extended attributes exist
    return SYSCALL_ENODATA;
}

static int32 sys_listxattr(sys_arg_t path_ptr, sys_arg_t list_ptr, sys_arg_t size) {
    // List extended attribute names
    (void)list_ptr;
    (void)size;
    
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    // No extended attributes - return 0 (empty list)
    return 0;
}

static int32 sys_removexattr(sys_arg_t path_ptr, sys_arg_t name_ptr) {
    // Remove extended attribute
    if (!path_ptr || !name_ptr) {
        return SYSCALL_EFAULT;
    }
    
    char path_buf[256];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }
    
    vfs_node_t* node = vfs_open(path_buf, VFS_READ);
    if (!node) {
        return SYSCALL_ENOENT;
    }
    
    // Attribute doesn't exist (none do)
    return SYSCALL_ENODATA;
}

static int32 sys_lsetxattr(sys_arg_t path_ptr, sys_arg_t name_ptr, sys_arg_t value_ptr, sys_arg_t size, sys_arg_t flags) {
    // Set xattr, don't follow symlinks (same as setxattr for us - no symlink support)
    return sys_setxattr(path_ptr, name_ptr, value_ptr, size, flags);
}

static int32 sys_lgetxattr(sys_arg_t path_ptr, sys_arg_t name_ptr, sys_arg_t value_ptr, sys_arg_t size) {
    // Get xattr, don't follow symlinks
    return sys_getxattr(path_ptr, name_ptr, value_ptr, size);
}

static int32 sys_llistxattr(sys_arg_t path_ptr, sys_arg_t list_ptr, sys_arg_t size) {
    // List xattrs, don't follow symlinks
    return sys_listxattr(path_ptr, list_ptr, size);
}

static int32 sys_lremovexattr(sys_arg_t path_ptr, sys_arg_t name_ptr) {
    // Remove xattr, don't follow symlinks
    return sys_removexattr(path_ptr, name_ptr);
}

static int32 sys_fsetxattr(sys_arg_t fd, sys_arg_t name_ptr, sys_arg_t value_ptr, sys_arg_t size, sys_arg_t flags) {
    // Set xattr by file descriptor
    (void)name_ptr;
    (void)value_ptr;
    (void)size;
    (void)flags;
    
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    return SYSCALL_ENOTSUP;
}

static int32 sys_fgetxattr(sys_arg_t fd, sys_arg_t name_ptr, sys_arg_t value_ptr, sys_arg_t size) {
    // Get xattr by file descriptor
    (void)name_ptr;
    (void)value_ptr;
    (void)size;
    
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    return SYSCALL_ENODATA;
}

static int32 sys_flistxattr(sys_arg_t fd, sys_arg_t list_ptr, sys_arg_t size) {
    // List xattrs by file descriptor
    (void)list_ptr;
    (void)size;
    
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    return 0; // Empty list
}

static int32 sys_fremovexattr(sys_arg_t fd, sys_arg_t name_ptr) {
    // Remove xattr by file descriptor
    (void)name_ptr;
    
    if (fd < 3) {
        return SYSCALL_EBADF;
    }
    
    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    
    return SYSCALL_ENODATA;
}

// Socket shutdown modes
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

// Socket options levels
#define SOL_SOCKET  1
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

// Socket options
#define SO_REUSEADDR  2
#define SO_KEEPALIVE  9
#define SO_BROADCAST  6
#define SO_SNDBUF     7
#define SO_RCVBUF     8
#define SO_ERROR      4
#define SO_TYPE       3

// Message header for sendmsg/recvmsg
typedef struct {
    void* msg_name;           // Optional address
    uint32 msg_namelen;       // Address length
    void* msg_iov;            // Scatter/gather array
    uint32 msg_iovlen;        // Number of elements in msg_iov
    void* msg_control;        // Ancillary data
    uint32 msg_controllen;    // Ancillary data length
    int32 msg_flags;          // Flags on received message
} msghdr_t;

// iovec_t is already defined earlier in this file

// Socket connection state (simplified)
typedef struct {
    bool connected;
    uint32 peer_addr;
    uint16 peer_port;
} socket_state_t;

static socket_state_t socket_states[16]; // State for up to 16 sockets

// Networking (continued)
static int32 sys_connect(sys_arg_t fd, sys_arg_t addr_ptr, sys_arg_t addrlen) {
    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (!addr_ptr || addrlen < sizeof(sockaddr_un_t)) {
            return SYSCALL_EINVAL;
        }
        if (!user_buffer_readable((const void*)addr_ptr, sizeof(sockaddr_un_t))) {
            return SYSCALL_EFAULT;
        }
        sockaddr_un_t uaddr;
        memory_copy((char*)addr_ptr, (char*)&uaddr, sizeof(uaddr));
        if (!(uaddr.sun_family == AF_UNIX || uaddr.sun_family == AF_LOCAL)) {
            return SYSCALL_EINVAL;
        }
        unix_path_socket_t* listener = find_unix_path_listener_by_path(uaddr.sun_path);
        if (!listener) {
            return SYSCALL_ENOENT;
        }
        int conn_id = alloc_unix_path_connection();
        if (conn_id < 0) {
            return SYSCALL_ENFILE;
        }
        unix_path_connection_t* conn = &unix_path_connections[conn_id];
        conn->fd_a = usock->fd;
        conn->fd_b = -1;
        usock->connected = true;
        usock->connection_id = conn_id;
        memory_copy(listener->path, usock->peer_path, sizeof(usock->peer_path));
        if (unix_path_enqueue_pending(listener, conn_id) != 0) {
            memory_set((uint8*)conn, 0, sizeof(*conn));
            usock->connected = false;
            usock->connection_id = -1;
            return SYSCALL_EAGAIN;
        }
        return 0;
    }

    // Connect socket to address
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    if (!addr_ptr || addrlen < sizeof(sockaddr_in_t)) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_readable((const void*)addr_ptr, addrlen)) {
        return SYSCALL_EFAULT;
    }
    
    const sockaddr_in_t* addr = (const sockaddr_in_t*)addr_ptr;
    
    // Store connection info
    uint32 idx = fd % 16;
    socket_states[idx].connected = true;
    socket_states[idx].peer_addr = addr->sin_addr;
    socket_states[idx].peer_port = ntohs(addr->sin_port);
    
    // For UDP sockets, connect just sets the default destination
    // For TCP, we'd need actual connection establishment
    return 0;
}

static int32 sys_accept(sys_arg_t fd, sys_arg_t addr_ptr, sys_arg_t addrlen_ptr) {
    unix_path_socket_t* listener = find_unix_path_socket_by_fd((int)fd);
    if (listener) {
        if (!listener->listening) {
            return SYSCALL_EINVAL;
        }
        int conn_id = unix_path_dequeue_pending(listener);
        if (conn_id < 0 || conn_id >= MAX_UNIX_PATH_CONNECTIONS) {
            return SYSCALL_EAGAIN;
        }

        int accepted_fd = alloc_local_fd_slot();
        if (accepted_fd < 0) {
            return SYSCALL_EMFILE;
        }

        int sock_idx = -1;
        for (uint32 i = 0; i < MAX_UNIX_PATH_SOCKETS; i++) {
            if (!unix_path_sockets[i].used) {
                sock_idx = (int)i;
                break;
            }
        }
        if (sock_idx < 0) {
            free_local_fd_slot(accepted_fd);
            return SYSCALL_ENFILE;
        }

        unix_path_connection_t* conn = &unix_path_connections[conn_id];
        if (!conn->used) {
            free_local_fd_slot(accepted_fd);
            return SYSCALL_EINVAL;
        }

        conn->fd_b = accepted_fd;
        unix_path_socket_t* accepted = &unix_path_sockets[sock_idx];
        memory_set((uint8*)accepted, 0, sizeof(*accepted));
        accepted->used = true;
        accepted->fd = accepted_fd;
        accepted->type = listener->type;
        accepted->connected = true;
        accepted->connection_id = conn_id;
        accepted->bound = false;
        accepted->listening = false;
        memory_copy((const char*)listener->path, accepted->peer_path, sizeof(accepted->peer_path));

        if (addr_ptr && addrlen_ptr &&
            user_buffer_writable((void*)addrlen_ptr, sizeof(uint32)) &&
            user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_un_t))) {
            uint32* plen = (uint32*)addrlen_ptr;
            if (*plen >= sizeof(sockaddr_un_t)) {
                sockaddr_un_t* out = (sockaddr_un_t*)addr_ptr;
                out->sun_family = AF_UNIX;
                memory_set((uint8*)out->sun_path, 0, sizeof(out->sun_path));
                memory_copy((const char*)listener->path, (char*)out->sun_path, sizeof(out->sun_path));
                *plen = sizeof(sockaddr_un_t);
            }
        }
        return accepted_fd;
    }

    // Accept connection on listening socket
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    // Our simple network stack doesn't support TCP accept yet
    // Return "would block" since we have no pending connections
    return -11; // EAGAIN
}

static int32 sys_sendmsg(sys_arg_t fd, sys_arg_t msg_ptr, sys_arg_t flags) {
    // Send message on socket
    (void)flags;

    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (!msg_ptr || !user_buffer_readable((const void*)msg_ptr, sizeof(msghdr_t))) {
            return SYSCALL_EFAULT;
        }
        const msghdr_t* msg = (const msghdr_t*)msg_ptr;
        if (!msg->msg_iov || msg->msg_iovlen == 0) {
            return 0;
        }
        if (!user_buffer_readable((const void*)msg->msg_iov, msg->msg_iovlen * sizeof(iovec_t))) {
            return SYSCALL_EFAULT;
        }
        const iovec_t* iov = (const iovec_t*)msg->msg_iov;
        int32 total_sent = 0;
        for (uint32 i = 0; i < msg->msg_iovlen; i++) {
            if (!iov[i].iov_base || iov[i].iov_len == 0) {
                continue;
            }
            if (!user_buffer_readable(iov[i].iov_base, iov[i].iov_len)) {
                return total_sent > 0 ? total_sent : SYSCALL_EFAULT;
            }
            int32 sent = unix_path_send((int)fd, (const uint8*)iov[i].iov_base, (uint32)iov[i].iov_len);
            if (sent < 0) {
                return total_sent > 0 ? total_sent : sent;
            }
            total_sent += sent;
            if ((uint32)sent < iov[i].iov_len) {
                break;
            }
        }
        return total_sent;
    }
    
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    if (!msg_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_readable((const void*)msg_ptr, sizeof(msghdr_t))) {
        return SYSCALL_EFAULT;
    }
    
    const msghdr_t* msg = (const msghdr_t*)msg_ptr;
    
    // Gather data from iov array
    if (!msg->msg_iov || msg->msg_iovlen == 0) {
        return 0; // Nothing to send
    }
    
    if (!user_buffer_readable((const void*)msg->msg_iov, msg->msg_iovlen * sizeof(iovec_t))) {
        return SYSCALL_EFAULT;
    }
    
    const iovec_t* iov = (const iovec_t*)msg->msg_iov;
    int32 total_sent = 0;
    
    // Get destination from msg_name or connected peer
    uint32 dest_addr = INADDR_LOOPBACK;
    uint16 dest_port = 0;
    
    if (msg->msg_name && msg->msg_namelen >= sizeof(sockaddr_in_t)) {
        const sockaddr_in_t* dest = (const sockaddr_in_t*)msg->msg_name;
        dest_addr = dest->sin_addr;
        dest_port = ntohs(dest->sin_port);
    } else {
        uint32 idx = fd % 16;
        if (socket_states[idx].connected) {
            dest_addr = socket_states[idx].peer_addr;
            dest_port = socket_states[idx].peer_port;
        } else {
            return -89; // EDESTADDRREQ - destination address required
        }
    }
    
    // Send each iov buffer
    for (uint32 i = 0; i < msg->msg_iovlen; i++) {
        if (iov[i].iov_base && iov[i].iov_len > 0) {
            if (!user_buffer_readable(iov[i].iov_base, iov[i].iov_len)) {
                return SYSCALL_EFAULT;
            }
            int32 sent = net_send_datagram(fd, (const uint8*)iov[i].iov_base, 
                                          iov[i].iov_len, dest_addr, dest_port);
            if (sent < 0) return sent;
            total_sent += sent;
        }
    }
    
    return total_sent;
}

static int32 sys_recvmsg(sys_arg_t fd, sys_arg_t msg_ptr, sys_arg_t flags) {
    // Receive message from socket
    (void)flags;

    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (!msg_ptr || !user_buffer_writable((void*)msg_ptr, sizeof(msghdr_t))) {
            return SYSCALL_EFAULT;
        }
        msghdr_t* msg = (msghdr_t*)msg_ptr;
        if (!msg->msg_iov || msg->msg_iovlen == 0) {
            return 0;
        }
        if (!user_buffer_writable((void*)msg->msg_iov, msg->msg_iovlen * sizeof(iovec_t))) {
            return SYSCALL_EFAULT;
        }
        iovec_t* iov = (iovec_t*)msg->msg_iov;
        for (uint32 i = 0; i < msg->msg_iovlen; i++) {
            if (!iov[i].iov_base || iov[i].iov_len == 0) {
                continue;
            }
            if (!user_buffer_writable(iov[i].iov_base, iov[i].iov_len)) {
                return SYSCALL_EFAULT;
            }
            return unix_path_recv((int)fd, (uint8*)iov[i].iov_base, (uint32)iov[i].iov_len);
        }
        return 0;
    }
    
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    if (!msg_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)msg_ptr, sizeof(msghdr_t))) {
        return SYSCALL_EFAULT;
    }
    
    msghdr_t* msg = (msghdr_t*)msg_ptr;
    
    if (!msg->msg_iov || msg->msg_iovlen == 0) {
        return 0;
    }
    
    if (!user_buffer_writable((void*)msg->msg_iov, msg->msg_iovlen * sizeof(iovec_t))) {
        return SYSCALL_EFAULT;
    }
    
    iovec_t* iov = (iovec_t*)msg->msg_iov;
    
    // Receive into first buffer with space
    for (uint32 i = 0; i < msg->msg_iovlen; i++) {
        if (iov[i].iov_base && iov[i].iov_len > 0) {
            if (!user_buffer_writable(iov[i].iov_base, iov[i].iov_len)) {
                return SYSCALL_EFAULT;
            }
            
            uint32 src_addr = 0;
            uint16 src_port = 0;
            int32 received = net_recv_datagram(fd, (uint8*)iov[i].iov_base,
                                              iov[i].iov_len, &src_addr, &src_port);
            
            // Fill in source address if requested
            if (received > 0 && msg->msg_name && msg->msg_namelen >= sizeof(sockaddr_in_t)) {
                sockaddr_in_t* src = (sockaddr_in_t*)msg->msg_name;
                src->sin_family = AF_INET;
                src->sin_port = htons(src_port);
                src->sin_addr = src_addr;
            }
            
            return received;
        }
    }
    
    return 0;
}

static int32 sys_shutdown(sys_arg_t fd, sys_arg_t how) {
    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (how > SHUT_RDWR) {
            return SYSCALL_EINVAL;
        }
        if (usock->connection_id >= 0 && usock->connection_id < MAX_UNIX_PATH_CONNECTIONS) {
            unix_path_connection_t* conn = &unix_path_connections[usock->connection_id];
            if (conn->used) {
                if (conn->fd_a == usock->fd) {
                    if (how == SHUT_WR || how == SHUT_RDWR) conn->a_closed = true;
                } else if (conn->fd_b == usock->fd) {
                    if (how == SHUT_WR || how == SHUT_RDWR) conn->b_closed = true;
                }
            }
        }
        return 0;
    }

    // Shut down part of a full-duplex connection
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    if (how > SHUT_RDWR) {
        return SYSCALL_EINVAL;
    }
    
    // For our simple implementation, mark socket as disconnected
    uint32 idx = fd % 16;
    if (how == SHUT_RDWR || how == SHUT_WR) {
        socket_states[idx].connected = false;
    }
    
    return 0;
}

static int32 sys_listen(sys_arg_t fd, sys_arg_t backlog) {
    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        (void)backlog;
        usock->listening = true;
        return 0;
    }

    // Mark socket as listening for connections
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    (void)backlog; // We don't actually queue connections
    
    // Mark as listening (would need more state tracking)
    return 0;
}

static int32 sys_getsockname(sys_arg_t fd, sys_arg_t addr_ptr, sys_arg_t addrlen_ptr) {
    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (!addr_ptr || !addrlen_ptr) {
            return SYSCALL_EFAULT;
        }
        if (!user_buffer_writable((void*)addrlen_ptr, sizeof(uint32))) {
            return SYSCALL_EFAULT;
        }
        uint32* addrlen = (uint32*)addrlen_ptr;
        if (*addrlen < sizeof(sockaddr_un_t)) {
            return SYSCALL_EINVAL;
        }
        if (!user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_un_t))) {
            return SYSCALL_EFAULT;
        }
        sockaddr_un_t* out = (sockaddr_un_t*)addr_ptr;
        out->sun_family = AF_UNIX;
        memory_set((uint8*)out->sun_path, 0, sizeof(out->sun_path));
        if (usock->bound) {
            memory_copy((const char*)usock->path, (char*)out->sun_path, sizeof(out->sun_path));
        }
        *addrlen = sizeof(sockaddr_un_t);
        return 0;
    }

    // Get local socket address
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    if (!addr_ptr || !addrlen_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)addrlen_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32* addrlen = (uint32*)addrlen_ptr;
    if (*addrlen < sizeof(sockaddr_in_t)) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_in_t))) {
        return SYSCALL_EFAULT;
    }
    
    sockaddr_in_t* addr = (sockaddr_in_t*)addr_ptr;
    
    // Return local address (loopback)
    addr->sin_family = AF_INET;
    addr->sin_port = 0;  // Would need to track bound port
    addr->sin_addr = INADDR_LOOPBACK;
    memory_set((char*)addr->sin_zero, 0, 8);
    
    *addrlen = sizeof(sockaddr_in_t);
    
    return 0;
}

static int32 sys_getpeername(sys_arg_t fd, sys_arg_t addr_ptr, sys_arg_t addrlen_ptr) {
    unix_path_socket_t* usock = find_unix_path_socket_by_fd((int)fd);
    if (usock) {
        if (!usock->connected) {
            return -107;
        }
        if (!addr_ptr || !addrlen_ptr) {
            return SYSCALL_EFAULT;
        }
        if (!user_buffer_writable((void*)addrlen_ptr, sizeof(uint32))) {
            return SYSCALL_EFAULT;
        }
        uint32* addrlen = (uint32*)addrlen_ptr;
        if (*addrlen < sizeof(sockaddr_un_t)) {
            return SYSCALL_EINVAL;
        }
        if (!user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_un_t))) {
            return SYSCALL_EFAULT;
        }
        sockaddr_un_t* out = (sockaddr_un_t*)addr_ptr;
        out->sun_family = AF_UNIX;
        memory_set((uint8*)out->sun_path, 0, sizeof(out->sun_path));
        memory_copy((const char*)usock->peer_path, (char*)out->sun_path, sizeof(out->sun_path));
        *addrlen = sizeof(sockaddr_un_t);
        return 0;
    }

    // Get peer socket address
    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    uint32 idx = fd % 16;
    if (!socket_states[idx].connected) {
        return -107; // ENOTCONN - socket not connected
    }
    
    if (!addr_ptr || !addrlen_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)addrlen_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32* addrlen = (uint32*)addrlen_ptr;
    if (*addrlen < sizeof(sockaddr_in_t)) {
        return SYSCALL_EINVAL;
    }
    
    if (!user_buffer_writable((void*)addr_ptr, sizeof(sockaddr_in_t))) {
        return SYSCALL_EFAULT;
    }
    
    sockaddr_in_t* addr = (sockaddr_in_t*)addr_ptr;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(socket_states[idx].peer_port);
    addr->sin_addr = socket_states[idx].peer_addr;
    memory_set((char*)addr->sin_zero, 0, 8);
    
    *addrlen = sizeof(sockaddr_in_t);
    
    return 0;
}

static int32 sys_socketpair(sys_arg_t domain, sys_arg_t type, sys_arg_t protocol, sys_arg_t sv_ptr) {
    // Minimal AF_UNIX socketpair implementation with in-kernel ring buffers.
    (void)protocol;
    
    if (!sv_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)sv_ptr, 2 * sizeof(int32))) {
        return SYSCALL_EFAULT;
    }
    
    if (!(domain == AF_UNIX || domain == AF_LOCAL)) {
        return SYSCALL_EINVAL;
    }
    if (!(type == SOCK_STREAM || type == SOCK_DGRAM)) {
        return SYSCALL_EINVAL;
    }

    int pair_idx = -1;
    for (uint32 i = 0; i < MAX_UNIX_SOCKETPAIRS; i++) {
        if (!unix_socketpairs[i].used) {
            pair_idx = (int)i;
            break;
        }
    }
    if (pair_idx < 0) {
        return SYSCALL_ENFILE;
    }

    int fd_a_slot = -1;
    int fd_b_slot = -1;
    for (uint32 i = 0; i < MAX_VFS_HANDLES; i++) {
        if (!vfs_handles[i].used) {
            if (fd_a_slot < 0) {
                fd_a_slot = (int)i;
            } else {
                fd_b_slot = (int)i;
                break;
            }
        }
    }
    if (fd_a_slot < 0 || fd_b_slot < 0) {
        return SYSCALL_EMFILE;
    }

    unix_socketpair_t* pair = &unix_socketpairs[pair_idx];
    memory_set((uint8*)pair, 0, sizeof(*pair));
    pair->used = true;
    pair->fd_a = fd_a_slot + 3;
    pair->fd_b = fd_b_slot + 3;

    vfs_handles[fd_a_slot].used = true;
    vfs_handles[fd_a_slot].data = NULL;
    vfs_handles[fd_a_slot].size = 0;
    vfs_handles[fd_a_slot].offset = 0;
    vfs_device_handles[fd_a_slot].used = false;
    vfs_device_handles[fd_a_slot].node = NULL;

    vfs_handles[fd_b_slot].used = true;
    vfs_handles[fd_b_slot].data = NULL;
    vfs_handles[fd_b_slot].size = 0;
    vfs_handles[fd_b_slot].offset = 0;
    vfs_device_handles[fd_b_slot].used = false;
    vfs_device_handles[fd_b_slot].node = NULL;

    int32* sv = (int32*)sv_ptr;
    sv[0] = pair->fd_a;
    sv[1] = pair->fd_b;
    return 0;
}

static int32 sys_setsockopt(sys_arg_t fd, sys_arg_t level, sys_arg_t optname, sys_arg_t optval_ptr, sys_arg_t optlen) {
    // Set socket option
    if (find_unix_path_socket_by_fd((int)fd)) {
        (void)level;
        (void)optname;
        (void)optval_ptr;
        (void)optlen;
        return 0;
    }

    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    (void)level;
    (void)optname;
    (void)optval_ptr;
    (void)optlen;
    
    // Accept common options but don't actually do anything
    // (our simple network stack doesn't need these)
    switch (optname) {
        case SO_REUSEADDR:
        case SO_KEEPALIVE:
        case SO_BROADCAST:
        case SO_SNDBUF:
        case SO_RCVBUF:
            return 0; // Accept and ignore
        default:
            return 0; // Accept unknown options too
    }
}

static int32 sys_getsockopt(sys_arg_t fd, sys_arg_t level, sys_arg_t optname, sys_arg_t optval_ptr, sys_arg_t optlen_ptr) {
    // Get socket option
    if (find_unix_path_socket_by_fd((int)fd)) {
        if (!optval_ptr || !optlen_ptr) {
            return SYSCALL_EFAULT;
        }
        if (!user_buffer_writable((void*)optlen_ptr, sizeof(uint32))) {
            return SYSCALL_EFAULT;
        }
        uint32* optlen = (uint32*)optlen_ptr;
        if (!user_buffer_writable((void*)optval_ptr, *optlen)) {
            return SYSCALL_EFAULT;
        }
        if (*optlen >= sizeof(int32)) {
            *(int32*)optval_ptr = 0;
            *optlen = sizeof(int32);
            return 0;
        }
        return SYSCALL_EINVAL;
    }

    if (!net_is_fd(fd)) {
        return SYSCALL_EBADF;
    }
    
    (void)level;
    
    if (!optval_ptr || !optlen_ptr) {
        return SYSCALL_EFAULT;
    }
    
    if (!user_buffer_writable((void*)optlen_ptr, sizeof(uint32))) {
        return SYSCALL_EFAULT;
    }
    
    uint32* optlen = (uint32*)optlen_ptr;
    
    if (!user_buffer_writable((void*)optval_ptr, *optlen)) {
        return SYSCALL_EFAULT;
    }
    
    // Return reasonable defaults
    switch (optname) {
        case SO_ERROR: {
            if (*optlen >= sizeof(int32)) {
                *(int32*)optval_ptr = 0; // No error
                *optlen = sizeof(int32);
            }
            break;
        }
        case SO_TYPE: {
            if (*optlen >= sizeof(int32)) {
                *(int32*)optval_ptr = SOCK_DGRAM; // Default to UDP
                *optlen = sizeof(int32);
            }
            break;
        }
        case SO_SNDBUF:
        case SO_RCVBUF: {
            if (*optlen >= sizeof(int32)) {
                *(int32*)optval_ptr = 65536; // 64K buffer
                *optlen = sizeof(int32);
            }
            break;
        }
        default:
            return SYSCALL_EINVAL;
    }
    
    return 0;
}

static int32 sys_clone(sys_arg_t clone_flags, sys_arg_t newsp, sys_arg_t parent_tidptr, sys_arg_t child_tidptr, sys_arg_t tls_val) {
    // For simplicity, implement clone as fork with basic flag support
    (void)parent_tidptr; // Not supported yet
    (void)child_tidptr;  // Not supported yet
    (void)tls_val;       // Not supported yet
    
    // Check if this is a child process returning from clone
    if (current_task && current_task->exit_code == FORK_CHILD_RETURN) {
        current_task->exit_code = 0; // Reset for normal exit
        return 0; // Child returns 0
    }
    
    // Basic clone flags support
    if (clone_flags & ~0x00010000) { // Only support SIGCHLD flag for now
        return SYSCALL_EINVAL;
    }
    
    // For simple implementation, use fork
    task_t* child_task = task_clone_current();
    if (!child_task) {
        return SYSCALL_ENOMEM; // Out of memory
    }
    
    // Return child PID to parent
    return child_task->id;
}

// Futex operations
#define FUTEX_WAIT   0
#define FUTEX_WAKE   1
#define FUTEX_FD     2
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_CMP_REQUEUE_PI 12

static int32 sys_futex(sys_arg_t uaddr, sys_arg_t op, sys_arg_t val, sys_arg_t utime, sys_arg_t uaddr2, sys_arg_t val3) {
    if (!uaddr) {
        return SYSCALL_EFAULT;
    }
    
    // For our simple implementation, only support basic FUTEX_WAIT and FUTEX_WAKE
    switch (op) {
        case FUTEX_WAIT:
            // Validate user address is readable
            if (!user_buffer_readable((const void*)uaddr, sizeof(uint32))) {
                return SYSCALL_EFAULT;
            }
            // For simplicity, just return -1 (would block in real implementation)
            return SYSCALL_ENOSYS;
            
        case FUTEX_WAKE:
            // Wake up waiters - for simplicity, just return 0 (no waiters woken)
            return 0;
            
        case FUTEX_FD:
        case FUTEX_REQUEUE:
        case FUTEX_CMP_REQUEUE:
        case FUTEX_WAKE_OP:
        case FUTEX_LOCK_PI:
        case FUTEX_UNLOCK_PI:
        case FUTEX_TRYLOCK_PI:
        case FUTEX_WAIT_BITSET:
        case FUTEX_WAKE_BITSET:
        case FUTEX_WAIT_REQUEUE_PI:
        case FUTEX_CMP_REQUEUE_PI:
            // Advanced futex operations not supported
            return SYSCALL_ENOSYS;
            
        default:
            return SYSCALL_EINVAL;
    }
    
    (void)val;    // Value parameter unused in simple implementation
    (void)utime;  // Timeout unused in simple implementation
    (void)uaddr2; // Second address unused in simple implementation
    (void)val3;   // Third value unused in simple implementation
}

// Poll file descriptor structure
typedef struct {
    int32 fd;         // File descriptor
    int16 events;      // Requested events
    int16 revents;     // Returned events
} pollfd_t;

// Poll event flags
#define POLLIN      0x001
#define POLLPRI     0x002
#define POLLOUT     0x004
#define POLLERR     0x008
#define POLLHUP     0x010
#define POLLNVAL    0x020

// I/O multiplexing
static int32 sys_poll(sys_arg_t ufds, sys_arg_t nfds, sys_arg_t timeout) {
    if (!ufds || nfds == 0) {
        return SYSCALL_EINVAL;
    }
    
    // Validate user buffer
    if (!user_buffer_readable((const void*)ufds, nfds * sizeof(pollfd_t)) ||
        !user_buffer_writable((void*)ufds, nfds * sizeof(pollfd_t))) {
        return SYSCALL_EFAULT;
    }
    
    pollfd_t* user_fds = (pollfd_t*)ufds;
    int32 ready_count = 0;
    
    // For simplicity, check each file descriptor for basic readiness
    for (uint32 i = 0; i < nfds; i++) {
        if (!user_buffer_readable(&user_fds[i], sizeof(pollfd_t)) ||
            !user_buffer_writable(&user_fds[i], sizeof(pollfd_t))) {
            return SYSCALL_EFAULT;
        }
        
        int32 fd = user_fds[i].fd;
        int16 events = user_fds[i].events;
        user_fds[i].revents = 0;
        
        // Check if file descriptor is valid
        if (fd < 0) {
            user_fds[i].revents |= POLLNVAL;
            continue;
        }
        
        // Basic readiness check
        if (events & POLLIN) {
            bool readable = false;
            if (fd == 0) {
                readable = true;
            } else {
                bool pipe_is_write_end = false;
                pipe_t* pipe = find_pipe_by_fd(fd, &pipe_is_write_end);
                if (pipe && !pipe_is_write_end && pipe_available_bytes(pipe) > 0) {
                    readable = true;
                }

                bool unix_is_a_side = false;
                unix_socketpair_t* pair = find_unix_socketpair_by_fd(fd, &unix_is_a_side);
                if (pair) {
                    uint32 avail = unix_is_a_side
                        ? ring_available(pair->b_to_a_read_pos, pair->b_to_a_write_pos, UNIX_SOCKET_BUFFER_SIZE)
                        : ring_available(pair->a_to_b_read_pos, pair->a_to_b_write_pos, UNIX_SOCKET_BUFFER_SIZE);
                    if (avail > 0) {
                        readable = true;
                    }
                }

                bool pty_is_master = false;
                pty_t* pty = find_pty_by_fd(fd, &pty_is_master);
                if (pty) {
                    uint32 avail = pty_is_master
                        ? ring_available(pty->s2m_read_pos, pty->s2m_write_pos, PTY_BUFFER_SIZE)
                        : ring_available(pty->m2s_read_pos, pty->m2s_write_pos, PTY_BUFFER_SIZE);
                    if (avail > 0) {
                        readable = true;
                    }
                }
            }
            if (readable) {
                user_fds[i].revents |= POLLIN;
                ready_count++;
            }
        }
        
        if (events & POLLOUT) {
            bool writable = false;
            if (fd == 1 || fd == 2) {
                writable = true;
            } else {
                bool pipe_is_write_end = false;
                pipe_t* pipe = find_pipe_by_fd(fd, &pipe_is_write_end);
                if (pipe && pipe_is_write_end && pipe_free_bytes(pipe) > 0) {
                    writable = true;
                }

                bool unix_is_a_side = false;
                unix_socketpair_t* pair = find_unix_socketpair_by_fd(fd, &unix_is_a_side);
                if (pair) {
                    uint32 free_space = unix_is_a_side
                        ? ring_free(pair->a_to_b_read_pos, pair->a_to_b_write_pos, UNIX_SOCKET_BUFFER_SIZE)
                        : ring_free(pair->b_to_a_read_pos, pair->b_to_a_write_pos, UNIX_SOCKET_BUFFER_SIZE);
                    if (free_space > 0) {
                        writable = true;
                    }
                }

                bool pty_is_master = false;
                pty_t* pty = find_pty_by_fd(fd, &pty_is_master);
                if (pty) {
                    uint32 free_space = pty_is_master
                        ? ring_free(pty->m2s_read_pos, pty->m2s_write_pos, PTY_BUFFER_SIZE)
                        : ring_free(pty->s2m_read_pos, pty->s2m_write_pos, PTY_BUFFER_SIZE);
                    if (free_space > 0) {
                        writable = true;
                    }
                }
            }
            if (writable) {
                user_fds[i].revents |= POLLOUT;
                ready_count++;
            }
        }
    }
    
    (void)timeout; // Timeout is ignored in this simple implementation
    
    return ready_count;
}

// fd_set structure for select
typedef struct {
    uint32 fds_bits[32]; // 1024 file descriptors (32 * 32 bits)
} fd_set_t;

static int32 sys_select(sys_arg_t n, sys_arg_t inp, sys_arg_t outp, sys_arg_t exp, sys_arg_t tvp) {
    if (n == 0) {
        return 0;
    }
    
    // Validate and read fd_sets if provided
    fd_set_t read_fds, write_fds, except_fds;
    
    if (inp) {
        if (!user_buffer_readable((const void*)inp, sizeof(fd_set_t)) ||
            !user_buffer_writable((void*)inp, sizeof(fd_set_t))) {
            return SYSCALL_EFAULT;
        }
        memory_copy((char*)inp, (char*)&read_fds, sizeof(fd_set_t));
    } else {
        memory_set((uint8*)&read_fds, 0, sizeof(fd_set_t));
    }
    
    if (outp) {
        if (!user_buffer_readable((const void*)outp, sizeof(fd_set_t)) ||
            !user_buffer_writable((void*)outp, sizeof(fd_set_t))) {
            return SYSCALL_EFAULT;
        }
        memory_copy((char*)outp, (char*)&write_fds, sizeof(fd_set_t));
    } else {
        memory_set((uint8*)&write_fds, 0, sizeof(fd_set_t));
    }
    
    if (exp) {
        if (!user_buffer_readable((const void*)exp, sizeof(fd_set_t)) ||
            !user_buffer_writable((void*)exp, sizeof(fd_set_t))) {
            return SYSCALL_EFAULT;
        }
        memory_copy((char*)exp, (char*)&except_fds, sizeof(fd_set_t));
    } else {
        memory_set((uint8*)&except_fds, 0, sizeof(fd_set_t));
    }
    
    // Clear output fd_sets
    if (inp) memory_set((uint8*)inp, 0, sizeof(fd_set_t));
    if (outp) memory_set((uint8*)outp, 0, sizeof(fd_set_t));
    if (exp) memory_set((uint8*)exp, 0, sizeof(fd_set_t));
    
    int32 ready_count = 0;
    
    // Check file descriptors up to n
    for (int32 fd = 0; fd < n && fd < 1024; fd++) {
        int32 fd_bit_index = fd / 32;
        int32 fd_bit_offset = fd % 32;
        uint32 fd_mask = 1U << fd_bit_offset;
        
        bool was_readable = (read_fds.fds_bits[fd_bit_index] & fd_mask) != 0;
        bool was_writable = (write_fds.fds_bits[fd_bit_index] & fd_mask) != 0;
        bool had_exception = (except_fds.fds_bits[fd_bit_index] & fd_mask) != 0;
        
        if (was_readable) {
            bool readable = false;
            if (fd == 0) {
                readable = true;
            } else {
                bool pipe_is_write_end = false;
                pipe_t* pipe = find_pipe_by_fd(fd, &pipe_is_write_end);
                if (pipe && !pipe_is_write_end && pipe_available_bytes(pipe) > 0) {
                    readable = true;
                }

                bool unix_is_a_side = false;
                unix_socketpair_t* pair = find_unix_socketpair_by_fd(fd, &unix_is_a_side);
                if (pair) {
                    uint32 avail = unix_is_a_side
                        ? ring_available(pair->b_to_a_read_pos, pair->b_to_a_write_pos, UNIX_SOCKET_BUFFER_SIZE)
                        : ring_available(pair->a_to_b_read_pos, pair->a_to_b_write_pos, UNIX_SOCKET_BUFFER_SIZE);
                    if (avail > 0) {
                        readable = true;
                    }
                }

                bool pty_is_master = false;
                pty_t* pty = find_pty_by_fd(fd, &pty_is_master);
                if (pty) {
                    uint32 avail = pty_is_master
                        ? ring_available(pty->s2m_read_pos, pty->s2m_write_pos, PTY_BUFFER_SIZE)
                        : ring_available(pty->m2s_read_pos, pty->m2s_write_pos, PTY_BUFFER_SIZE);
                    if (avail > 0) {
                        readable = true;
                    }
                }
            }
            if (readable) {
                if (inp) ((fd_set_t*)inp)->fds_bits[fd_bit_index] |= fd_mask;
                ready_count++;
            }
        }
        
        if (was_writable) {
            bool writable = false;
            if (fd == 1 || fd == 2) {
                writable = true;
            } else {
                bool pipe_is_write_end = false;
                pipe_t* pipe = find_pipe_by_fd(fd, &pipe_is_write_end);
                if (pipe && pipe_is_write_end && pipe_free_bytes(pipe) > 0) {
                    writable = true;
                }

                bool unix_is_a_side = false;
                unix_socketpair_t* pair = find_unix_socketpair_by_fd(fd, &unix_is_a_side);
                if (pair) {
                    uint32 free_space = unix_is_a_side
                        ? ring_free(pair->a_to_b_read_pos, pair->a_to_b_write_pos, UNIX_SOCKET_BUFFER_SIZE)
                        : ring_free(pair->b_to_a_read_pos, pair->b_to_a_write_pos, UNIX_SOCKET_BUFFER_SIZE);
                    if (free_space > 0) {
                        writable = true;
                    }
                }

                bool pty_is_master = false;
                pty_t* pty = find_pty_by_fd(fd, &pty_is_master);
                if (pty) {
                    uint32 free_space = pty_is_master
                        ? ring_free(pty->m2s_read_pos, pty->m2s_write_pos, PTY_BUFFER_SIZE)
                        : ring_free(pty->s2m_read_pos, pty->s2m_write_pos, PTY_BUFFER_SIZE);
                    if (free_space > 0) {
                        writable = true;
                    }
                }
            }
            if (writable) {
                if (outp) ((fd_set_t*)outp)->fds_bits[fd_bit_index] |= fd_mask;
                ready_count++;
            }
        }
        
        // No exceptions in our simple implementation
        if (had_exception) {
            if (exp) ((fd_set_t*)exp)->fds_bits[fd_bit_index] |= fd_mask;
            ready_count++;
        }
    }
    
    (void)tvp; // Timeout is ignored in this simple implementation
    
    return ready_count;
}

static int32 sys_access(sys_arg_t path_ptr, sys_arg_t mode) {
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }
    
    (void)mode;
    char path_buf[128];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }

    if (!vfs_read_file(path_buf, 0, 0)) {
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
    enum {
        IOCTL_TCGETS = 0x5401,
        IOCTL_TCSETS = 0x5402,
        IOCTL_TIOCGWINSZ = 0x5413,
        IOCTL_TIOCSWINSZ = 0x5414,
        IOCTL_TIOCGPTN = 0x80045430,
        IOCTL_TIOCSPTLCK = 0x40045431,
    };
    typedef struct {
        uint16 ws_row;
        uint16 ws_col;
        uint16 ws_xpixel;
        uint16 ws_ypixel;
    } winsize_t;

     bool pty_is_master = false;
     pty_t* pty = find_pty_by_fd((int)fd, &pty_is_master);
     bool is_tty_like = (fd <= 2) || (pty != NULL);

     if (is_tty_like) {
         switch (request) {
             case IOCTL_TCGETS: // TIOCGETA is the same as IOCTL_TCGETS
                 if (!arg || !user_buffer_writable((void*)arg, sizeof(termios_t))) {
                     return SYSCALL_EFAULT;
                 }
                 if (pty) {
                     memory_copy((char*)pty->termios_blob, (char*)arg, sizeof(termios_t));
                 } else {
                     // For standard FDs (console), get termios from current TTY
                     uint8_t tty_num = tty_get_current_vt() - 1; // VT numbers are 1-12, TTY numbers are 0-11
                     virtual_tty_t* tty = &g_virtual_ttys[tty_num];
                     memory_copy((const char*)&tty->termios, (char*)arg, sizeof(termios_t));
                 }
                 return 0;

             case IOCTL_TCSETS: // TIOCSETA, TIOCSETAW, TIOCSETAF are the same as IOCTL_TCSETS
                 if (!arg || !user_buffer_readable((const void*)arg, sizeof(termios_t))) {
                     return SYSCALL_EFAULT;
                 }
                 if (pty) {
                     memory_copy((const char*)arg, (char*)pty->termios_blob, sizeof(termios_t));
                 } else {
                     // For standard FDs (console), set termios on current TTY
                     uint8_t tty_num = tty_get_current_vt() - 1; // VT numbers are 1-12, TTY numbers are 0-11
                     virtual_tty_t* tty = &g_virtual_ttys[tty_num];
                     memory_copy((const char*)arg, (char*)&tty->termios, sizeof(termios_t));
                 }
                 return 0;

             case IOCTL_TIOCGWINSZ: // TIOCGWINSZ is the same as IOCTL_TIOCGWINSZ
                if (!arg || !user_buffer_writable((void*)arg, sizeof(winsize_t))) {
                    return SYSCALL_EFAULT;
                }
                {
                    winsize_t* ws = (winsize_t*)arg;
                    ws->ws_row = pty ? pty->rows : 25;
                    ws->ws_col = pty ? pty->cols : 80;
                    ws->ws_xpixel = 0;
                    ws->ws_ypixel = 0;
                }
                return 0;

             case IOCTL_TIOCSWINSZ: // TIOCSWINSZ is the same as IOCTL_TIOCSWINSZ
                if (!arg || !user_buffer_readable((const void*)arg, sizeof(winsize_t))) {
                    return SYSCALL_EFAULT;
                }
                if (pty) {
                    const winsize_t* ws = (const winsize_t*)arg;
                    pty->rows = ws->ws_row ? ws->ws_row : 25;
                    pty->cols = ws->ws_col ? ws->ws_col : 80;
                }
                return 0;

            case IOCTL_TIOCGPTN:
                if (!pty || !pty_is_master) {
                    return SYSCALL_ENOTTY;
                }
                if (!arg || !user_buffer_writable((void*)arg, sizeof(int32))) {
                    return SYSCALL_EFAULT;
                }
                *(int32*)arg = pty->index;
                return 0;

            case IOCTL_TIOCSPTLCK:
                if (!pty || !pty_is_master) {
                    return SYSCALL_ENOTTY;
                }
                if (!arg || !user_buffer_readable((const void*)arg, sizeof(int32))) {
                    return SYSCALL_EFAULT;
                }
                pty->slave_locked = (*(int32*)arg != 0);
                return 0;

            default:
                break;
        }
    }

    uint32 slot = fd - 3;
    if (slot >= MAX_VFS_HANDLES || !vfs_handles[slot].used) {
        return SYSCALL_EBADF;
    }
    if (!vfs_device_handles[slot].used || !vfs_device_handles[slot].node) {
        return SYSCALL_ENOTTY;
    }

    int rc = vfs_ioctl(vfs_device_handles[slot].node, (uint32)request, (void*)arg);
    if (rc >= 0) {
        return rc;
    }

    switch (rc) {
        case IOCTL_ENOTTY: return SYSCALL_ENOTTY;
        case IOCTL_EINVAL: return SYSCALL_EINVAL;
        case IOCTL_EFAULT: return SYSCALL_EFAULT;
        case IOCTL_ENODEV: return SYSCALL_ENODEV;
        case IOCTL_ENOSYS: return SYSCALL_ENOSYS;
        default:           return SYSCALL_EINVAL;
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

int32 sys_power(int32 request) {
    if (request == POWER_ACTION_REBOOT) {
        power_reboot();
    } else {
        power_shutdown();
    }

    // Should never return
    return SYSCALL_EPERM;
}

int32 sys_user(sys_arg_t op, sys_arg_t arg2, sys_arg_t arg3, sys_arg_t arg4, sys_arg_t arg5, sys_arg_t arg6) {
    const char* user = (const char*)arg2;
    const char* pass = (const char*)arg3;
    const char* aux  = (const char*)arg4;
    auth_user_info_t* out_info = (auth_user_info_t*)arg5;
    uint32 extra = arg6;
    auth_result_t res = AUTH_ERR_INVALID;

    switch (op) {
            case USER_OP_LOGIN: {
            if (!user_buffer_readable(user, 1) || !user_buffer_readable(pass, 1)) {
                return SYSCALL_EFAULT;
            }
            auth_user_info_t info = {0};
            res = auth_login(user, pass, &info);
            debuglog(DEBUG_INFO, "[SYSCALL] USER_OP_LOGIN result: %d\n", res);
            if (res == AUTH_OK && current_task) {
                current_task->uid = info.uid;
                current_task->gid = info.gid;
                current_task->groups_mask = info.groups_mask;
            }
            if (out_info) {
                debuglog(DEBUG_INFO, "[SYSCALL] out_info is not NULL, checking writability\n");
                if (!user_buffer_writable(out_info, sizeof(auth_user_info_t))) {
                    debuglog(DEBUG_ERROR, "[SYSCALL] out_info not writable, returning EFAULT\n");
                    return SYSCALL_EFAULT;
                }
                memory_copy((const char*)&info, (char*)out_info, sizeof(auth_user_info_t));
            }
            break;
        }
            case USER_OP_SIGNUP:
            if (!user_buffer_readable(user, 1) || !user_buffer_readable(pass, 1)) {
                return SYSCALL_EFAULT;
            }
            res = auth_signup(user, pass, aux, false);
            break;
            case USER_OP_PASSWD:
            if (!user_buffer_readable(user, 1) || !user_buffer_readable(pass, 1)) {
                return SYSCALL_EFAULT;
            }
            res = auth_change_password(user, pass);
            break;
            case USER_OP_GROUP:
            if (!user_buffer_readable(aux, 1)) {
                return SYSCALL_EFAULT;
            }
            res = auth_add_group(aux, 0);
            break;
            case USER_OP_CURRENT:
            if (out_info && !user_buffer_writable(out_info, sizeof(auth_user_info_t))) {
                return SYSCALL_EFAULT;
            }
            res = auth_get_current(out_info);
            break;
            case USER_OP_LIST:
            if (out_info) {
                size_t total_bytes = (size_t)extra * sizeof(auth_user_info_t);
                if (extra > 0 && total_bytes / sizeof(auth_user_info_t) != (size_t)extra) {
                    return SYSCALL_ERANGE;
                }
                if (!user_buffer_writable(out_info, total_bytes)) {
                    return SYSCALL_EFAULT;
                }
            }
            if (aux && !user_buffer_writable((void*)aux, sizeof(uint32))) {
                return SYSCALL_EFAULT;
            }
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
    char path_buf[128];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }
    if (!vfs_read_file(path_buf, 0, 0)) {
        return SYSCALL_ENOENT;
    }
    // Initrd is read-only, so unlink is not supported yet.
    return SYSCALL_EPERM;
}

static int32 sys_mknod(sys_arg_t path_ptr, sys_arg_t mode, sys_arg_t dev) {
    if (!path_ptr) {
        return SYSCALL_EFAULT;
    }

    char path_buf[128];
    if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)path_ptr)) {
        return SYSCALL_EFAULT;
    }

    // For now, only allow device nodes in /dev
    if (strstr(path_buf, "/dev/") != path_buf) {
        return SYSCALL_EPERM;  // Only allow device nodes in /dev
    }

    // Extract device type and numbers from mode
    uint8_t type = 0;
    if ((mode & S_IFMT) == S_IFCHR) {
        type = DT_CHR;
    } else if ((mode & S_IFMT) == S_IFBLK) {
        type = DT_BLK;
    } else {
        return SYSCALL_EINVAL;  // Must be character or block device
    }

    uint16_t major = (dev >> 8) & 0xFF;
    uint16_t minor = dev & 0xFF;

    // Use the device filesystem API to create the node
    if (device_create_node(path_buf, major, minor, type, (uint16_t)mode) != 0) {
        return SYSCALL_EPERM;  // Could not create device node
    }

    return 0;
}

static inline uint32 pipe_available_bytes(pipe_t* pipe) {
    if (pipe->write_pos >= pipe->read_pos) {
        return pipe->write_pos - pipe->read_pos;
    } else {
        return PIPE_BUFFER_SIZE - pipe->read_pos + pipe->write_pos;
    }
}

static inline uint32 pipe_free_bytes(pipe_t* pipe) {
    return PIPE_BUFFER_SIZE - pipe_available_bytes(pipe) - 1;
}

static pipe_t* find_pipe_by_fd(int fd, bool* is_write_end) {
    for (uint32 i = 0; i < MAX_PIPES; i++) {
        pipe_t* pipe = &pipes[i];
        if (!pipe->used) {
            continue;
        }
        if (pipe->read_fd == fd) {
            if (is_write_end) {
                *is_write_end = false;
            }
            return pipe;
        }
        if (pipe->write_fd == fd) {
            if (is_write_end) {
                *is_write_end = true;
            }
            return pipe;
        }
    }
    return NULL;
}

static inline uint32 ring_available(uint32 read_pos, uint32 write_pos, uint32 capacity) {
    if (write_pos >= read_pos) {
        return write_pos - read_pos;
    }
    return capacity - read_pos + write_pos;
}

static inline uint32 ring_free(uint32 read_pos, uint32 write_pos, uint32 capacity) {
    return capacity - ring_available(read_pos, write_pos, capacity) - 1;
}

static unix_socketpair_t* find_unix_socketpair_by_fd(int fd, bool* is_a_side) {
    for (uint32 i = 0; i < MAX_UNIX_SOCKETPAIRS; i++) {
        unix_socketpair_t* pair = &unix_socketpairs[i];
        if (!pair->used) {
            continue;
        }
        if (pair->fd_a == fd) {
            if (is_a_side) {
                *is_a_side = true;
            }
            return pair;
        }
        if (pair->fd_b == fd) {
            if (is_a_side) {
                *is_a_side = false;
            }
            return pair;
        }
    }
    return NULL;
}

static pty_t* find_pty_by_fd(int fd, bool* is_master) {
    for (uint32 i = 0; i < MAX_PTYS; i++) {
        pty_t* pty = &ptys[i];
        if (!pty->used) {
            continue;
        }
        if (pty->master_fd == fd) {
            if (is_master) {
                *is_master = true;
            }
            return pty;
        }
        if (pty->slave_fd == fd) {
            if (is_master) {
                *is_master = false;
            }
            return pty;
        }
    }
    return NULL;
}

static pty_t* find_pty_by_index(int index) {
    if (index < 0 || index >= MAX_PTYS) {
        return NULL;
    }
    if (!ptys[index].used) {
        return NULL;
    }
    return &ptys[index];
}

static unix_path_socket_t* find_unix_path_socket_by_fd(int fd) {
    for (uint32 i = 0; i < MAX_UNIX_PATH_SOCKETS; i++) {
        if (unix_path_sockets[i].used && unix_path_sockets[i].fd == fd) {
            return &unix_path_sockets[i];
        }
    }
    return NULL;
}

static unix_path_socket_t* find_unix_path_listener_by_path(const char* path) {
    if (!path || !*path) {
        return NULL;
    }
    for (uint32 i = 0; i < MAX_UNIX_PATH_SOCKETS; i++) {
        unix_path_socket_t* sock = &unix_path_sockets[i];
        if (!sock->used || !sock->bound || !sock->listening) {
            continue;
        }
        if (strcmp(sock->path, path) == 0) {
            return sock;
        }
    }
    return NULL;
}

static int alloc_local_fd_slot(void) {
    for (uint32 i = 0; i < MAX_VFS_HANDLES; i++) {
        if (!vfs_handles[i].used && !vfs_device_handles[i].used) {
            vfs_handles[i].used = true;
            vfs_handles[i].data = NULL;
            vfs_handles[i].size = 0;
            vfs_handles[i].offset = 0;
            vfs_device_handles[i].used = false;
            vfs_device_handles[i].node = NULL;
            return (int)(i + 3);
        }
    }
    return -1;
}

static void free_local_fd_slot(int fd) {
    if (fd < 3) {
        return;
    }
    uint32 slot = (uint32)(fd - 3);
    if (slot >= MAX_VFS_HANDLES) {
        return;
    }
    vfs_handles[slot].used = false;
    vfs_handles[slot].data = NULL;
    vfs_handles[slot].size = 0;
    vfs_handles[slot].offset = 0;
    vfs_device_handles[slot].used = false;
    vfs_device_handles[slot].node = NULL;
}

static int alloc_unix_path_connection(void) {
    for (uint32 i = 0; i < MAX_UNIX_PATH_CONNECTIONS; i++) {
        if (!unix_path_connections[i].used) {
            memory_set((uint8*)&unix_path_connections[i], 0, sizeof(unix_path_connection_t));
            unix_path_connections[i].used = true;
            unix_path_connections[i].fd_a = -1;
            unix_path_connections[i].fd_b = -1;
            return (int)i;
        }
    }
    return -1;
}

static int unix_path_enqueue_pending(unix_path_socket_t* sock, int conn_id) {
    if (!sock || sock->pending_count >= MAX_UNIX_PATH_PENDING) {
        return -1;
    }
    sock->pending_conn_ids[sock->pending_tail] = conn_id;
    sock->pending_tail = (uint8)((sock->pending_tail + 1) % MAX_UNIX_PATH_PENDING);
    sock->pending_count++;
    return 0;
}

static int unix_path_dequeue_pending(unix_path_socket_t* sock) {
    if (!sock || sock->pending_count == 0) {
        return -1;
    }
    int conn_id = sock->pending_conn_ids[sock->pending_head];
    sock->pending_head = (uint8)((sock->pending_head + 1) % MAX_UNIX_PATH_PENDING);
    sock->pending_count--;
    return conn_id;
}

static int32 unix_path_send(int fd, const uint8* data, uint32 len) {
    if (!data || len == 0) {
        return 0;
    }
    unix_path_socket_t* sock = find_unix_path_socket_by_fd(fd);
    if (!sock || !sock->connected || sock->connection_id < 0 || sock->connection_id >= MAX_UNIX_PATH_CONNECTIONS) {
        return SYSCALL_EBADF;
    }
    unix_path_connection_t* conn = &unix_path_connections[sock->connection_id];
    if (!conn->used) {
        return SYSCALL_EBADF;
    }

    bool is_a = (conn->fd_a == fd);
    if (!is_a && conn->fd_b != fd) {
        return SYSCALL_EBADF;
    }

    bool peer_closed = is_a ? conn->b_closed : conn->a_closed;
    if (peer_closed) {
        return SYSCALL_EPIPE;
    }

    uint8* out_buf = is_a ? conn->a_to_b : conn->b_to_a;
    uint32* out_r = is_a ? &conn->a2b_read_pos : &conn->b2a_read_pos;
    uint32* out_w = is_a ? &conn->a2b_write_pos : &conn->b2a_write_pos;
    uint32 free_space = ring_free(*out_r, *out_w, UNIX_PATH_SOCKET_BUFFER_SIZE);
    if (free_space == 0) {
        return 0;
    }
    uint32 to_write = (len < free_space) ? len : free_space;
    for (uint32 i = 0; i < to_write; i++) {
        out_buf[*out_w] = data[i];
        *out_w = (*out_w + 1) % UNIX_PATH_SOCKET_BUFFER_SIZE;
    }
    return (int32)to_write;
}

static int32 unix_path_recv(int fd, uint8* data, uint32 len) {
    if (!data || len == 0) {
        return 0;
    }
    unix_path_socket_t* sock = find_unix_path_socket_by_fd(fd);
    if (!sock || !sock->connected || sock->connection_id < 0 || sock->connection_id >= MAX_UNIX_PATH_CONNECTIONS) {
        return SYSCALL_EBADF;
    }
    unix_path_connection_t* conn = &unix_path_connections[sock->connection_id];
    if (!conn->used) {
        return SYSCALL_EBADF;
    }

    bool is_a = (conn->fd_a == fd);
    if (!is_a && conn->fd_b != fd) {
        return SYSCALL_EBADF;
    }

    uint8* in_buf = is_a ? conn->b_to_a : conn->a_to_b;
    uint32* in_r = is_a ? &conn->b2a_read_pos : &conn->a2b_read_pos;
    uint32* in_w = is_a ? &conn->b2a_write_pos : &conn->a2b_write_pos;
    uint32 available = ring_available(*in_r, *in_w, UNIX_PATH_SOCKET_BUFFER_SIZE);
    if (available == 0) {
        return 0;
    }
    uint32 to_read = (len < available) ? len : available;
    for (uint32 i = 0; i < to_read; i++) {
        data[i] = in_buf[*in_r];
        *in_r = (*in_r + 1) % UNIX_PATH_SOCKET_BUFFER_SIZE;
    }
    return (int32)to_read;
}

static shm_segment_t* find_shm_segment_by_id(int32 shmid) {
    for (uint32 i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].used && shm_segments[i].shmid == shmid) {
            return &shm_segments[i];
        }
    }
    return NULL;
}

static shm_segment_t* find_shm_segment_by_key(int32 key) {
    for (uint32 i = 0; i < MAX_SHM_SEGMENTS; i++) {
        if (shm_segments[i].used && shm_segments[i].key == key) {
            return &shm_segments[i];
        }
    }
    return NULL;
}

static shm_attachment_t* find_shm_attachment(uint32 task_id, uint32 addr) {
    for (uint32 i = 0; i < MAX_SHM_ATTACHMENTS; i++) {
        if (shm_attachments[i].used &&
            shm_attachments[i].task_id == task_id &&
            shm_attachments[i].addr == addr) {
            return &shm_attachments[i];
        }
    }
    return NULL;
}

static int32 sys_pipe(sys_arg_t pipefd_ptr) {
    if (!pipefd_ptr) {
        return SYSCALL_EFAULT;
    }

    if (!user_buffer_writable((void*)pipefd_ptr, sizeof(int) * 2)) {
        return SYSCALL_EFAULT;
    }

    // Find free pipe slot
    int pipe_idx = -1;
    for (uint32 i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].used) {
            pipe_idx = i;
            break;
        }
    }

    if (pipe_idx == -1) {
        return SYSCALL_ENFILE;
    }

    // Find two free VFS handle slots
    int read_fd = -1, write_fd = -1;
    for (uint32 i = 0; i < MAX_VFS_HANDLES; i++) {
        if (!vfs_handles[i].used) {
            if (read_fd == -1) {
                read_fd = i;
            } else {
                write_fd = i;
                break;
            }
        }
    }

    if (read_fd == -1 || write_fd == -1) {
        return SYSCALL_EMFILE;
    }

    // Initialize pipe
    pipe_t* pipe = &pipes[pipe_idx];
    pipe->used = true;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->read_fd = read_fd + 3;
    pipe->write_fd = write_fd + 3;
    pipe->read_closed = false;
    pipe->write_closed = false;
    memory_set(pipe->buffer, 0, PIPE_BUFFER_SIZE);

    // Set up read handle (special marker: fd >= 3 and data == NULL means pipe)
    vfs_handles[read_fd].used = true;
    vfs_handles[read_fd].data = NULL;
    vfs_handles[read_fd].size = (uint32)pipe_idx;  // Store pipe index in size field
    vfs_handles[read_fd].offset = 0;
    vfs_device_handles[read_fd].used = false;
    vfs_device_handles[read_fd].node = NULL;

    // Set up write handle
    vfs_handles[write_fd].used = true;
    vfs_handles[write_fd].data = (const uint8*)1;  // Marker: 1 = write end
    vfs_handles[write_fd].size = (uint32)pipe_idx;
    vfs_handles[write_fd].offset = 0;
    vfs_device_handles[write_fd].used = false;
    vfs_device_handles[write_fd].node = NULL;

    // Return file descriptors to userspace
    int* user_fds = (int*)pipefd_ptr;
    user_fds[0] = pipe->read_fd;
    user_fds[1] = pipe->write_fd;

    return 0;
}

static int32 sys_pipe2(sys_arg_t pipefd_ptr, sys_arg_t flags) {
    if (!pipefd_ptr) {
        return SYSCALL_EFAULT;
    }

    if (!user_buffer_writable((void*)pipefd_ptr, sizeof(int) * 2)) {
        return SYSCALL_EFAULT;
    }

    int32 result = sys_pipe(pipefd_ptr);
    if (result != 0) {
        return result;
    }

    return 0;
}

static int32 sys_seteuid(sys_arg_t uid) {
    if (current_task) {
        current_task->uid = uid;
        return 0;
    }
    return SYSCALL_EPERM;
}

static int32 sys_setegid(sys_arg_t gid) {
    if (current_task) {
        current_task->gid = gid;
        return 0;
    }
    return SYSCALL_EPERM;
}

// ============================================================================
// Input Event Syscalls - Direct event reading for userspace graphics
// ============================================================================

/**
 * Read a keyboard event directly (non-blocking)
 * Returns: number of bytes read (sizeof(input_event_t)) or 0 if no events
 */
static long sys_read_kbd_event(void* user_event) {
    if (!user_event) {
        return -SYSCALL_EFAULT;
    }

    input_ring_t* kbd_ring = devfs_get_kbd_ring();
    if (!kbd_ring) {
        return 0;  // No events available, devices not initialized
    }

    input_event_t event;
    if (!input_ring_pop(kbd_ring, &event)) {
        return 0;  // No events in queue
    }

    // Copy event to userspace
    USER_ACCESS_BEGIN();
    memory_copy((const char*)&event, (char*)user_event, sizeof(input_event_t));
    USER_ACCESS_END();

    return sizeof(input_event_t);
}

/**
 * Read a mouse event directly (non-blocking)
 * Returns: number of bytes read (sizeof(input_event_t)) or 0 if no events
 */
static long sys_read_mouse_event(void* user_event) {
    if (!user_event) {
        return -SYSCALL_EFAULT;
    }

    input_ring_t* mouse_ring = devfs_get_mouse_ring();
    if (!mouse_ring) {
        return 0;  // No events available, devices not initialized
    }

    input_event_t event;
    if (!input_ring_pop(mouse_ring, &event)) {
        return 0;  // No events in queue
    }

    // Copy event to userspace
    USER_ACCESS_BEGIN();
    memory_copy((const char*)&event, (char*)user_event, sizeof(input_event_t));
    USER_ACCESS_END();

    return sizeof(input_event_t);
}

/**
 * Poll for input events (non-blocking)
 * Returns: bitmask of available input (bit 0 = kbd, bit 1 = mouse)
 */
static long sys_poll_input(void) {
    long result = 0;

    input_ring_t* kbd_ring = devfs_get_kbd_ring();
    if (kbd_ring && !input_ring_is_empty(kbd_ring)) {
        result |= 1;  // Keyboard events available
    }

    input_ring_t* mouse_ring = devfs_get_mouse_ring();
    if (mouse_ring && !input_ring_is_empty(mouse_ring)) {
        result |= 2;  // Mouse events available
    }

    return result;
}

void syscall_init(void) {
#if ARCH_64BIT
    set_idt_gate_flags(SYSCALL_VECTOR, (uintptr_t)syscall_handler, IDT_GATE_USER_INT);
   //
    //print("[SYSCALL] Installed 64-bit software interrupt handler at 0x80\n");
#else
    set_idt_gate_flags(SYSCALL_VECTOR, (uintptr_t)isr128, IDT_GATE_USER_INT);
    //print("[SYSCALL] Installed software interrupt handler at 0x80\n");
#endif
}

void syscall_handle(syscall_frame_t* frame) {
    if (!frame) {
        return;
    }

    // Check if task has already been marked as terminated by a previous syscall
    // (e.g., sys_exit was called). If so, do NOT return to userspace.
    if (current_task && current_task->state == TASK_STATE_TERMINATED) {
       // print("[SYSCALL] Task terminated, not returning to userspace\n");
        // Enter infinite loop - task_schedule() will switch to another task
        // or this task will be cleaned up by the scheduler
        while (1) {
            __asm__ __volatile__("hlt");
        }
    }

    // DEBUG: Print syscall entry
   /* print("[SYSCALL] Entry, frame=0x"); print_hex((uint32)(uintptr_t)frame);
#if ARCH_64BIT
    print(", rax="); print_hex((uint32)frame->rax);
#else
    print(", eax="); print_hex(frame->eax);
#endif
    print("\n");*/

    // Note syscall as activity for watchdogs/debugging.
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
    
    // Use optimized function pointer table dispatch for common syscalls
    int32 result = -38; // -ENOSYS default
    
    // Debug logging for high-numbered syscalls
    if (num == 471) {
      //  debuglog(DEBUG_INFO, "[SYSCALL] SYS_MMAP_FB (471) called\n");
    }
    
    // Main syscall dispatch table.
    if (num < SYS_MAX) {
        switch (num) {
            case SYS_WRITE:      result = sys_write(arg1, arg2, arg3); break;
            case SYS_READ:       result = sys_read(arg1, arg2, arg3); break;
            case SYS_OPEN:       result = sys_open(arg1, arg2, arg3); break;
            case SYS_CLOSE:      result = sys_close(arg1); break;
            case SYS_POLL:       result = sys_poll(arg1, arg2, arg3); break;
            case SYS_LSEEK:      result = sys_lseek(arg1, arg2, arg3); break;
            case SYS_GETPID:     result = sys_getpid(); break;
            case SYS_TIME:       result = sys_time(arg1); break;
            case SYS_BRK:        result = sys_brk(arg1); break;
            case SYS_NANOSLEEP:  result = sys_nanosleep(arg1, arg2); break;
            case SYS_UNAME:      result = sys_uname(arg1); break;
            case SYS_EXIT:       result = sys_exit(arg1); break;
            case SYS_EXIT_GROUP: result = sys_exit(arg1); break;
            case SYS_UNLINK:     result = sys_unlink(arg1); break;
            case SYS_MKNOD:      result = sys_mknod(arg1, arg2, arg3); break;
            case SYS_PIPE:       result = sys_pipe(arg1); break;
            case SYS_SOCKET:     result = sys_socket(arg1, arg2, arg3); break;
            case SYS_BIND:       result = sys_bind(arg1, arg2, arg3); break;
            case SYS_SENDTO:     result = sys_sendto(arg1, arg2, arg3, arg4, arg5, arg6); break;
            case SYS_RECVFROM:   result = sys_recvfrom(arg1, arg2, arg3, arg4, arg5, arg6); break;
            case SYS_NETINFO:    result = sys_netinfo(arg1, arg2); break;
            case SYS_REBOOT:     result = sys_power(POWER_ACTION_REBOOT); break;
            case SYS_POWER:      result = sys_power(arg1); break;
            
            // Additional common syscalls (continue from original switch)
            case SYS_GETUID:     result = sys_getuid(); break;
            case SYS_GETGID:     result = sys_getgid(); break;
            case SYS_GETEUID:    result = sys_geteuid(); break;
            case SYS_GETEGID:    result = sys_getegid(); break;
            case SYS_GETPPID:    result = sys_getppid(); break;
            case SYS_DUP:        result = sys_dup(arg1); break;
            case SYS_DUP2:       result = sys_dup2(arg1, arg2); break;
            case SYS_IOCTL:      result = sys_ioctl(arg1, arg2, arg3); break;
            case SYS_FCNTL:      result = sys_fcntl(arg1, arg2, arg3); break;
            case SYS_ACCESS:     result = sys_access(arg1, arg2); break;
            case SYS_PIPE2:      result = sys_pipe2(arg1, arg2); break;
            case SYS_SELECT:     result = sys_select(arg1, arg2, arg3, arg4, arg5); break;
            case SYS_GETCWD:     result = sys_getcwd(arg1, arg2); break;
            case SYS_CHDIR:      result = sys_chdir(arg1); break;
            case SYS_FCHDIR:     result = sys_fchdir(arg1); break;
            case SYS_RENAME:     result = sys_rename(arg1, arg2); break;
            case SYS_MKDIR:      result = sys_mkdir(arg1, arg2); break;
            case SYS_RMDIR:      result = sys_rmdir(arg1); break;
            case SYS_CREAT:      result = sys_creat(arg1, arg2); break;
            case SYS_LINK:       result = sys_link(arg1, arg2); break;
            case SYS_SYMLINK:    result = sys_symlink(arg1, arg2); break;
            case SYS_READLINK:   result = sys_readlink(arg1, arg2, arg3); break;
            case SYS_CHMOD:      result = sys_chmod(arg1, arg2); break;
            case SYS_FCHMOD:     result = sys_fchmod(arg1, arg2); break;
            case SYS_CHOWN:      result = sys_chown(arg1, arg2, arg3); break;
            case SYS_FCHOWN:     result = sys_fchown(arg1, arg2, arg3); break;
            case SYS_LCHOWN:     result = sys_lchown(arg1, arg2, arg3); break;
            case SYS_UMASK:      result = sys_umask(arg1); break;
            case SYS_FSYNC:      result = sys_fsync(arg1); break;
            case SYS_FDATASYNC:  result = sys_fdatasync(arg1); break;
            case SYS_FTRUNCATE:  result = sys_ftruncate(arg1, arg2); break;
            case SYS_READV:      result = sys_readv(arg1, arg2, arg3); break;
            case SYS_WRITEV:     result = sys_writev(arg1, arg2, arg3); break;
            case SYS_PREAD64:    result = sys_pread64(arg1, arg2, arg3, arg4); break;
            case SYS_PWRITE64:   result = sys_pwrite64(arg1, arg2, arg3, arg4); break;
            case SYS_SETUID:     result = sys_setuid(arg1); break;
            case SYS_SETGID:     result = sys_setgid(arg1); break;
            case SYS_SETEUID:    result = sys_seteuid(arg1); break;
            case SYS_SETEGID:    result = sys_setegid(arg1); break;
            case SYS_SETREUID:   result = sys_setreuid(arg1, arg2); break;
            case SYS_SETREGID:   result = sys_setregid(arg1, arg2); break;
            case SYS_FORK:       result = sys_fork(); break;
            case SYS_EXECVE:     result = sys_execve(arg1, arg2, arg3); break;
            case SYS_WAITID:     result = sys_wait4(arg1, arg2, arg3, arg4); break;
            case SYS_KILL:       result = sys_kill(arg1, arg2); break;
            case SYS_SETTIMEOFDAY: result = sys_settimeofday(arg1, arg2); break;
            case SYS_SCHED_YIELD: result = sys_sched_yield(); break;
            case SYS_CONNECT:    result = sys_connect(arg1, arg2, arg3); break;
            case SYS_ACCEPT:     result = sys_accept(arg1, arg2, arg3); break;
            case SYS_SENDMSG:    result = sys_sendmsg(arg1, arg2, arg3); break;
            case SYS_RECVMSG:    result = sys_recvmsg(arg1, arg2, arg3); break;
            case SYS_SHUTDOWN:   result = sys_shutdown(arg1, arg2); break;
            case SYS_LISTEN:     result = sys_listen(arg1, arg2); break;
            case SYS_GETSOCKNAME: result = sys_getsockname(arg1, arg2, arg3); break;
            case SYS_GETPEERNAME: result = sys_getpeername(arg1, arg2, arg3); break;
            case SYS_SOCKETPAIR: result = sys_socketpair(arg1, arg2, arg3, arg4); break;
            case SYS_SETSOCKOPT: result = sys_setsockopt(arg1, arg2, arg3, arg4, arg5); break;
            case SYS_GETSOCKOPT: result = sys_getsockopt(arg1, arg2, arg3, arg4, arg5); break;
            
            // Forest OS specific syscalls continue here
            case SYS_GETDENTS:   result = sys_getdents(arg1, arg2, arg3); break;

            // Process management
            case SYS_VFORK:      result = sys_vfork(); break;
            case SYS_CLONE:      result = sys_clone(arg1, arg2, arg3, arg4, arg5); break;
            case SYS_WAIT4:      result = sys_wait4(arg1, arg2, arg3, arg4); break;
            case SYS_TKILL:      result = sys_tkill(arg1, arg2); break;
            case SYS_TGKILL:     result = sys_tgkill(arg1, arg2, arg3); break;
            case SYS_GETTID:     result = sys_gettid(); break;

            case SYS_SETSID:     result = sys_setsid(); break;
            case SYS_GETSID:     result = sys_getsid(arg1); break;
            case SYS_SETPGID:    result = sys_setpgid(arg1, arg2); break;
            case SYS_GETPGID:    result = sys_getpgid(arg1); break;
            case SYS_GETPGRP:    result = sys_getpgrp(); break;
             case 127:  result = sys_tcgetpgrp(arg1); break;  // SYS_TCGETPGRP
             case 128:  result = sys_tcsetpgrp(arg1, arg2); break;  // SYS_TCSETPGRP
        
            case SYS_SETRESUID:  result = sys_setresuid(arg1, arg2, arg3); break;
            case SYS_GETRESUID:  result = sys_getresuid(arg1, arg2, arg3); break;
            case SYS_SETRESGID:  result = sys_setresgid(arg1, arg2, arg3); break;
            case SYS_GETRESGID:  result = sys_getresgid(arg1, arg2, arg3); break;
            case SYS_SETFSUID:   result = sys_setfsuid(arg1); break;
            case SYS_SETFSGID:   result = sys_setfsgid(arg1); break;
            case SYS_GETGROUPS:  result = sys_getgroups(arg1, arg2); break;
            case SYS_SETGROUPS:  result = sys_setgroups(arg1, arg2); break;

            // Memory management
            case SYS_MMAP:       result = sys_mmap(arg1, arg2, arg3, arg4, arg5, arg6); break;
            case SYS_MUNMAP:     result = sys_munmap(arg1, arg2); break;
            case SYS_MPROTECT:   result = sys_mprotect(arg1, arg2, arg3); break;
            case SYS_MADVISE:    result = sys_madvise(arg1, arg2, arg3); break;
            case SYS_MSYNC:      result = sys_msync(arg1, arg2, arg3); break;
            case SYS_MLOCK:      result = sys_mlock(arg1, arg2); break;
            case SYS_MUNLOCK:    result = sys_munlock(arg1, arg2); break;
            case SYS_MLOCKALL:   result = sys_mlockall(arg1); break;
            case SYS_MUNLOCKALL: result = sys_munlockall(); break;
            case SYS_MREMAP:     result = sys_mremap(arg1, arg2, arg3, arg4, arg5); break;
            case SYS_MINCORE:    result = sys_mincore(arg1, arg2, arg3); break;

            // Signals
            case SYS_PAUSE:      result = sys_pause(); break;
            case SYS_RT_SIGACTION: result = sys_rt_sigaction(arg1, arg2, arg3, arg4); break;
            case SYS_RT_SIGPROCMASK: result = sys_rt_sigprocmask(arg1, arg2, arg3, arg4); break;
            case SYS_RT_SIGPENDING: result = sys_rt_sigpending(arg1, arg2); break;
            case SYS_RT_SIGTIMEDWAIT: result = sys_rt_sigtimedwait(arg1, arg2, arg3, arg4); break;
            case SYS_RT_SIGQUEUEINFO: result = sys_rt_sigqueueinfo(arg1, arg2, arg3); break;
            case SYS_RT_SIGSUSPEND: result = sys_rt_sigsuspend(arg1, arg2); break;
            case SYS_SIGALTSTACK: result = sys_sigaltstack(arg1, arg2); break;

            // System information
            case SYS_GETPRIORITY: result = sys_getpriority(arg1, arg2); break;
            case SYS_SETPRIORITY: result = sys_setpriority(arg1, arg2, arg3); break;
            case SYS_SETHOSTNAME: result = sys_sethostname(arg1, arg2); break;
            case SYS_SETDOMAINNAME: result = sys_setdomainname(arg1, arg2); break;
            case SYS_GETTIMEOFDAY: result = sys_gettimeofday(arg1, arg2); break;
            case SYS_CLOCK_GETTIME: result = sys_clock_gettime(arg1, arg2); break;

            // File system attributes
            case SYS_STATFS:      result = sys_statfs(arg1, arg2); break;
            case SYS_FSTATFS:     result = sys_fstatfs(arg1, arg2); break;
            case SYS_UTIME:       result = sys_utime(arg1, arg2); break;
            case SYS_UTIMES:      result = sys_utimes(arg1, arg2); break;
            case SYS_UTIMENSAT:   result = sys_utimensat(arg1, arg2, arg3, arg4); break;

            // Extended attributes
            case SYS_SETXATTR:    result = sys_setxattr(arg1, arg2, arg3, arg4, arg5); break;
            case SYS_GETXATTR:    result = sys_getxattr(arg1, arg2, arg3, arg4); break;
            case SYS_LGETXATTR:   result = sys_lgetxattr(arg1, arg2, arg3, arg4); break;
            case SYS_FGETXATTR:   result = sys_fgetxattr(arg1, arg2, arg3, arg4); break;
            case SYS_FLISTXATTR:  result = sys_flistxattr(arg1, arg2, arg3); break;
            case SYS_FREMOVEXATTR: result = sys_fremovexattr(arg1, arg2); break;

            // Synchronization
            case SYS_FUTEX:      result = sys_futex(arg1, arg2, arg3, arg4, arg5, arg6); break;

            // System calls that return ENOSYS for now (too complex/advanced)
            case SYS_PTRACE:      result = SYSCALL_ENOSYS; break;  // Debugging
            case SYS_PERSONALITY: result = SYSCALL_ENOSYS; break;  // Execution domains
            case SYS_SYSLOG:      result = SYSCALL_ENOSYS; break;  // System logging
            case SYS_VHANGUP:     result = SYSCALL_ENOSYS; break;  // Virtual terminal
            case SYS_MODIFY_LDT:  result = SYSCALL_ENOSYS; break;  // Local descriptor table
            case SYS_PIVOT_ROOT:  result = SYSCALL_ENOSYS; break;  // Root filesystem
            case SYS_PRCTL:       result = SYSCALL_ENOSYS; break;  // Process control
            case SYS_ARCH_PRCTL:  result = SYSCALL_ENOSYS; break;  // Architecture specific
            case SYS_ADJTIMEX:    result = SYSCALL_ENOSYS; break;  // Time adjustment
            case SYS_MOUNT:       result = SYSCALL_ENOSYS; break;  // Filesystem mounting
            case SYS_UMOUNT2:     result = SYSCALL_ENOSYS; break;  // Filesystem unmounting
            case SYS_SWAPON:      result = SYSCALL_ENOSYS; break;  // Swap
            case SYS_SWAPOFF:     result = SYSCALL_ENOSYS; break;  // Swap
            case SYS_INIT_MODULE: result = SYSCALL_ENOSYS; break;  // Kernel modules
            case SYS_DELETE_MODULE: result = SYSCALL_ENOSYS; break; // Kernel modules
            case SYS_IOPL:        result = SYSCALL_ENOSYS; break;  // I/O privilege
            case SYS_IOPERM:      result = SYSCALL_ENOSYS; break;  // I/O permissions
            case SYS_SEMGET:      result = SYSCALL_ENOSYS; break;  // System V semaphores
            case SYS_SEMOP:       result = SYSCALL_ENOSYS; break;  // System V semaphores
            case SYS_SEMCTL:      result = SYSCALL_ENOSYS; break;  // System V semaphores
            case SYS_SHMGET:      result = sys_shmget(arg1, arg2, arg3); break;
            case SYS_SHMAT:       result = sys_shmat(arg1, arg2, arg3); break;
            case SYS_SHMCTL:      result = sys_shmctl(arg1, arg2, arg3); break;
            case SYS_SHMDT:       result = sys_shmdt(arg1); break;
            case SYS_MSGGET:      result = SYSCALL_ENOSYS; break;  // System V message queues
            case SYS_MSGSND:      result = SYSCALL_ENOSYS; break;  // System V message queues
            case SYS_MSGRCV:      result = SYSCALL_ENOSYS; break;  // System V message queues
            case SYS_MSGCTL:      result = SYSCALL_ENOSYS; break;  // System V message queues

            case SYS_SEMTIMEDOP:  result = SYSCALL_ENOSYS; break;  // System V semaphores
            case SYS_FUTEX_WAITV: result = SYSCALL_ENOSYS; break;  // Advanced futex
            case SYS_KEXEC_LOAD:  result = SYSCALL_ENOSYS; break;  // Kexec
        
            case SYS_ADD_KEY:     result = SYSCALL_ENOSYS; break;  // Key management
            case SYS_REQUEST_KEY: result = SYSCALL_ENOSYS; break;  // Key management
            case SYS_KEYCTL:      result = SYSCALL_ENOSYS; break;  // Key management
            case SYS_IOPRIO_SET:  result = SYSCALL_ENOSYS; break;  // I/O priority
            case SYS_IOPRIO_GET:  result = SYSCALL_ENOSYS; break;  // I/O priority
            case SYS_INOTIFY_INIT: result = SYSCALL_ENOSYS; break; // Inotify
            case SYS_INOTIFY_ADD_WATCH: result = SYSCALL_ENOSYS; break; // Inotify
            case SYS_INOTIFY_RM_WATCH: result = SYSCALL_ENOSYS; break; // Inotify
            case SYS_MIGRATE_PAGES: result = SYSCALL_ENOSYS; break; // Memory migration
            case SYS_OPENAT:      result = sys_openat(arg1, arg2, arg3, arg4); break;
            case SYS_MKDIRAT:     result = sys_mkdirat(arg1, arg2, arg3); break;
            case SYS_MKNODAT:     result = SYSCALL_ENOSYS; break;  // Relative mknod
            case SYS_FCHOWNAT:    result = SYSCALL_ENOSYS; break;  // Relative chown
            case SYS_FUTIMESAT:   result = SYSCALL_ENOSYS; break;  // Relative utimes
            case SYS_NEWFSTATAT:  result = sys_newfstatat(arg1, arg2, arg3, arg4); break;
            case SYS_UNLINKAT:    result = sys_unlinkat(arg1, arg2, arg3); break;
            case SYS_RENAMEAT:    result = SYSCALL_ENOSYS; break;  // Relative rename
            case SYS_LINKAT:      result = SYSCALL_ENOSYS; break;  // Relative link
            case SYS_SYMLINKAT:   result = SYSCALL_ENOSYS; break;  // Relative symlink
            case SYS_READLINKAT:  result = SYSCALL_ENOSYS; break;  // Relative readlink
            case SYS_FCHMODAT:    result = SYSCALL_ENOSYS; break;  // Relative chmod
            case SYS_FACCESSAT:   result = sys_faccessat(arg1, arg2, arg3, arg4); break;
            case SYS_PSELECT6:    result = SYSCALL_ENOSYS; break;  // Advanced select
            case SYS_PPOLL:       result = SYSCALL_ENOSYS; break;  // Advanced poll
            case SYS_UNSHARE:     result = SYSCALL_ENOSYS; break;  // Namespace unshare
            case SYS_SET_ROBUST_LIST: result = SYSCALL_ENOSYS; break; // Robust futex
            case SYS_GET_ROBUST_LIST: result = SYSCALL_ENOSYS; break; // Robust futex
            case SYS_SPLICE:      result = SYSCALL_ENOSYS; break;  // Zero-copy I/O
            case SYS_TEE:         result = SYSCALL_ENOSYS; break;  // Zero-copy I/O
            case SYS_SYNC_FILE_RANGE: result = SYSCALL_ENOSYS; break; // Sync ranges
            case SYS_VMSPLICE:    result = SYSCALL_ENOSYS; break;  // Memory splice
            case SYS_MOVE_PAGES:  result = SYSCALL_ENOSYS; break;  // Memory migration
            case SYS_EPOLL_WAIT:  result = SYSCALL_ENOSYS; break;  // Epoll
            case SYS_EPOLL_CTL:   result = SYSCALL_ENOSYS; break;  // Epoll
            case SYS_SIGNALFD:    result = SYSCALL_ENOSYS; break;  // Signal file descriptor
            case SYS_TIMERFD_CREATE: result = SYSCALL_ENOSYS; break; // Timer FD
            case SYS_EVENTFD:     result = SYSCALL_ENOSYS; break;  // Event FD
            case SYS_FALLOCATE:   result = SYSCALL_ENOSYS; break;  // File allocation
            case SYS_TIMERFD_SETTIME: result = SYSCALL_ENOSYS; break; // Timer FD
            case SYS_TIMERFD_GETTIME: result = SYSCALL_ENOSYS; break; // Timer FD
            case SYS_ACCEPT4:     result = SYSCALL_ENOSYS; break;  // Advanced accept
            case SYS_SIGNALFD4:   result = SYSCALL_ENOSYS; break;  // Advanced signal FD
            case SYS_EVENTFD2:    result = SYSCALL_ENOSYS; break;  // Advanced event FD
            case SYS_EPOLL_CREATE1: result = SYSCALL_ENOSYS; break; // Epoll
            case SYS_DUP3:        result = SYSCALL_ENOSYS; break;  // Advanced dup
        
            case SYS_INOTIFY_INIT1: result = SYSCALL_ENOSYS; break; // Inotify
            case SYS_PREADV:      result = SYSCALL_ENOSYS; break;  // Vector I/O
            case SYS_PWRITEV:     result = SYSCALL_ENOSYS; break;  // Vector I/O
            case SYS_RT_TGSIGQUEUEINFO: result = SYSCALL_ENOSYS; break; // RT signals
            case SYS_PERF_EVENT_OPEN: result = SYSCALL_ENOSYS; break; // Performance monitoring
            case SYS_RECVMMSG:    result = SYSCALL_ENOSYS; break;  // Multiple messages
            case SYS_FANOTIFY_INIT: result = SYSCALL_ENOSYS; break; // Fanotify
            case SYS_FANOTIFY_MARK: result = SYSCALL_ENOSYS; break; // Fanotify
            case SYS_PRLIMIT64:   result = SYSCALL_ENOSYS; break;  // Resource limits
            case SYS_NAME_TO_HANDLE_AT: result = SYSCALL_ENOSYS; break; // File handles
            case SYS_OPEN_BY_HANDLE_AT: result = SYSCALL_ENOSYS; break; // File handles
            case SYS_CLOCK_ADJTIME: result = SYSCALL_ENOSYS; break; // Clock adjustment
            case SYS_SYNCFS:      result = SYSCALL_ENOSYS; break;  // Sync filesystem
            case SYS_SENDMMSG:    result = SYSCALL_ENOSYS; break;  // Multiple messages
            case SYS_SETNS:       result = SYSCALL_ENOSYS; break;  // Namespace operations
            case SYS_GETCPU:      result = SYSCALL_ENOSYS; break;  // CPU information
            case SYS_PROCESS_VM_READV: result = SYSCALL_ENOSYS; break; // Process VM
            case SYS_PROCESS_VM_WRITEV: result = SYSCALL_ENOSYS; break; // Process VM
            case SYS_KCMP:        result = SYSCALL_ENOSYS; break;  // Kernel comparison
            case SYS_FINIT_MODULE: result = SYSCALL_ENOSYS; break; // Module loading
            case SYS_SCHED_SETATTR: result = SYSCALL_ENOSYS; break; // Scheduler attributes
            case SYS_SCHED_GETATTR: result = SYSCALL_ENOSYS; break; // Scheduler attributes
            case SYS_RENAMEAT2:   result = SYSCALL_ENOSYS; break;  // Advanced rename
            case SYS_SECCOMP:     result = SYSCALL_ENOSYS; break;  // Seccomp
            case SYS_GETRANDOM:   result = SYSCALL_ENOSYS; break;  // Random numbers
            case SYS_MEMFD_CREATE: result = SYSCALL_ENOSYS; break; // Memory file descriptor
            case SYS_KEXEC_FILE_LOAD: result = SYSCALL_ENOSYS; break; // Kexec
            case SYS_BPF:         result = SYSCALL_ENOSYS; break;  // BPF
            case SYS_EXECVEAT:    result = SYSCALL_ENOSYS; break;  // Execute at
            case SYS_USERFAULTFD: result = SYSCALL_ENOSYS; break;  // User fault FD
            case SYS_MEMBARRIER:  result = SYSCALL_ENOSYS; break;  // Memory barriers
            case SYS_MLOCK2:      result = SYSCALL_ENOSYS; break;  // Advanced mlock
            case SYS_COPY_FILE_RANGE: result = SYSCALL_ENOSYS; break; // Copy file range
            case SYS_PREADV2:     result = SYSCALL_ENOSYS; break;  // Advanced vector I/O
            case SYS_PWRITEV2:    result = SYSCALL_ENOSYS; break;  // Advanced vector I/O
            case SYS_PKEY_MPROTECT: result = SYSCALL_ENOSYS; break; // Protection keys
            case SYS_PKEY_ALLOC:  result = SYSCALL_ENOSYS; break;  // Protection keys
            case SYS_PKEY_FREE:   result = SYSCALL_ENOSYS; break;  // Protection keys
            case SYS_STATX:       result = SYSCALL_ENOSYS; break;  // Advanced stat
            case SYS_IO_PGETEVENTS: result = SYSCALL_ENOSYS; break; // Advanced I/O
            case SYS_RSEQ:        result = SYSCALL_ENOSYS; break;  // Restartable sequences
            case SYS_PIDFD_SEND_SIGNAL: result = SYSCALL_ENOSYS; break; // PID FD signals
            case SYS_IO_URING_SETUP: result = SYSCALL_ENOSYS; break; // io_uring
            case SYS_IO_URING_ENTER: result = SYSCALL_ENOSYS; break; // io_uring
            case SYS_IO_URING_REGISTER: result = SYSCALL_ENOSYS; break; // io_uring
            case SYS_OPEN_TREE:   result = SYSCALL_ENOSYS; break;  // Open tree
            case SYS_MOVE_MOUNT:  result = SYSCALL_ENOSYS; break;  // Move mount
            case SYS_FSOPEN:      result = SYSCALL_ENOSYS; break;  // Filesystem open
            case SYS_FSCONFIG:    result = SYSCALL_ENOSYS; break;  // Filesystem config
            case SYS_FSMOUNT:     result = SYSCALL_ENOSYS; break;  // Filesystem mount
            case SYS_FSPICK:      result = SYSCALL_ENOSYS; break;  // Filesystem pick
            case SYS_PIDFD_OPEN:  result = SYSCALL_ENOSYS; break;  // PID FD open
            case SYS_CLONE3:      result = SYSCALL_ENOSYS; break;  // Advanced clone
            case SYS_CLOSE_RANGE: result = SYSCALL_ENOSYS; break;  // Close range
            case SYS_OPENAT2:     result = SYSCALL_ENOSYS; break;  // Advanced openat
            case SYS_PIDFD_GETFD: result = SYSCALL_ENOSYS; break;  // PID FD get FD
            case SYS_FACCESSAT2:  result = SYSCALL_ENOSYS; break;  // Advanced faccessat
            case SYS_PROCESS_MADVISE: result = SYSCALL_ENOSYS; break; // Process madvise
            case SYS_EPOLL_PWAIT2: result = SYSCALL_ENOSYS; break; // Advanced epoll
            case SYS_MOUNT_SETATTR: result = SYSCALL_ENOSYS; break; // Mount attributes
            case SYS_QUOTACTL_FD: result = SYSCALL_ENOSYS; break;  // Quota control
            case SYS_LANDLOCK_CREATE_RULESET: result = SYSCALL_ENOSYS; break; // Landlock
            case SYS_LANDLOCK_ADD_RULE: result = SYSCALL_ENOSYS; break; // Landlock
            case SYS_LANDLOCK_RESTRICT_SELF: result = SYSCALL_ENOSYS; break; // Landlock
            case SYS_MEMFD_SECRET: result = SYSCALL_ENOSYS; break; // Secret memory
            case SYS_PROCESS_MRELEASE: result = SYSCALL_ENOSYS; break; // Process release
            case SYS_SET_MEMPOLICY_HOME_NODE: result = SYSCALL_ENOSYS; break; // Memory policy
            case SYS_CACHESTAT:   result = SYSCALL_ENOSYS; break;  // Cache statistics
            case SYS_FCHMODAT2:   result = SYSCALL_ENOSYS; break;  // Advanced chmod
            case SYS_MAP_SHADOW_STACK: result = SYSCALL_ENOSYS; break; // Shadow stack
            case SYS_FUTEX_WAKE:  result = sys_futex(arg1, FUTEX_WAKE, arg2, arg3, arg4, arg5); break;
            case SYS_FUTEX_WAIT:  result = sys_futex(arg1, FUTEX_WAIT, arg2, arg3, arg4, arg5); break;
            case SYS_FUTEX_REQUEUE: result = sys_futex(arg1, FUTEX_REQUEUE, arg2, arg3, arg4, arg5); break;
            case SYS_STATMOUNT:   result = SYSCALL_ENOSYS; break;  // Stat mount
            case SYS_LISTMOUNT:   result = SYSCALL_ENOSYS; break;  // List mount
            case SYS_LSM_GET_SELF_ATTR: result = SYSCALL_ENOSYS; break; // LSM attributes
            case SYS_LSM_SET_SELF_ATTR: result = SYSCALL_ENOSYS; break; // LSM attributes
            case SYS_LSM_LIST_MODULES: result = SYSCALL_ENOSYS; break; // LSM modules
            case SYS_MSEAL:       result = SYSCALL_ENOSYS; break;  // Memory sealing
            case SYS_SETXATTRAT:  result = SYSCALL_ENOSYS; break;  // Extended attributes
            case SYS_GETXATTRAT:  result = SYSCALL_ENOSYS; break;  // Extended attributes
            case SYS_LISTXATTRAT: result = SYSCALL_ENOSYS; break;  // Extended attributes
            case SYS_REMOVEXATTRAT: result = SYSCALL_ENOSYS; break; // Extended attributes
            case SYS_OPEN_TREE_ATTR: result = SYSCALL_ENOSYS; break; // Open tree attributes

        default:             result = SYSCALL_ENOSYS; break;
    }
    }
    
    // Handle Forest OS specific syscalls (these are > 100)
    if (num == SYS_MMAP_FB) {
        debuglog(DEBUG_INFO, "[SYSCALL] SYS_MMAP_FB (471) case reached\n");
        result = sys_mmap_fb();
        debuglog(DEBUG_INFO, "[SYSCALL] sys_mmap_fb returned: %d\n", result);
    } else if (num == SYS_MUNMAP_FB) {
        result = sys_munmap_fb((void*)arg1);
    } else if (num == SYS_GET_FB_INFO) {
        result = sys_get_fb_info((void*)arg1);
    } else if (num == SYS_USERCTL) {
        result = sys_user(arg1, arg2, arg3, arg4, arg5, arg6);
    } else if (num == SYS_START_FB_WATCHER) {
        result = sys_start_fb_watcher();
    } else if (num == SYS_STOP_FB_WATCHER) {
        result = sys_stop_fb_watcher();
    } else if (num == SYS_FB_FLUSH) {
        result = sys_fb_flush();
    } else if (num == SYS_READ_KBD_EVENT) {
        result = sys_read_kbd_event((void*)arg1);
    } else if (num == SYS_READ_MOUSE_EVENT) {
        result = sys_read_mouse_event((void*)arg1);
    } else if (num == SYS_POLL_INPUT) {
        result = sys_poll_input();
    } else if (num == SYS_SPAWN_TASK) {
        result = sys_spawn_task(arg1, arg2);
    }
    // Sound/Audio syscalls (Phloem API)
    else if (num == SYS_SOUND_PLAY) {
        // arg1 = PCM data pointer, arg2 = length, arg3 = format struct pointer
        const SoundDriver* driver = sound_active_driver();
        if (!driver || !driver->play_pcm) {
            result = SYSCALL_ENODEV;
        } else if (!arg1 || !arg2 || !arg3) {
            result = SYSCALL_EFAULT;
        } else if (!user_buffer_readable((const void*)arg3, sizeof(SoundFormat))) {
            result = SYSCALL_EFAULT;
        } else {
            const uint64 max_audio_bytes = 4ULL * 1024ULL * 1024ULL;
            if ((uint64)arg2 > max_audio_bytes) {
                result = SYSCALL_EINVAL;
            } else if (!user_buffer_readable((const void*)arg1, (size_t)arg2)) {
                result = SYSCALL_EFAULT;
            } else {
                SoundFormat fmt;
                memory_copy((const char*)arg3, (char*)&fmt, sizeof(SoundFormat));

                uint8* kernel_audio = (uint8*)kmalloc((size_t)arg2);
                if (!kernel_audio) {
                    result = SYSCALL_ENOMEM;
                } else {
                    memory_copy((const char*)arg1, (char*)kernel_audio, (size_t)arg2);
                    result = driver->play_pcm((SoundDriver*)driver, kernel_audio, (uint32)arg2, &fmt)
                        ? 0 : SYSCALL_EIO;
                    kfree(kernel_audio);
                }
            }
        }
    } else if (num == SYS_SOUND_STOP) {
        // No stop function currently, just return success
        result = 0;
    } else if (num == SYS_SOUND_BEEP) {
        // arg1 = frequency, arg2 = duration_ms
        sound_beep((uint32)arg1, (uint32)arg2);
        result = 0;
    } else if (num == SYS_SOUND_SET_VOLUME) {
        // arg1 = volume (0-255)
        sound_set_volume((uint8)arg1);
        result = 0;
    } else if (num == SYS_SOUND_GET_VOLUME) {
        const SoundDriver* driver = sound_active_driver();
        result = driver ? driver->volume : 0;
    } else if (num == SYS_SOUND_GET_INFO) {
        // arg1 = pointer to sound_device_info_t struct
        const SoundDriver* driver = sound_active_driver();
        if (driver && arg1) {
            struct {
                uint32 device_type;
                char name[64];
                uint8 volume;
                uint8 reserved[3];
            } info;
            info.device_type = driver->type;
            info.volume = driver->volume;
            // Copy name safely
            int i;
            for (i = 0; driver->name[i] && i < 63; i++) {
                info.name[i] = driver->name[i];
            }
            info.name[i] = '\0';
            memory_copy((const char*)&info, (char*)arg1, sizeof(info));
            result = 0;
        } else if (!driver) {
            result = SYSCALL_ENODEV;
        } else {
            result = SYSCALL_EFAULT;
        }
    } else if (num == SYS_SOUND_GET_CAPS) {
        // arg1 = pointer to DeviceCapabilities struct
        const SoundDriver* driver = sound_active_driver();
        if (driver && driver->get_capabilities && arg1) {
            DeviceCapabilities caps;
            if (driver->get_capabilities((SoundDriver*)driver, &caps)) {
                memory_copy((const char*)&caps, (char*)arg1, sizeof(caps));
                result = 0;
            } else {
                result = SYSCALL_EIO;
            }
        } else if (!driver) {
            result = SYSCALL_ENODEV;
        } else {
            result = SYSCALL_EFAULT;
        }
    } else if (num == SYS_SOUND_PLAY_WAV) {
        // arg1 = const char* path (userspace)
        char path_buf[256];
        if (!arg1) {
            result = SYSCALL_EFAULT;
        } else if (!user_copy_string(path_buf, sizeof(path_buf), (const char*)arg1)) {
            result = SYSCALL_EFAULT;
        } else {
            result = sound_play_wav(path_buf) ? 0 : SYSCALL_EIO;
        }
    }

#if ARCH_64BIT
    frame->rax = (sys_arg_t)result;
#else
    frame->eax = (uint32)result;
#endif

    // CRITICAL: Check if task was terminated during syscall processing.
    // If so, do NOT return to userspace - enter infinite loop instead.
    // This prevents the #GP fault that occurs when returning to a task
    // that called sys_exit() and was marked TERMINATED.
    if (current_task && current_task->state == TASK_STATE_TERMINATED) {
        print("[SYSCALL] Task terminated during syscall, halting instead of returning to userspace\n");
        // Disable interrupts and halt - task_schedule() should have switched away
        // but if we're here, we can't safely return to userspace
        __asm__ __volatile__("cli");
        while (1) {
            __asm__ __volatile__("hlt");
        }
    }
}
