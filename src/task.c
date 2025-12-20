#include "include/task.h"
#include "include/memory.h"
#include "include/screen.h"
#include "include/panic.h"
#include "include/util.h"
#include "include/elf.h"
#include "include/interrupt.h" // For context switching using new system
#include "include/gdt.h"
#include "include/spinlock.h"
#include "include/string.h"
#include "include/timer.h"

// Explicit forward declaration to help compiler resolve implicit declaration
extern page_directory_t* vmm_get_current_page_directory(void);
extern string long_to_string(long n);

#define KERNEL_STACK_SIZE 8192 // 8KB for kernel stack per task
#define USER_STACK_SIZE 4    // 4 pages, 16KB for user stack
// USER_STACK_TOP is defined in memory.h

task_t* current_task = 0;
task_t* ready_queue_head = 0;
static task_t* idle_task = 0; // Idle task that runs when no other tasks are available
static uint32 next_task_id = 1;

static spinlock_t task_scheduler_lock = SPINLOCK_INIT("task_scheduler");

// Temporary stack for initial task setup
// This will be replaced by a proper kernel stack for each task
static uint8 initial_kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(4096)));

// Idle task function - runs when no other tasks are available
static void idle_task_function(void) {
    for (;;) {
        // Halt and wait for interrupts to wake us up
        __asm__ __volatile__("hlt");
    }
} 

// Helper function to set up initial CPU state for a new task
// This function prepares the kernel stack for the new task such that
// when context_switch_asm loads this task, it finds a valid stack frame
// for popa/popf/iret to jump to user mode.
// Forward declaration for assembly function
extern void task_start_usermode_asm(void);

static void setup_initial_cpu_state(task_t* task, uint32 entry_point, uint32 user_stack_top, uint32 kernel_stack_top) {
    // The kernel stack for the new task will look like this (from high address to low):
    // ...
    // [iret frame] -- pushed by hardware if interrupt, or simulated here
    // [general purpose registers] -- pushed by pusha, or simulated here
    // The task->kernel_stack will point to the saved ESP of general purpose regs.

    // Start with the highest address of the allocated kernel stack
    uint32* stack_ptr = (uint32*)kernel_stack_top;

    // Simulate the IRET frame (pushed last, popped first by IRET)
    *(--stack_ptr) = GDT_USER_DATA_SELECTOR;  // SS (user data segment, ring 3)
    *(--stack_ptr) = user_stack_top;          // ESP (user stack pointer)
    *(--stack_ptr) = 0x202;                   // EFLAGS (enable interrupts, bit 2 is always 1)
    *(--stack_ptr) = GDT_USER_CODE_SELECTOR;  // CS (user code segment, ring 3)
    *(--stack_ptr) = entry_point;             // EIP (entry point of the ELF)

    // Simulate the PUSHA-saved general purpose registers
    // (order: EDI, ESI, EBP, ESP (old), EBX, EDX, ECX, EAX)
    // The ESP pushed by PUSHA is the stack pointer *before* PUSHA.
    // We can put an arbitrary value here, it won't be used for iret.
    // But it makes the stack frame consistent.
    *(--stack_ptr) = 0; // EDI
    *(--stack_ptr) = 0; // ESI
    *(--stack_ptr) = 0; // EBP
    *(--stack_ptr) = 0; // ESP (value before PUSHA; dummy value)
    *(--stack_ptr) = 0; // EBX
    *(--stack_ptr) = 0; // EDX
    *(--stack_ptr) = 0; // ECX
    *(--stack_ptr) = 0; // EAX

    // Add a fake EFLAGS and return address for the context switch
    // When task_switch_asm does popf, it will restore these flags
    *(--stack_ptr) = 0x202; // EFLAGS (interrupts enabled)
    
    // This return address is where task_switch_asm will jump to after popa/popf/ret
    *(--stack_ptr) = (uint32)task_start_usermode_asm;

    // The task->kernel_stack should now point to this location
    // which is the top of the simulated stack frame for context switching.
    task->kernel_stack = (uint32)stack_ptr;
}


