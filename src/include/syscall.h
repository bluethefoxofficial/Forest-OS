#ifndef SYSCALL_H
#define SYSCALL_H

#include "types.h"
#include "framebuffer.h"

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#define ARCH_32BIT 0
#else
#define ARCH_64BIT 0
#define ARCH_32BIT 1
#endif
#endif

#define SYSCALL_VECTOR 0x80

// Linux x86_64 syscall numbers - complete table
enum syscall_number {
    SYS_READ                    = 0,
    SYS_WRITE                   = 1,
    SYS_OPEN                    = 2,
    SYS_CLOSE                   = 3,
    SYS_STAT                    = 4,
    SYS_FSTAT                   = 5,
    SYS_LSTAT                   = 6,
    SYS_POLL                    = 7,
    SYS_LSEEK                   = 8,
    SYS_MMAP                    = 9,
    SYS_MPROTECT                = 10,
    SYS_MUNMAP                  = 11,
    SYS_BRK                     = 12,
    SYS_RT_SIGACTION            = 13,
    SYS_RT_SIGPROCMASK          = 14,
    SYS_RT_SIGRETURN            = 15,
    SYS_IOCTL                   = 16,
    SYS_PREAD64                 = 17,
    SYS_PWRITE64                = 18,
    SYS_READV                   = 19,
    SYS_WRITEV                  = 20,
    SYS_ACCESS                  = 21,
    SYS_PIPE                    = 22,
    SYS_SELECT                  = 23,
    SYS_SCHED_YIELD             = 24,
    SYS_MREMAP                  = 25,
    SYS_MSYNC                   = 26,
    SYS_MINCORE                 = 27,
    SYS_MADVISE                 = 28,
    SYS_SHMGET                  = 29,
    SYS_SHMAT                   = 30,
    SYS_SHMCTL                  = 31,
    SYS_DUP                     = 32,
    SYS_DUP2                    = 33,
    SYS_PAUSE                   = 34,
    SYS_NANOSLEEP               = 35,
    SYS_GETITIMER               = 36,
    SYS_ALARM                   = 37,
    SYS_SETITIMER               = 38,
    SYS_GETPID                  = 39,
    SYS_SENDFILE                = 40,
    SYS_SOCKET                  = 41,
    SYS_CONNECT                 = 42,
    SYS_ACCEPT                  = 43,
    SYS_SENDTO                  = 44,
    SYS_RECVFROM                = 45,
    SYS_SENDMSG                 = 46,
    SYS_RECVMSG                 = 47,
    SYS_SHUTDOWN                = 48,
    SYS_BIND                    = 49,
    SYS_LISTEN                  = 50,
    SYS_GETSOCKNAME             = 51,
    SYS_GETPEERNAME             = 52,
    SYS_SOCKETPAIR              = 53,
    SYS_SETSOCKOPT              = 54,
    SYS_GETSOCKOPT              = 55,
    SYS_CLONE                   = 56,
    SYS_FORK                    = 57,
    SYS_VFORK                   = 58,
    SYS_EXECVE                  = 59,
    SYS_EXIT                    = 60,
    SYS_WAIT4                   = 61,
    SYS_KILL                    = 62,
    SYS_UNAME                   = 63,
    SYS_SEMGET                  = 64,
    SYS_SEMOP                   = 65,
    SYS_SEMCTL                  = 66,
    SYS_SHMDT                   = 67,
    SYS_MSGGET                  = 68,
    SYS_MSGSND                  = 69,
    SYS_MSGRCV                  = 70,
    SYS_MSGCTL                  = 71,
    SYS_FCNTL                   = 72,
    SYS_FLOCK                   = 73,
    SYS_FSYNC                   = 74,
    SYS_FDATASYNC               = 75,
    SYS_TRUNCATE                = 76,
    SYS_FTRUNCATE               = 77,
    SYS_GETDENTS                = 78,
    SYS_GETCWD                  = 79,
    SYS_CHDIR                   = 80,
    SYS_FCHDIR                  = 81,
    SYS_RENAME                  = 82,
    SYS_MKDIR                   = 83,
    SYS_RMDIR                   = 84,
    SYS_CREAT                   = 85,
    SYS_LINK                    = 86,
    SYS_UNLINK                  = 87,
    SYS_SYMLINK                 = 88,
    SYS_READLINK                = 89,
    SYS_CHMOD                   = 90,
    SYS_FCHMOD                  = 91,
    SYS_CHOWN                   = 92,
    SYS_FCHOWN                  = 93,
    SYS_LCHOWN                  = 94,
    SYS_UMASK                   = 95,
    SYS_GETTIMEOFDAY            = 96,
    SYS_GETRLIMIT               = 97,
    SYS_GETRUSAGE               = 98,
    SYS_SYSINFO                 = 99,
    SYS_TIMES                   = 100,
    SYS_PTRACE                  = 101,
    SYS_GETUID                  = 102,
    SYS_SYSLOG                  = 103,
    SYS_GETGID                  = 104,
    SYS_SETUID                  = 105,
    SYS_SETGID                  = 106,
    SYS_GETEUID                 = 107,
    SYS_GETEGID                 = 108,
    SYS_SETPGID                 = 109,
    SYS_GETPPID                 = 110,
    SYS_GETPGRP                 = 111,
    SYS_SETSID                  = 112,
    SYS_SETEUID                 = 113,
    SYS_SETEGID                 = 114,
    SYS_TCGETPGRP               = 127,
    SYS_TCSETPGRP               = 128,
    SYS_SETREUID                = 115,
    SYS_SETREGID                = 116,
    SYS_GETGROUPS               = 117,
    SYS_SETGROUPS               = 118,
    SYS_SETRESUID               = 119,
    SYS_GETRESUID               = 120,
    SYS_SETRESGID               = 121,
    SYS_GETRESGID               = 122,
    SYS_SETFSUID                = 123,
    SYS_SETFSGID                = 124,
    SYS_GETPGID                 = 125,
    SYS_GETSID                  = 126,
    SYS_CAPGET                  = 129,
    SYS_CAPSET                  = 130,
    SYS_RT_SIGPENDING           = 131,
    SYS_RT_SIGTIMEDWAIT         = 132,
    SYS_RT_SIGQUEUEINFO         = 133,
    SYS_RT_SIGSUSPEND           = 134,
    SYS_SIGALTSTACK             = 135,
    SYS_UTIME                   = 136,
    SYS_MKNOD                   = 137,
    SYS_STATFS                  = 138,
    SYS_FSTATFS                 = 139,
    SYS_PERSONALITY             = 140,
    SYS_USTAT                   = 141,
    SYS_SYSFS                   = 142,
    SYS_GETPRIORITY             = 143,
    SYS_SETPRIORITY             = 144,
    SYS_SCHED_SETPARAM          = 143,
    SYS_SCHED_GETPARAM          = 144,
    SYS_SCHED_SETSCHEDULER      = 145,
    SYS_SCHED_GETSCHEDULER      = 146,
    SYS_SCHED_GET_PRIORITY_MAX  = 148,
    SYS_SCHED_GET_PRIORITY_MIN  = 149,
    SYS_SCHED_RR_GET_INTERVAL   = 150,
    SYS_MLOCK                   = 151,
    SYS_MUNLOCK                 = 152,
    SYS_MLOCKALL                = 153,
    SYS_MUNLOCKALL              = 154,
    SYS_VHANGUP                 = 155,
    SYS_MODIFY_LDT              = 156,
    SYS_PIVOT_ROOT              = 157,
    SYS_PRCTL                   = 159,
    SYS_ARCH_PRCTL              = 160,
    SYS_ADJTIMEX                = 161,
    SYS_SETRLIMIT               = 162,
    SYS_CHROOT                  = 163,
    SYS_SYNC                    = 164,
    SYS_ACCT                    = 165,
    SYS_SETTIMEOFDAY            = 166,
    SYS_MOUNT                   = 167,
    SYS_UMOUNT2                 = 168,
    SYS_SWAPON                  = 169,
    SYS_SWAPOFF                 = 170,
    SYS_REBOOT                  = 171,
    SYS_SETHOSTNAME             = 172,
    SYS_SETDOMAINNAME           = 173,
    SYS_IOPL                    = 174,
    SYS_IOPERM                  = 175,
    SYS_INIT_MODULE             = 177,
    SYS_DELETE_MODULE           = 178,
    SYS_QUOTACTL                = 181,
    SYS_GETTID                  = 188,
    SYS_READAHEAD               = 189,
    SYS_SETXATTR                = 190,
    SYS_LSETXATTR               = 191,
    SYS_FSETXATTR               = 192,
    SYS_GETXATTR                = 193,
    SYS_LGETXATTR               = 194,
    SYS_FGETXATTR               = 195,
    SYS_LISTXATTR               = 196,
    SYS_LLISTXATTR              = 197,
    SYS_FLISTXATTR              = 198,
    SYS_REMOVEXATTR             = 199,
    SYS_LREMOVEXATTR            = 200,
    SYS_FREMOVEXATTR            = 201,
    SYS_TKILL                   = 202,
    SYS_TIME                    = 203,
    SYS_FUTEX                   = 204,
    SYS_SCHED_SETAFFINITY       = 205,
    SYS_SCHED_GETAFFINITY       = 206,
    SYS_IO_SETUP                = 208,
    SYS_IO_DESTROY              = 209,
    SYS_IO_GETEVENTS            = 210,
    SYS_IO_SUBMIT               = 211,
    SYS_IO_CANCEL               = 212,
    SYS_LOOKUP_DCOOKIE          = 214,
    SYS_EPOLL_CREATE            = 215,
    SYS_REMAP_FILE_PAGES        = 218,
    SYS_GETDENTS64              = 219,
    SYS_SET_TID_ADDRESS         = 220,
    SYS_RESTART_SYSCALL         = 221,
    SYS_SEMTIMEDOP              = 222,
    SYS_FADVISE64               = 223,
    SYS_TIMER_CREATE            = 224,
    SYS_TIMER_SETTIME           = 225,
    SYS_TIMER_GETTIME           = 226,
    SYS_TIMER_GETOVERRUN        = 227,
    SYS_TIMER_DELETE            = 228,
    SYS_CLOCK_SETTIME           = 229,
    SYS_CLOCK_GETTIME           = 230,
    SYS_CLOCK_GETRES            = 231,
    SYS_CLOCK_NANOSLEEP         = 232,
    SYS_EXIT_GROUP              = 233,
    SYS_EPOLL_WAIT              = 234,
    SYS_EPOLL_CTL               = 235,
    SYS_TGKILL                  = 236,
    SYS_UTIMES                  = 237,
    SYS_MBIND                   = 239,
    SYS_SET_MEMPOLICY           = 240,
    SYS_GET_MEMPOLICY           = 241,
    SYS_MQ_OPEN                 = 242,
    SYS_MQ_UNLINK               = 243,
    SYS_MQ_TIMEDSEND            = 244,
    SYS_MQ_TIMEDRECEIVE         = 245,
    SYS_MQ_NOTIFY               = 246,
    SYS_MQ_GETSETATTR           = 247,
    SYS_KEXEC_LOAD              = 248,
    SYS_WAITID                  = 249,
    SYS_ADD_KEY                 = 250,
    SYS_REQUEST_KEY             = 251,
    SYS_KEYCTL                  = 252,
    SYS_IOPRIO_SET              = 253,
    SYS_IOPRIO_GET              = 254,
    SYS_INOTIFY_INIT            = 255,
    SYS_INOTIFY_ADD_WATCH       = 256,
    SYS_INOTIFY_RM_WATCH        = 257,
    SYS_MIGRATE_PAGES           = 258,
    SYS_OPENAT                  = 259,
    SYS_MKDIRAT                 = 260,
    SYS_MKNODAT                 = 261,
    SYS_FCHOWNAT                = 262,
    SYS_FUTIMESAT               = 263,
    SYS_NEWFSTATAT              = 264,
    SYS_UNLINKAT                = 265,
    SYS_RENAMEAT                = 266,
    SYS_LINKAT                  = 267,
    SYS_SYMLINKAT               = 268,
    SYS_READLINKAT              = 269,
    SYS_FCHMODAT                = 270,
    SYS_FACCESSAT               = 271,
    SYS_PSELECT6                = 272,
    SYS_PPOLL                   = 273,
    SYS_UNSHARE                 = 274,
    SYS_SET_ROBUST_LIST         = 275,
    SYS_GET_ROBUST_LIST         = 276,
    SYS_SPLICE                  = 277,
    SYS_TEE                     = 278,
    SYS_SYNC_FILE_RANGE         = 279,
    SYS_VMSPLICE                = 280,
    SYS_MOVE_PAGES              = 281,
    SYS_UTIMENSAT               = 282,
    SYS_EPOLL_PWAIT             = 283,
    SYS_SIGNALFD                = 284,
    SYS_TIMERFD_CREATE          = 285,
    SYS_EVENTFD                 = 286,
    SYS_FALLOCATE               = 287,
    SYS_TIMERFD_SETTIME         = 288,
    SYS_TIMERFD_GETTIME         = 289,
    SYS_ACCEPT4                 = 290,
    SYS_SIGNALFD4               = 291,
    SYS_EVENTFD2                = 292,
    SYS_EPOLL_CREATE1           = 293,
    SYS_DUP3                    = 294,
    SYS_PIPE2                   = 295,
    SYS_INOTIFY_INIT1           = 296,
    SYS_PREADV                  = 297,
    SYS_PWRITEV                 = 298,
    SYS_RT_TGSIGQUEUEINFO       = 299,
    SYS_PERF_EVENT_OPEN         = 300,
    SYS_RECVMMSG                = 301,
    SYS_FANOTIFY_INIT           = 302,
    SYS_FANOTIFY_MARK           = 303,
    SYS_PRLIMIT64               = 304,
    SYS_NAME_TO_HANDLE_AT       = 305,
    SYS_OPEN_BY_HANDLE_AT       = 306,
    SYS_CLOCK_ADJTIME           = 307,
    SYS_SYNCFS                  = 308,
    SYS_SENDMMSG                = 309,
    SYS_SETNS                   = 310,
    SYS_GETCPU                  = 311,
    SYS_PROCESS_VM_READV        = 312,
    SYS_PROCESS_VM_WRITEV       = 313,
    SYS_KCMP                    = 314,
    SYS_FINIT_MODULE            = 315,
    SYS_SCHED_SETATTR           = 316,
    SYS_SCHED_GETATTR           = 317,
    SYS_RENAMEAT2               = 318,
    SYS_SECCOMP                 = 319,
    SYS_GETRANDOM               = 320,
    SYS_MEMFD_CREATE            = 321,
    SYS_KEXEC_FILE_LOAD         = 322,
    SYS_BPF                     = 323,
    SYS_EXECVEAT                = 324,
    SYS_USERFAULTFD             = 325,
    SYS_MEMBARRIER              = 326,
    SYS_MLOCK2                  = 327,
    SYS_COPY_FILE_RANGE         = 328,
    SYS_PREADV2                 = 329,
    SYS_PWRITEV2                = 330,
    SYS_PKEY_MPROTECT           = 331,
    SYS_PKEY_ALLOC              = 332,
    SYS_PKEY_FREE               = 333,
    SYS_STATX                   = 334,
    SYS_IO_PGETEVENTS           = 335,
    SYS_RSEQ                    = 336,
    SYS_PIDFD_SEND_SIGNAL       = 426,
    SYS_IO_URING_SETUP          = 427,
    SYS_IO_URING_ENTER          = 428,
    SYS_IO_URING_REGISTER       = 429,
    SYS_OPEN_TREE               = 430,
    SYS_MOVE_MOUNT              = 431,
    SYS_FSOPEN                  = 432,
    SYS_FSCONFIG                = 433,
    SYS_FSMOUNT                 = 434,
    SYS_FSPICK                  = 435,
    SYS_PIDFD_OPEN              = 436,
    SYS_CLONE3                  = 437,
    SYS_CLOSE_RANGE             = 438,
    SYS_OPENAT2                 = 439,
    SYS_PIDFD_GETFD             = 440,
    SYS_FACCESSAT2              = 441,
    SYS_PROCESS_MADVISE         = 442,
    SYS_EPOLL_PWAIT2            = 443,
    SYS_MOUNT_SETATTR           = 444,
    SYS_QUOTACTL_FD             = 445,
    SYS_LANDLOCK_CREATE_RULESET = 446,
    SYS_LANDLOCK_ADD_RULE       = 447,
    SYS_LANDLOCK_RESTRICT_SELF  = 448,
    SYS_MEMFD_SECRET            = 449,
    SYS_PROCESS_MRELEASE        = 450,
    SYS_FUTEX_WAITV             = 451,
    SYS_SET_MEMPOLICY_HOME_NODE = 452,
    SYS_CACHESTAT               = 453,
    SYS_FCHMODAT2               = 454,
    SYS_MAP_SHADOW_STACK        = 455,
    SYS_FUTEX_WAKE              = 456,
    SYS_FUTEX_WAIT              = 457,
    SYS_FUTEX_REQUEUE           = 458,
    SYS_STATMOUNT               = 459,
    SYS_LISTMOUNT               = 460,
    SYS_LSM_GET_SELF_ATTR       = 461,
    SYS_LSM_SET_SELF_ATTR       = 462,
    SYS_LSM_LIST_MODULES        = 463,
    SYS_MSEAL                   = 464,
    SYS_SETXATTRAT              = 465,
    SYS_GETXATTRAT              = 466,
    SYS_LISTXATTRAT             = 467,
    SYS_REMOVEXATTRAT           = 468,
    SYS_OPEN_TREE_ATTR          = 469,
    SYS_NETINFO                 = 470,
    SYS_MMAP_FB                 = 471,
    SYS_MUNMAP_FB               = 472,
    SYS_GET_FB_INFO             = 473,
    SYS_POWER                   = 474,
    SYS_USERCTL                 = 475,
    SYS_START_FB_WATCHER        = 476,
    SYS_STOP_FB_WATCHER         = 477,
    SYS_FB_FLUSH                = 478,

