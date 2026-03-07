#ifndef VFS_H
#define VFS_H

#include "types.h"
#include "stdbool.h"
#include <stddef.h>

// VFS node types
#define VFS_FILE        0x01
#define VFS_DIRECTORY   0x02
#define VFS_CHARDEVICE  0x03
#define VFS_BLOCKDEVICE 0x04
#define VFS_PIPE        0x05
#define VFS_SYMLINK     0x06
#define VFS_MOUNTPOINT  0x08
#define VFS_DELETED     0x10    // File marked as deleted (deferred deletion)
#define VFS_DELETING    0x20    // File is being deleted (has open handles)

// VFS open flags
#define VFS_READ        0x01
#define VFS_WRITE       0x02
#define VFS_CREATE      0x04
#define VFS_APPEND      0x08
#define VFS_TRUNC       0x10
#define VFS_NONBLOCK    0x20    // Non-blocking I/O
#define VFS_EXCL        0x40    // Exclusive open
#define VFS_DEFER_DELETE 0x80   // Deferred deletion (delete when last handle closes)

// Device number macros
#define MKDEV(major, minor)  (((uint32)(major) << 16) | ((minor) & 0xFFFF))
#define MAJOR(dev)           (((dev) >> 16) & 0xFFFF)
#define MINOR(dev)           ((dev) & 0xFFFF)

// Major device numbers (kernel-defined)
#define INPUT_MAJOR     13      // Input devices (keyboard, mouse)
#define TTY_MAJOR       4       // TTY devices
#define CONSOLE_MAJOR   5       // Console device

// Minor device numbers for input devices
#define INPUT_MINOR_KBD     0   // /dev/kbd or /dev/input/event0
#define INPUT_MINOR_MOUSE   1   // /dev/mouse or /dev/input/event1

// Poll event flags
#define POLLIN          0x0001  // Data available for reading
#define POLLPRI         0x0002  // Urgent data available
#define POLLOUT         0x0004  // Writing now will not block
#define POLLERR         0x0008  // Error condition
#define POLLHUP         0x0010  // Hang up
#define POLLNVAL        0x0020  // Invalid request

// Forward declarations
typedef struct vfs_node vfs_node_t;
typedef struct vfs_dirent vfs_dirent_t;
typedef struct vfs_filesystem vfs_filesystem_t;
typedef struct vfs_mount vfs_mount_t;

// File stat structure (must be defined before vfs_node_ops)
struct vfs_stat {
    uint32 dev;
    uint32 ino;
    uint32 mode;
    uint32 nlink;
    uint32 uid;
    uint32 gid;
    uint32 rdev;
    uint32 size;
    uint32 blksize;
    uint32 blocks;
    uint32 atime;
    uint32 mtime;
    uint32 ctime;
};

// Directory entry
struct vfs_dirent {
    char name[256];
    uint32 inode;
};

// VFS node structure
struct vfs_node {
    char name[128];
    uint32 mask;
    uint32 uid;
    uint32 gid;
    uint32 flags;
    uint32 inode;
    uint32 length;
    uint32 impl;

    // Device support fields
    uint16 major;           // Major device number (driver class)
    uint16 minor;           // Minor device number (device instance)
    uint32 open_count;      // Reference count for multiple openers
    uint32 flags_mode;      // Open mode flags (VFS_NONBLOCK, etc.)

    // File operations
    uint32 (*read)(struct vfs_node* node, uint32 offset, uint32 size, uint8* buffer);
    uint32 (*write)(struct vfs_node* node, uint32 offset, uint32 size, uint8* buffer);
    void (*open)(struct vfs_node* node, uint32 flags);
    void (*close)(struct vfs_node* node);
    bool (*readdir)(struct vfs_node* node, uint32 index, struct vfs_dirent* dirent);
    struct vfs_node* (*finddir)(struct vfs_node* node, const char* name);

    // Extended operations for character/block devices
    int (*ioctl)(struct vfs_node* node, uint32 request, void* arg);
    int (*poll)(struct vfs_node* node, uint32 events);

    // Internal data
    void* internal_data;
    struct vfs_node* ptr;

    // Extended file operations
    int (*unlink)(struct vfs_node* node, const char* name);
    int (*mkdir)(struct vfs_node* node, const char* name, uint32 mode);
    int (*rmdir)(struct vfs_node* node, const char* name);
    int (*rename)(struct vfs_node* node, const char* oldname, const char* newname);
    int (*truncate)(struct vfs_node* node, uint32 length);
    int (*stat)(struct vfs_node* node, struct vfs_stat* statbuf);
    int (*chmod)(struct vfs_node* node, uint32 mode);
    int (*chown)(struct vfs_node* node, uint32 uid, uint32 gid);
    int (*sync)(struct vfs_node* node);
};