// Helper function to create a kernel task with a function pointer
static task_t* create_kernel_task(void (*entry_point)(void), const char* name) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    if (!new_task) {
        print_colored("[TASK] Failed to allocate memory for kernel task: ", 0x0C, 0x00);
        print(name);
        print("\n");
        return 0;
    }

    new_task->id = next_task_id++;
    new_task->state = TASK_STATE_READY;
    new_task->page_directory = vmm_get_current_page_directory(); // Use current kernel PD
    new_task->priority = 1; // Default priority
    new_task->ticks_left = 0; // Will be set by scheduler
    new_task->pending_signals = 0;
    new_task->next = 0;

    // No ELF info for kernel tasks
    memory_set((uint8*)&new_task->elf_info, 0, sizeof(elf_load_info_t));

    // Allocate kernel stack
    uint32 kernel_stack_vaddr = (uint32)kmalloc_aligned(KERNEL_STACK_SIZE, MEMORY_PAGE_SIZE);
    if (!kernel_stack_vaddr) {
        print_colored("[TASK] Failed to allocate kernel stack for task: ", 0x0C, 0x00);
        print(name);
        print("\n");
        kfree(new_task);
        return 0;
    }
    new_task->kernel_stack_base = kernel_stack_vaddr;
    
    // Set up kernel stack for entry point
    uint32* stack_ptr = (uint32*)(kernel_stack_vaddr + KERNEL_STACK_SIZE);
    
    // Push return address (should never return, but just in case)  
    *(--stack_ptr) = 0; // Return address (never used)
    *(--stack_ptr) = (uint32)entry_point;  // Entry point
    *(--stack_ptr) = 0; // EBP
    *(--stack_ptr) = 0; // EBX
    *(--stack_ptr) = 0; // ESI
    *(--stack_ptr) = 0; // EDI
    
    new_task->kernel_stack = (uint32)stack_ptr;

    print("[TASK] Created kernel task '");
    print(name);
    print("' with ID: ");
    print(int_to_string(new_task->id));
    print("\n");

    return new_task;
}

void tasks_init(void) {
    // Initialize the scheduler and create the initial task (kernel task)

    // Create the first task for the currently running kernel.
    // This task will be "current_task" from now on.
    task_t* kernel_task = (task_t*)kmalloc(sizeof(task_t));
    if (!kernel_task) {
        kernel_panic("Failed to allocate memory for kernel task");
    }

    kernel_task->id = next_task_id++;
    kernel_task->state = TASK_STATE_RUNNING;
    kernel_task->page_directory = vmm_get_current_page_directory(); // Use current kernel PD
    
    // The initial kernel stack is statically allocated. Its top address is passed here.
    // For the initial kernel task, we don't save/restore a full CPU state in the same way
    // as user tasks. Its context is the kernel itself.
    // This field will be updated by the first context switch *away* from the kernel_task.
    kernel_task->kernel_stack_base = (uint32)initial_kernel_stack;
    kernel_task->kernel_stack = (uint32)&initial_kernel_stack[KERNEL_STACK_SIZE];
    kernel_task->priority = 1;
    kernel_task->ticks_left = 0;
    kernel_task->pending_signals = 0;
    kernel_task->next = 0;
    gdt_set_kernel_stack(kernel_task->kernel_stack);

    // No ELF info for the kernel task itself
    memory_set((uint8*)&kernel_task->elf_info, 0, sizeof(elf_load_info_t));

    current_task = kernel_task;
    ready_queue_head = kernel_task; // Add kernel task to ready queue
    
    // Create the idle task
    idle_task = create_kernel_task(idle_task_function, "idle");
    if (!idle_task) {
        kernel_panic("Failed to create idle task");
    }
    
    // Add idle task to ready queue (circular list)
    kernel_task->next = idle_task;
    idle_task->next = kernel_task;
    
    print("[TASK] Initialized tasking system. Kernel task ID: ");
    print(int_to_string(current_task->id));
    print(", Idle task ID: ");
    print(int_to_string(idle_task->id));
    print("\n");
    
    // Debug: Print initial queue state
    debug_print_ready_queue();
}


