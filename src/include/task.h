#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "elf.h"
#include "memory.h" // For page_directory_t

#define USER_HEAP_GUARD_PAGES 1
#define FORK_CHILD_RETURN 0xF00D  // Special return value for child processes

// Process states
typedef enum {
    TASK_STATE_RUNNING,
    TASK_STATE_READY,
    TASK_STATE_WAITING,
    TASK_STATE_TERMINATED
} task_state_t;

// Process group and session states
typedef enum {
    JOB_STATE_RUNNING,
    JOB_STATE_STOPPED,
    JOB_STATE_TERMINATED
} job_state_t;

// Task Control Block (TCB)
typedef struct task {
    char name[32];              // Task name
    uint32 id;                  // Process ID
    uint32 pgrp;                // Process group ID
    uint32 session;             // Session ID
    int32 tty_fd;               // Controlling terminal file descriptor (-1 if none)
    task_state_t state;         // Current state of the task
    uintptr_t kernel_stack;     // Saved kernel stack pointer (ESP/RSP) for this task
    uintptr_t kernel_stack_base; // Base address of the allocated kernel stack
    page_directory_t* page_directory; // Page directory for this task
    elf_load_info_t elf_info;   // ELF loading information (for cleanup, etc.)
    // Userspace memory layout (per-task brk/heap tracking)
    uintptr_t user_heap_base;   // Lowest heap address (page-aligned)
    uintptr_t user_heap_limit;  // Highest heap address allowed before the stack
    uintptr_t user_brk;         // Current program break (may be unaligned)
    int32 exit_code;            // Exit status (set when task terminates)
    char  exit_reason[32];      // Short reason string for termination
    uint32 uid;                 // Owning user
    uint32 gid;                 // Primary group
    uint32 groups_mask;         // Supplemental groups bitmask

    // Scheduling-related fields
    uint32 priority;            // Task priority
    uint32 ticks_left;          // Time slices left for execution
    uint32 pending_signals;     // Bitmap of pending signals
    uint32 sleep_until_tick;    // Tick count to wake up at
    uint32 last_active_tick;    // Last tick when the task made a syscall/IO (0 if never)
    
    // Priority boost (for interactive tasks)
    uint32 original_priority;   // Original priority before boost
    uint32 boost_expires_at;    // Tick when boost expires
    
    // Watchdog (to detect stuck tasks)
    bool watchdog_enabled;      // Whether watchdog is enabled for this task
    uint32 consecutive_timeouts; // Number of consecutive watchdog timeouts
    
    // Background task flag - background tasks don't become foreground automatically
    bool is_background;
    
    // Memory quotas
    uint32 memory_quota;        // Maximum memory allowed (0 = unlimited)
    uint32 memory_used;         // Current memory usage

    // Initial user-mode entry flag: when true, this is the first switch to this task.
    // The kernel stack is pre-built with task_switch_asm frame + IRET frame.
    bool needs_usermode_entry;
    uintptr_t usermode_entry_point;  // ELF entry point for initial jump
    uintptr_t usermode_stack_top;    // User stack top for initial jump

    struct task* next;          // Pointer to the next task in the linked list
} task_t;

// Signal definitions
#define SIGHUP          1       // Hangup
#define SIGINT          2       // Interrupt
#define SIGQUIT         3       // Quit
#define SIGILL          4       // Illegal instruction
#define SIGTRAP         5       // Trace/breakpoint trap
#define SIGABRT         6       // Aborted
#define SIGBUS          7       // Bus error
#define SIGFPE          8       // Floating point exception
#define SIGKILL         9       // Killed
#define SIGUSR1         10      // User defined signal 1
#define SIGSEGV         11      // Segmentation fault
#define SIGUSR2         12      // User defined signal 2
#define SIGPIPE         13      // Broken pipe
#define SIGALRM         14      // Alarm clock
#define SIGTERM         15      // Terminated
#define SIGSTKFLT       16      // Stack fault
#define SIGCHLD         17      // Child exited
#define SIGCONT         18      // Continued
#define SIGSTOP         19      // Stopped (signal)
#define SIGTSTP         20      // Stopped
#define SIGTTIN         21      // Stopped (tty input)
#define SIGTTOU         22      // Stopped (tty output)
#define SIGURG          23      // Urgent I/O condition
#define SIGXCPU         24      // CPU time limit exceeded
#define SIGXFSZ         25      // File size limit exceeded
#define SIGVTALRM       26      // Virtual timer expired
#define SIGPROF         27      // Profiling timer expired
#define SIGWINCH        28      // Window changed
#define SIGIO           29      // I/O possible
#define SIGPWR          30      // Power failure
#define SIGSYS          31      // Bad system call

// Function prototypes for task management
void tasks_init(void);
task_t* task_create_elf(const uint8* elf_data, size_t elf_size, const char* name);
task_t* task_create_kernel(void (*entry_point)(void), const char* name, uint32 stack_size);
task_t* task_clone_current(void);
void task_destroy(task_t* task);
void task_switch(task_t* next_task); // Updated signature
void task_schedule(void);
void task_kill(uint32 pid); // Added
void task_terminate_current(int signal);  // Terminate current task with signal
void task_exit(int code, const char* reason); // Graceful exit with reason
bool task_exists(uint32 pid);
int32 task_wait_pid(uint32 pid);
int32 task_get_exit_code(uint32 pid);
uint32 task_get_last_active_tick(uint32 pid);
void task_mark_active(void);
void debug_print_ready_queue(void); // Debug function

void sleep_busy(uint32 microseconds);
void sleep_interruptible(uint32 milliseconds);
void task_shutdown_all(void);
void task_yield(void);  // Yield CPU to next task

// Foreground task API - keeps a GUI app running with priority scheduling
void task_set_foreground(task_t* task);        // Set task as foreground (gets priority)
void task_clear_foreground(void);              // Clear foreground status
task_t* task_get_foreground(void);             // Get current foreground task
bool task_is_foreground(task_t* task);         // Check if task is foreground

// Signal management functions
void task_send_signal(uint32 pid, int signal);     // Send signal to a specific task or process group
void task_send_signal_to_pgrp(uint32 pgrp, int signal); // Send signal to all tasks in a process group

// Global variables (defined in task.c)
extern task_t* current_task;
extern task_t* ready_queue_head;

#endif // TASK_H