// VFS filesystem operations
struct vfs_filesystem {
    const char* name;
    uint32 (*probe)(void* dev_data, void* read_sector, void* write_sector);
    bool (*mount)(void* dev_data, void* read_sector, void* write_sector, uint64_t sectors, void** sb_out);
    bool (*umount)(void* sb);
    vfs_node_t* (*get_root)(void* sb);
    vfs_node_t* (*lookup)(void* sb, const char* path);
    int (*mkdir)(void* sb, const char* path, uint32 mode);
    int (*rmdir)(void* sb, const char* path);
    int (*unlink)(void* sb, const char* path);
    int (*rename)(void* sb, const char* oldpath, const char* newpath);
    int (*truncate)(void* sb, const char* path, uint32 length);
    int (*sync)(void* sb);
    struct vfs_filesystem* next;
};

// VFS mount structure (no block device requirement)
struct vfs_mount {
    char mountpoint[256];
    char device[256];
    vfs_filesystem_t* fs;
    void* fs_data;
    vfs_node_t* root;
    uint32 flags;
    struct vfs_mount* next;
};

// Deferred operation types
typedef enum {
    VFS_DEFERRED_DELETE,
    VFS_DEFERRED_RENAME,
    VFS_DEFERRED_RMDIR,
    VFS_DEFERRED_TRUNCATE,
    VFS_DEFERRED_SYNC
} vfs_deferred_op_t;

// Deferred operation entry
typedef struct vfs_deferred_op {
    vfs_deferred_op_t type;
    char path[256];
    char newpath[256];  // For rename
    uint32 value;       // For truncate, mode, etc.
    void* data;
    uint32 data_size;
    struct vfs_deferred_op* next;
} vfs_deferred_op_entry_t;

// VFS initialization
bool vfs_init(void);

// File operations
vfs_node_t* vfs_open(const char* path, uint32 flags);
void vfs_close(vfs_node_t* node);
uint32 vfs_read(vfs_node_t* node, uint32 offset, uint32 size, uint8* buffer);
uint32 vfs_write(vfs_node_t* node, uint32 offset, uint32 size, uint8* buffer);
bool vfs_readdir(vfs_node_t* node, uint32 index, vfs_dirent_t* dirent);
vfs_node_t* vfs_finddir(vfs_node_t* node, const char* name);

// Legacy function (kept for compatibility)
bool vfs_read_file(const char* path, const uint8** data, uint32* size);

// Extended operations for device files
int vfs_ioctl(vfs_node_t* node, uint32 request, void* arg);
int vfs_poll(vfs_node_t* node, uint32 events);

// Device node creation helper
vfs_node_t* vfs_create_device_node(const char* name, uint32 flags,
                                    uint16 major, uint16 minor,
                                    void* driver_data);

// Extended file operations
int vfs_unlink(const char* path);
int vfs_mkdir(const char* path, uint32 mode);
int vfs_rmdir(const char* path);
int vfs_rename(const char* oldpath, const char* newpath);
int vfs_truncate(const char* path, uint32 length);
int vfs_stat(const char* path, struct vfs_stat* statbuf);
int vfs_chmod(const char* path, uint32 mode);
int vfs_chown(const char* path, uint32 uid, uint32 gid);
int vfs_sync(const char* path);

// Mount/umount operations (no block device required)
int vfs_mount(const char* device, const char* mountpoint, const char* fstype, void* dev_data,
              void* read_sector, void* write_sector, uint64_t sectors);
int vfs_umount(const char* mountpoint);
bool vfs_mount_init(void);

// Filesystem registration
int vfs_register_filesystem(vfs_filesystem_t* fs);
vfs_filesystem_t* vfs_get_filesystem(const char* name);

// Deferred operation management
int vfs_defer_delete(const char* path);
int vfs_defer_rename(const char* oldpath, const char* newpath);
int vfs_defer_truncate(const char* path, uint32 length);
int vfs_process_deferred_ops(void);
int vfs_cancel_deferred_op(const char* path);
bool vfs_has_deferred_ops(void);

// Symbolic link operations
vfs_node_t* vfs_create_symlink(const char* name, const char* target);
int vfs_readlink(vfs_node_t* node, char* buffer, size_t buffer_size);
vfs_node_t* vfs_resolve_symlink(vfs_node_t* symlink);
void vfs_destroy_symlink(vfs_node_t* node);

// Get mount table
vfs_mount_t* vfs_get_mounts(void);

#endif