task_t* task_create_elf(const uint8* elf_data, size_t elf_size, const char* name) {
    spinlock_acquire(&task_scheduler_lock);
    
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    if (!new_task) {
        print_colored("[TASK] Failed to allocate memory for new task: ", 0x0C, 0x00);
        print(name);
        print("\n");
        spinlock_release(&task_scheduler_lock);
        return 0;
    }

    new_task->id = next_task_id++;
    new_task->state = TASK_STATE_READY;
    new_task->priority = 1; // Default priority
    new_task->ticks_left = 0; // Will be set by scheduler
    new_task->next = 0; // Will be added to ready queue later

    // 1. Load ELF into a new page directory
    elf_load_info_t elf_info;
    int status = elf_load_executable(elf_data, elf_size, &elf_info);
    if (status != 0 || !elf_info.valid || elf_info.entry_point == 0) {
        print_colored("[TASK] Failed to load ELF for task: ", 0x0C, 0x00);
        print(name);
        print(" (status: ");
        print(int_to_string(status));
        print(")\n");
        kfree(new_task);
        spinlock_release(&task_scheduler_lock);
        return 0;
    }
    new_task->elf_info = elf_info;
    new_task->page_directory = (page_directory_t*)elf_info.page_directory;

    // 2. Allocate and map user stack for the new task
    // It is important to switch to the new page directory to map pages into it
    // because vmm_map_page maps into the *current* page directory.
    // However, elf_load_executable already switched to the new page directory when loading the ELF.
    // So, we just need to ensure the correct page directory is active.
    page_directory_t* current_pd_ptr = vmm_get_current_page_directory();
    vmm_switch_page_directory((page_directory_t*)elf_info.page_directory);
    for (int i = 0; i < USER_STACK_SIZE; i++) {
        uint32 p_addr = pmm_alloc_frame();
        if (!p_addr) {
            print_colored("[TASK] Failed to allocate user stack frame for task: ", 0x0C, 0x00);
            print(name);
            print("\n");
            // Need to clean up previously allocated frames and page directory
            vmm_switch_page_directory(current_pd_ptr);
            vmm_destroy_page_directory((page_directory_t*)elf_info.page_directory);
            kfree(new_task);
            spinlock_release(&task_scheduler_lock);
            return 0;
        }
        vmm_map_page((page_directory_t*)elf_info.page_directory,
                     USER_STACK_TOP - (i + 1) * MEMORY_PAGE_SIZE,
                     p_addr,
                     PAGE_USER | PAGE_WRITABLE);
    }
    vmm_switch_page_directory(current_pd_ptr);

    // 3. Allocate a kernel stack for the new task
    // We need 2 pages for the kernel stack (8KB)
    uint32 kernel_stack_vaddr = (uint32)kmalloc_aligned(KERNEL_STACK_SIZE, MEMORY_PAGE_SIZE); // Allocate 8KB aligned
    if (!kernel_stack_vaddr) {
        print_colored("[TASK] Failed to allocate kernel stack for task: ", 0x0C, 0x00);
        print(name);
        print("\n");
        // Need to clean up previously allocated frames (user stack) and page directory
        vmm_destroy_page_directory((page_directory_t*)elf_info.page_directory); // This should also free pmm frames for the user stack
        kfree(new_task);
        spinlock_release(&task_scheduler_lock);
        return 0;
    }
    new_task->kernel_stack_base = kernel_stack_vaddr; // Store the base address
    
    // The stack grows downwards, so the "top" is the highest address
    uint32 kernel_stack_top = kernel_stack_vaddr + KERNEL_STACK_SIZE;

    // 3. Set up initial CPU state on the task's kernel stack
    // Simulate an interrupt frame on the kernel stack that iret will use to jump to user mode
    // The kernel_stack field stores the actual ESP value to restore during context switch.
    // This ESP will point to the CPU state structure we are creating.
    // The user stack is set up by elf_load_executable, USER_STACK_TOP from shell_loader.c is 0xE0000000 - 4KB
    // For now, let's use a fixed user stack top, but this should ideally come from elf_info or a global constant.
// #define USER_STACK_TOP 0xE0000000 // Temporary, should match shell_loader.c logic - REMOVED

    setup_initial_cpu_state(new_task, elf_info.entry_point, USER_STACK_TOP, kernel_stack_top);


    // 4. Add the new task to the ready queue
    if (ready_queue_head == 0) {
        ready_queue_head = new_task;
        new_task->next = new_task; // Point to itself for a single-element circular list
    } else {
        // Find the tail of the circular list
        task_t* head = ready_queue_head;
        while (head->next != ready_queue_head) {
            head = head->next;
        }
        head->next = new_task;
        new_task->next = ready_queue_head;
    }
    
    print("[TASK] Created task ID: ");
    print(int_to_string(new_task->id));
    print(" (");
    print(name);
    print(") ELF entry: ");
    print(int_to_string(new_task->elf_info.entry_point));
    print(", kernel_stack: ");
    print(int_to_string(new_task->kernel_stack));
    print(", page_dir: ");
    print(int_to_string((uint32)new_task->page_directory));
    print("\n");
    
    // Skip debug information about entry point bytes for now as it may cause page faults
    // TODO: Add safe memory reading function for debug output

    spinlock_release(&task_scheduler_lock);
    return new_task;
}