    // Input device syscalls (for direct input event reading)
    SYS_READ_KBD_EVENT          = 479,
    SYS_READ_MOUSE_EVENT        = 480,
    SYS_POLL_INPUT              = 481,

    // Sound/Audio syscalls (Phloem API)
    SYS_SOUND_PLAY              = 482,  // Play PCM audio data
    SYS_SOUND_STOP              = 483,  // Stop current playback
    SYS_SOUND_BEEP              = 484,  // Generate beep tone
    SYS_SOUND_SET_VOLUME        = 485,  // Set master volume (0-255)
    SYS_SOUND_GET_VOLUME        = 486,  // Get current volume
    SYS_SOUND_GET_INFO          = 487,  // Get sound device info
    SYS_SOUND_GET_CAPS          = 488,  // Get device capabilities
    SYS_SOUND_PLAY_WAV          = 489,  // Queue WAV playback by path (non-blocking)
    SYS_SPAWN_TASK              = 490,  // Spawn ELF from VFS path as a new task

    // Maximum syscall number
    SYS_MAX                     = 491
};

// Time structures (for nanosleep, gettimeofday, etc.)
// These definitions may conflict with libc headers, so we guard them carefully
#ifndef TIME_STRUCTURES_DEFINED
#define TIME_STRUCTURES_DEFINED
struct timeval {
    uint32 tv_sec;   // seconds
    uint32 tv_usec;  // microseconds
};

