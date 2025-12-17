#ifndef TASK_H
#define TASK_H

#include "types.h"
#include "elf.h"
#include "memory.h" // For page_directory_t

// Process states
typedef enum {
    TASK_STATE_RUNNING,
    TASK_STATE_READY,
    TASK_STATE_WAITING,
    TASK_STATE_TERMINATED
} task_state_t;

// Task Control Block (TCB)
typedef struct task {
    uint32 id;                  // Process ID
    task_state_t state;         // Current state of the task
    uint32 kernel_stack;        // Saved kernel stack pointer (ESP) for this task
    uint32 kernel_stack_base;   // Base address of the allocated kernel stack
    page_directory_t* page_directory; // Page directory for this task
    elf_load_info_t elf_info;   // ELF loading information (for cleanup, etc.)

    // Scheduling-related fields
    uint32 priority;            // Task priority
    uint32 ticks_left;          // Time slices left for execution
    uint32 pending_signals;     // Bitmap of pending signals

    struct task* next;          // Pointer to the next task in the linked list
} task_t;

// Signal definitions
#define SIGKILL 1 // Basic kill signal

// Function prototypes for task management
void tasks_init(void);
task_t* task_create_elf(const uint8* elf_data, size_t elf_size, const char* name);
void task_destroy(task_t* task);
void task_switch(task_t* next_task); // Updated signature
void task_schedule(void);
void task_kill(uint32 pid); // Added


// Global variables (defined in task.c)
extern task_t* current_task;
extern task_t* ready_queue_head;

#endif // TASK_H