// Defined in assembly (src/context_switch.asm)
extern void task_switch_asm(uint32* old_esp_ptr, uint32 new_esp_val, uint32 new_page_directory_phys);
extern void task_start_usermode_asm(void);

void task_switch(task_t* next_task) {
    if (!next_task) {
        print("[TASK] ERROR: Attempted to switch to null task\n");
        return;
    }

    if (!current_task || current_task == next_task) {
        return; // No switch needed or current_task is null (first switch)
    }

    if (!next_task->page_directory) {
        print("[TASK] ERROR: Next task has null page directory\n");
        return;
    }

    if (next_task->kernel_stack == 0) {
        print("[TASK] ERROR: Next task has invalid kernel stack\n");
        return;
    }

    task_t* prev_task = current_task;
    current_task = next_task;

    gdt_set_kernel_stack(next_task->kernel_stack_base + KERNEL_STACK_SIZE);

    // Call assembly to perform context switch
    // - Save prev_task's kernel ESP into &prev_task->kernel_stack
    // - Load current_task's kernel ESP from current_task->kernel_stack
    // - Load current_task's page directory (already physical address from vmm_create_page_directory)
    task_switch_asm(&prev_task->kernel_stack, current_task->kernel_stack, (uint32)current_task->page_directory);
}

// Helper function to validate the ready queue integrity
static bool validate_ready_queue(void) {
    if (!ready_queue_head) {
        return true; // Empty queue is valid
    }

    task_t* current = ready_queue_head;
    int count = 0;
    do {
        if (!current) {
            print("[TASK] ERROR: NULL pointer in ready queue\n");
            return false;
        }
        count++;
        if (count > 1000) { // Prevent infinite loops
            print("[TASK] ERROR: Ready queue appears to have infinite loop\n");
            return false;
        }
        current = current->next;
    } while (current != ready_queue_head);
    
    return true;
}

// Helper function to count valid runnable tasks
static int count_runnable_tasks(void) {
    if (!ready_queue_head) return 0;
    
    int count = 0;
    task_t* current = ready_queue_head;
    do {
        if (current && current->state == TASK_STATE_READY) {
            count++;
        }
        current = current->next;
    } while (current && current != ready_queue_head);
    
    return count;
}

// Debug function to print the current state of the ready queue
void debug_print_ready_queue(void) {
    print("[TASK] Ready queue state:\n");
    if (!ready_queue_head) {
        print("  Queue is empty\n");
        return;
    }
    
    task_t* current = ready_queue_head;
    int count = 0;
    do {
        if (!current) {
            print("  ERROR: NULL pointer in queue!\n");
            break;
        }
        
        print("  Task ");
        print(int_to_string(current->id));
        print(": state=");
        switch (current->state) {
            case TASK_STATE_RUNNING: print("RUNNING"); break;
            case TASK_STATE_READY: print("READY"); break;
            case TASK_STATE_WAITING: print("WAITING"); break;
            case TASK_STATE_TERMINATED: print("TERMINATED"); break;
            default: print("UNKNOWN"); break;
        }
        print(", next=");
        if (current->next) {
            print(int_to_string(current->next->id));
        } else {
            print("NULL");
        }
        print("\n");
        
        current = current->next;
        count++;
        if (count > 20) { // Prevent spam
            print("  ... (truncated after 20 tasks)\n");
            break;
        }
    } while (current && current != ready_queue_head);
    
    print("  Current task: ");
    if (current_task) {
        print(int_to_string(current_task->id));
    } else {
        print("NULL");
    }
    print("\n");
}