struct timespec {
    uint32 tv_sec;   // seconds
    uint32 tv_nsec;  // nanoseconds
};
#endif

// System call argument type (based on architecture)
#if ARCH_64BIT
typedef uint64 sys_arg_t;
#else
typedef uint32 sys_arg_t;
#endif

#ifndef USERSPACE_BUILD
typedef struct {
#if ARCH_64BIT
    /* Register snapshot for 64-bit int 0x80 ABI:
     * rax = syscall number / return value
     * rbx, rcx, rdx, rsi, rdi, rbp = arguments 1..6
     * rsp captured for completeness/debugging
     */
    uint64 rdi;
    uint64 rsi;
    uint64 rbp;
    uint64 rsp;
    uint64 rbx;
    uint64 rdx;
    uint64 rcx;
    uint64 rax;
#else
    // pusha pushes EAX first, EDI last. On stack (low to high address):
    // EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX
    // Struct fields must match stack layout (ESP points to EDI after pusha)
    uint32 edi;  // Argument 5 (at lowest address, top of stack)
    uint32 esi;  // Argument 4
    uint32 ebp;  // Argument 6
    uint32 esp;  // Original ESP (saved by pusha, not used)
    uint32 ebx;  // Argument 1
    uint32 edx;  // Argument 3
    uint32 ecx;  // Argument 2
    uint32 eax;  // System call number and return value (at highest address)
#endif
} syscall_frame_t;

// Forward declaration for framebuffer info
// typedef struct fb_info fb_info_t; // Already defined in framebuffer.h

// Framebuffer and power syscalls (Forest OS extensions)
extern long sys_mmap_fb(void);
extern long sys_munmap_fb(void* addr);
extern long sys_get_fb_info(fb_info_t* user_info);
extern int32 sys_power(int32 action);
extern int32 sys_user(sys_arg_t arg1, sys_arg_t arg2, sys_arg_t arg3, sys_arg_t arg4, sys_arg_t arg5, sys_arg_t arg6);

void syscall_init(void);
void syscall_handle(syscall_frame_t* frame);
#endif

// mmap constants (available to both kernel and userspace)
#define PROT_NONE   0x00
#define PROT_READ   0x01
#define PROT_WRITE  0x02
#define PROT_EXEC   0x04

#define MAP_SHARED     0x01
#define MAP_PRIVATE    0x02
#define MAP_FIXED      0x10
#define MAP_ANONYMOUS  0x20

#endif