void task_schedule(void) {
    uint32 current_ticks = timer_get_ticks();
    spinlock_acquire(&task_scheduler_lock);

    // Wake up sleeping tasks
    task_t* t = ready_queue_head;
    if (t) {
        do {
            if (t->state == TASK_STATE_WAITING && t->sleep_until_tick > 0 && current_ticks >= t->sleep_until_tick) {
                t->state = TASK_STATE_READY;
                t->sleep_until_tick = 0;
            }
            t = t->next;
        } while (t != ready_queue_head);
    }
    
    // Validate queue integrity first
    if (!validate_ready_queue()) {
        spinlock_release(&task_scheduler_lock);
        kernel_panic("Ready queue corruption detected!");
    }

    // If the current task is marked for termination, destroy it.
    if (current_task && (current_task->pending_signals & SIGKILL)) {
        print("[TASK] Terminating current task ID: ");
        print(int_to_string(current_task->id));
        print("\n");
        
        // Save the next task before destroying current
        task_t* next_in_queue = current_task->next;
        
        // Special case: if this is the only task
        if (current_task->next == current_task) {
            ready_queue_head = NULL;
            current_task = NULL;
            kernel_panic("Last task terminated - no tasks remaining!");
        }
        
        task_destroy(current_task); 
        current_task = NULL;

        // If ready_queue_head became null, there are no more tasks.
        if (ready_queue_head == NULL) {
            kernel_panic("No more tasks to schedule after terminating a task!");
        }
        
        // Start from the next task
        current_task = next_in_queue;
    }

    if (!ready_queue_head) {
        print("[TASK] WARNING: No tasks in ready queue, creating idle task\n");
        // We need at least one task to run
        kernel_panic("No tasks available to schedule!");
    }

    // Find the next available task in the circular ready queue
    task_t* next_task = current_task; 
    if (!next_task) { 
        next_task = ready_queue_head;
    }

    task_t* initial_scan_start = next_task;
    int scan_count = 0; // Prevent infinite loops
    bool found_runnable = false;

    do {
        next_task = next_task->next;
        scan_count++;
        
        if (!next_task) {
            print("[TASK] ERROR: Null task found in ready queue at scan ");
            print(int_to_string(scan_count));
            print("\n");
            // Try to recover by starting from head
            next_task = ready_queue_head;
            if (!next_task) {
                kernel_panic("Ready queue head is NULL!");
            }
            break;
        }
        
        // Prevent infinite scanning
        if (scan_count > 1000) {
            print("[TASK] ERROR: Infinite loop detected in task scanning\n");
            kernel_panic("Task queue corruption - infinite loop");
        }
        
        // Check if this task is runnable
        if (next_task->state == TASK_STATE_READY || next_task->state == TASK_STATE_RUNNING) {
            found_runnable = true;
            break;
        }
        
    } while (next_task != initial_scan_start);

    // If no runnable task was found, fall back to idle task
    if (!found_runnable) {
        print("[TASK] WARNING: No runnable tasks found (");
        print(int_to_string(count_runnable_tasks()));
        print(" runnable), switching to idle task\n");
        
        // Use idle task as fallback
        if (idle_task && (idle_task->state == TASK_STATE_READY || idle_task->state == TASK_STATE_RUNNING)) {
            next_task = idle_task;
            next_task->state = TASK_STATE_RUNNING;
        } else if (ready_queue_head && (ready_queue_head->state == TASK_STATE_READY || ready_queue_head->state == TASK_STATE_RUNNING)) {
            // Final fallback to kernel task
            next_task = ready_queue_head;
            print("[TASK] Falling back to kernel task\n");
        } else {
            kernel_panic("No runnable tasks available including idle task!");
        }
    }

    // Update state of selected task
    if (next_task->state != TASK_STATE_RUNNING) {
        next_task->state = TASK_STATE_RUNNING;
    }
    
    // Release lock before context switch to avoid deadlock
    spinlock_release(&task_scheduler_lock);
    
    // Switch to the next task
    task_switch(next_task);
}


// Deallocates resources associated with a task and removes it from the ready queue
void task_destroy(task_t* task) {
    if (!task) {
        return;
    }

    print("[TASK] Destroying task ID: ");
    print(int_to_string(task->id));
    print("\n");

    // Remove from ready queue
    if (ready_queue_head == task) {
        if (task->next == task) { // Only one task in the queue
            ready_queue_head = 0;
        } else {
            task_t* current = ready_queue_head;
            while (current->next != ready_queue_head) {
                current = current->next;
            }
            ready_queue_head = task->next;
            current->next = ready_queue_head;
        }
    } else {
        task_t* current = ready_queue_head;
        // Loop until current->next is task or we've looped through the whole list
        while (current && current->next != task && current->next != ready_queue_head) {
            current = current->next;
        }
        if (current && current->next == task) {
            current->next = task->next;
        }
    }

    // Deallocate page directory and its mapped frames (user code, data, stack)
    vmm_destroy_page_directory(task->page_directory);

    // Deallocate kernel stack (kmalloc_a was used, so kfree it)
    kfree((void*)task->kernel_stack_base);

    // Free the task_t struct itself
    kfree(task);

    // If the destroyed task was the current task, a reschedule will be necessary.
    // This scenario should primarily be handled by the scheduler itself,
    // which would call task_destroy for a terminated task and then reschedule.
}

void task_kill(uint32 pid) {
    if (pid == 0) { // PID 0 is usually reserved for the idle task or invalid
        return;
    }

    if (current_task && current_task->id == pid) {
        // Cannot kill current task directly here. Mark it for termination.
        // The scheduler will pick this up.
        current_task->pending_signals |= SIGKILL;
        print("[TASK] Marked current task (ID: ");
        print(int_to_string(pid));
        print(") for SIGKILL.\n");
        return;
    }

    task_t* task_to_kill = 0;
    task_t* current = ready_queue_head;

    if (current) {
        do {
            if (current->id == pid) {
                task_to_kill = current;
                break;
            }
            current = current->next;
        } while (current != ready_queue_head);
    }

    if (task_to_kill) {
        task_to_kill->pending_signals |= SIGKILL;
        print("[TASK] Sent SIGKILL to task ID: ");
        print(int_to_string(pid));
        print("\n");
    } else {
        print("[TASK] No task found with ID: ");
        print(int_to_string(pid));
        print(" to kill.\n");
    }
}

void sleep_busy(uint32 microseconds) {
    uint32 start_ticks = timer_get_ticks();
    // Assuming 100Hz timer, so 1 tick = 10ms = 10000us
    uint32 ticks_to_wait = microseconds / 10000;
    if (microseconds % 10000 != 0) {
        ticks_to_wait++;
    }

    uint32 end_ticks = start_ticks + ticks_to_wait;
    while (timer_get_ticks() < end_ticks) {
        // Busy wait
        asm volatile("pause");
    }
}

void sleep_interruptible(uint32 milliseconds) {
    if (!current_task || milliseconds == 0) {
        return;
    }

    // Assuming 100Hz timer, so 1 tick = 10ms
    uint32 ticks_to_sleep = milliseconds / 10;
    if (ticks_to_sleep == 0) {
        ticks_to_sleep = 1;
    }

    uint32 current_ticks = timer_get_ticks();
    current_task->sleep_until_tick = current_ticks + ticks_to_sleep;
    current_task->state = TASK_STATE_WAITING;

    task_schedule();
}

void task_shutdown_all(void) {
    if (!ready_queue_head) {
        return;
    }

    while (true) {
        task_t* victim = 0;
        task_t* iter = ready_queue_head;
        if (!iter) {
            break;
        }

        do {
            if (iter->elf_info.entry_point != 0) {
                victim = iter;
                break;
            }
            iter = iter->next;
        } while (iter && iter != ready_queue_head);

        if (!victim) {
            break;
        }

        if (victim == current_task) {
            current_task = (victim->next != victim) ? victim->next : 0;
        }

        task_destroy(victim);
        if (!ready_queue_head) {
            break;
        }
    }
}
