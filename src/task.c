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
#include "include/ps2_mouse.h"
#include "include/mm.h"
#include "include/debuglog.h"
#include "include/auth.h"
#include <stddef.h>

// Architecture detection
#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

// Architecture-specific stack word type
#if ARCH_64BIT
typedef uint64 stack_word_t;
#define STACK_WORD_SIZE 8
#else
typedef uint32 stack_word_t;
#define STACK_WORD_SIZE 4
#endif

// Explicit forward declaration to help compiler resolve implicit declaration
extern page_directory_t* vmm_get_current_page_directory(void);
extern string long_to_string(long n);
extern void vmm_destroy_page_directory(page_directory_t* dir);  // For deferred cleanup

#define KERNEL_STACK_SIZE 8192 // 8KB for kernel stack per task
#define USER_STACK_SIZE 32   // 32 pages, 128KB for user stack (more suitable for GUI apps)
// USER_STACK_TOP is defined in memory.h

task_t* current_task = 0;
task_t* ready_queue_head = 0;
static task_t* idle_task = 0; // Idle task that runs when no other tasks are available
static task_t* foreground_task = 0; // GUI app that gets priority scheduling
static uint32 next_task_id = 1;

static spinlock_t task_scheduler_lock = SPINLOCK_INIT("task_scheduler");

// Deferred cleanup list for tasks destroyed while their context was active
// These tasks' page directories need to be cleaned up after we've switched away
#define MAX_DEFERRED_CLEANUP 16
static task_t* deferred_cleanup_tasks[MAX_DEFERRED_CLEANUP];
static uint32_t deferred_cleanup_count = 0;
static spinlock_t deferred_cleanup_lock = SPINLOCK_INIT("deferred_cleanup");

// Temporary stack for initial task setup
// This will be replaced by a proper kernel stack for each task
static uint8 initial_kernel_stack[KERNEL_STACK_SIZE] __attribute__((aligned(4096)));

// Idle task function - runs when no other tasks are available
static void idle_task_function(void) {
    for (;;) {
        // Use STI+HLT to halt but allow interrupts to wake CPU
        // This is safe because we're in kernel context with interrupts handled
        __asm__ __volatile__("sti; hlt");
    }
}

// Forward declarations for assembly functions (defined in context_switch.asm)
extern void task_switch_asm(uintptr_t* old_sp_ptr, uintptr_t new_sp_val, uintptr_t new_page_directory_phys);
extern void task_start_usermode_asm(void);  // Entry point for IRET to user mode
// Note: jump_to_usermode_asm is deprecated and will trap if called

// Sync kernel PDEs into a task's page directory (defined in vmm.c)
// Must be called before switching to a task to ensure all kernel heap pages
// allocated after the task was created are visible through its CR3.
extern void vmm_sync_kernel_pdes(page_directory_t* task_dir);

// Simple helpers for mapping/unmapping user pages for task-local regions
static bool task_map_user_pages(page_directory_t* dir, uint32 start, uint32 end, uint32 flags) {
    if (!dir || start >= end) {
        return false;
    }

    uint32 aligned_start = memory_align_down(start, MEMORY_PAGE_SIZE);
    uint32 aligned_end = memory_align_up(end, MEMORY_PAGE_SIZE);

    for (uint32 va = aligned_start; va < aligned_end; va += MEMORY_PAGE_SIZE) {
        uint32 frame = pmm_alloc_frame();
        if (!frame) {
            return false;
        }

        memory_result_t res = vmm_map_page(dir, va, frame, flags);
        if (res == MEMORY_ERROR_ALREADY_MAPPED) {
            pmm_free_frame(frame);
            continue;
        }

        if (res != MEMORY_OK) {
            pmm_free_frame(frame);
            return false;
        }
    }

    return true;
}

static void task_unmap_user_pages(page_directory_t* dir, uint32 start, uint32 end) {
    if (!dir || start >= end) {
        return;
    }

    uint32 aligned_start = memory_align_down(start, MEMORY_PAGE_SIZE);
    uint32 aligned_end = memory_align_up(end, MEMORY_PAGE_SIZE);

    for (uint32 va = aligned_start; va < aligned_end; va += MEMORY_PAGE_SIZE) {
        uint32 phys = vmm_get_physical_addr(dir, va);
        vmm_unmap_page(dir, va);
        if (phys) {
            pmm_free_frame(phys);
        }
    }
}

static void setup_initial_cpu_state(task_t* task,
                                    uintptr_t entry_point,
                                    uintptr_t user_stack_top,
                                    uintptr_t kernel_stack_top) {
    // Prepare the kernel stack so task_switch_asm can restore registers and
    // eventually drop to user mode via task_start_usermode_asm.
    //
    // Method A (OSDev Wiki): Build complete stack frame once, execute IRET.
    //
    // Flow: task_switch_asm() -> POPA, POPF, POP EBP, RET
    //       -> RET jumps to task_start_usermode_asm
    //       -> task_start_usermode_asm sets DS/ES/FS/GS, then IRET
    //       -> CPU in Ring 3 at ELF entry point!

    stack_word_t* stack_ptr = (stack_word_t*)kernel_stack_top;

    #if ARCH_64BIT
    // =========================================================================
    // 64-BIT LAYOUT (high address -> low address)
    // =========================================================================
    // Stack grows DOWN. We push from kernel_stack_top downward.
    //
    // After task_switch_asm does: pop r15..rax, popfq, pop rbp, ret
    //   -> RET jumps to task_start_usermode_asm
    // task_start_usermode_asm does: set segments, pop r15..rax, popfq, iretq
    //   -> IRETQ pops RIP, CS, RFLAGS, RSP, SS and drops to ring 3
    //
    // Layout:
    //   [IRETQ frame - 5 qwords for ring change]
    //   [Register frame for task_start_usermode_asm - 15 qwords + RFLAGS]
    //   [task_switch_asm frame: return addr, RBP, RFLAGS, 15 registers]
    // =========================================================================

    // === IRETQ frame (pushed first, ends up at highest addresses) ===
    // IRETQ pops in order: RIP, CS, RFLAGS, RSP, SS
    // So we push in reverse: SS, RSP, RFLAGS, CS, RIP
    *(--stack_ptr) = GDT_USER_DATA_SELECTOR;    // SS (user data segment, ring 3)
    *(--stack_ptr) = user_stack_top;            // RSP (user stack pointer)
    *(--stack_ptr) = 0x202;                     // RFLAGS (IF=1, reserved bit 1=1)
    *(--stack_ptr) = GDT_USER_CODE_SELECTOR;    // CS (user code segment, ring 3)
    *(--stack_ptr) = entry_point;               // RIP (entry point of ELF)

    // === Register frame for task_start_usermode_asm ===
    // task_start_usermode_asm pops: r15, r14, ..., r8, rdi, rsi, rbp, rbx, rdx, rcx, rax, then popfq
    *(--stack_ptr) = 0x202;  // RFLAGS for popfq in task_start_usermode_asm
    *(--stack_ptr) = 0;      // RAX
    *(--stack_ptr) = 0;      // RCX
    *(--stack_ptr) = 0;      // RDX
    *(--stack_ptr) = 0;      // RBX
    *(--stack_ptr) = 0;      // RBP
    *(--stack_ptr) = 0;      // RSI
    *(--stack_ptr) = 0;      // RDI
    *(--stack_ptr) = 0;      // R8
    *(--stack_ptr) = 0;      // R9
    *(--stack_ptr) = 0;      // R10
    *(--stack_ptr) = 0;      // R11
    *(--stack_ptr) = 0;      // R12
    *(--stack_ptr) = 0;      // R13
    *(--stack_ptr) = 0;      // R14
    *(--stack_ptr) = 0;      // R15

    // === Frame for task_switch_asm ===
    // task_switch_asm does: pop r15..rax, popfq, pop rbp, ret

    // Return address - RET from task_switch_asm jumps here
    *(--stack_ptr) = (stack_word_t)(uintptr_t)task_start_usermode_asm;

    // Saved RBP for 'pop rbp'
    *(--stack_ptr) = 0;

    // RFLAGS for 'popfq'
    *(--stack_ptr) = 0x202;

    // Register values for task_switch_asm's individual pops (reverse order)
    *(--stack_ptr) = 0;  // RAX
    *(--stack_ptr) = 0;  // RCX
    *(--stack_ptr) = 0;  // RDX
    *(--stack_ptr) = 0;  // RBX
    *(--stack_ptr) = 0;  // RBP
    *(--stack_ptr) = 0;  // RSI
    *(--stack_ptr) = 0;  // RDI
    *(--stack_ptr) = 0;  // R8
    *(--stack_ptr) = 0;  // R9
    *(--stack_ptr) = 0;  // R10
    *(--stack_ptr) = 0;  // R11
    *(--stack_ptr) = 0;  // R12
    *(--stack_ptr) = 0;  // R13
    *(--stack_ptr) = 0;  // R14
    *(--stack_ptr) = 0;  // R15

    #else
    // =========================================================================
    // 32-BIT LAYOUT (high address -> low address)
    // =========================================================================
    // Stack grows DOWN. We push from kernel_stack_top downward.
    //
    // After task_switch_asm does: popa, popf, pop ebp, ret
    //   -> RET jumps to task_start_usermode_asm
    // task_start_usermode_asm does: set DS/ES/FS/GS = 0x23, then iret
    //   -> IRET pops EIP, CS, EFLAGS, ESP, SS and drops to ring 3
    //
    // Stack layout (HIGH to LOW address):
    //   +----------------------------------+
    //   | SS = 0x23 (user data)            |  <- IRET frame (5 dwords)
    //   | ESP = user_stack_top             |
    //   | EFLAGS = 0x202                   |
    //   | CS = 0x1B (user code)            |
    //   | EIP = entry_point                |
    //   +----------------------------------+
    //   | return_addr = task_start_usermode_asm | <- for 'ret'
    //   | saved EBP = 0                    |      <- for 'pop ebp'
    //   | EFLAGS = 0x202                   |      <- for 'popf'
    //   | EAX = 0                          |  <- POPA frame (8 dwords)
    //   | ECX = 0                          |     for 'popa'
    //   | EDX = 0                          |
    //   | EBX = 0                          |
    //   | ESP = 0 (dummy, ignored)         |
    //   | EBP = 0                          |
    //   | ESI = 0                          |
    //   | EDI = 0                          |
    //   +----------------------------------+  <- task->kernel_stack points HERE
    // =========================================================================

    // === IRET frame (pushed first, ends up at highest addresses) ===
    // IRET pops in order: EIP, CS, EFLAGS, ESP, SS
    // So we push in reverse: SS, ESP, EFLAGS, CS, EIP
    *(--stack_ptr) = GDT_USER_DATA_SELECTOR;    // SS (user data segment, ring 3)
    *(--stack_ptr) = user_stack_top;            // ESP (user stack pointer)
    *(--stack_ptr) = 0x202;                     // EFLAGS (IF=1, reserved bit 1=1)
    *(--stack_ptr) = GDT_USER_CODE_SELECTOR;    // CS (user code segment, ring 3)
    *(--stack_ptr) = entry_point;               // EIP (entry point of ELF)

    // === Frame for task_switch_asm ===
    // task_switch_asm does: popa, popf, pop ebp, ret
    //
    // Stack layout (from HIGH to LOW address, i.e., order we push):
    //   [HIGH] return_addr -> for 'ret'
    //          saved_ebp   -> for 'pop ebp'
    //          eflags      -> for 'popf'
    //          8 GPRs      -> for 'popa' (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI)
    //   [LOW]  <- task->kernel_stack points here

    // Return address - where 'ret' from task_switch_asm will jump to
    *(--stack_ptr) = (stack_word_t)(uintptr_t)task_start_usermode_asm;

    // Saved EBP for 'pop ebp' - can be 0 since we don't care about frame pointer
    *(--stack_ptr) = 0;

    // EFLAGS for 'popf' (keep interrupts enabled during transition)
    *(--stack_ptr) = 0x202;

    // POPA frame - popa pops: EDI, ESI, EBP, (skip ESP), EBX, EDX, ECX, EAX
    // We push in reverse order of popa: EAX first (highest), EDI last (lowest)
    *(--stack_ptr) = 0;  // EAX
    *(--stack_ptr) = 0;  // ECX
    *(--stack_ptr) = 0;  // EDX
    *(--stack_ptr) = 0;  // EBX
    *(--stack_ptr) = 0;  // ESP (dummy, skipped by POPA)
    *(--stack_ptr) = 0;  // EBP
    *(--stack_ptr) = 0;  // ESI
    *(--stack_ptr) = 0;  // EDI

    #endif

    // task->kernel_stack points to where task_switch_asm will load ESP/RSP
    // This is the BOTTOM of the frame we just constructed
    task->kernel_stack = (uintptr_t)stack_ptr;

    // DEBUG: Verify the stack contents
    debuglog(DEBUG_INFO, "[STACK_SETUP] ESP will be: 0x%x\n", (uint32)(uintptr_t)stack_ptr);
    debuglog(DEBUG_INFO, "[STACK_SETUP] task_start_usermode_asm addr: 0x%x\n", (uint32)(uintptr_t)task_start_usermode_asm);
    debuglog(DEBUG_INFO, "[STACK_SETUP] Return addr at offset 40: 0x%x\n", *(uint32*)((uintptr_t)stack_ptr + 40));
                                    }

                                    static stack_word_t* prepare_kernel_task_stack(void (*entry_point)(void), stack_word_t* stack_top) {
                                        if (!stack_top) {
                                            return NULL;
                                        }

                                        stack_word_t* sp = stack_top;

                                        #if ARCH_64BIT
                                        // 64-bit: task_switch_asm does: individual pops (r15-r8, rdi-rax), popfq, pop rbp, ret
                                        // Stack layout from HIGH to LOW:
                                        //   Padding (keeps RSP % 16 == 8 at task entry, like a call would)
                                        //   Return address (entry_point)
                                        //   Saved RBP (for pop rbp)
                                        //   RFLAGS (for popfq)
                                        //   RAX, RCX, RDX, RBX, RBP, RSI, RDI, R8-R15

                                        // ABI alignment padding so the task starts with RSP % 16 == 8
                                        *(--sp) = 0;

                                        // Return address for the final RET in task_switch_asm
                                        *(--sp) = (stack_word_t)(uintptr_t)entry_point;

                                        // Saved RBP for 'pop rbp'
                                        *(--sp) = 0;

                                        // RFLAGS to be restored by POPFQ (keep IF set)
                                        *(--sp) = 0x202;

                                        // Values for general purpose registers (pushed in reverse pop order)
                                        *(--sp) = 0; // RAX
                                        *(--sp) = 0; // RCX
                                        *(--sp) = 0; // RDX
                                        *(--sp) = 0; // RBX
                                        *(--sp) = 0; // RBP
                                        *(--sp) = 0; // RSI
                                        *(--sp) = 0; // RDI
                                        *(--sp) = 0; // R8
                                        *(--sp) = 0; // R9
                                        *(--sp) = 0; // R10
                                        *(--sp) = 0; // R11
                                        *(--sp) = 0; // R12
                                        *(--sp) = 0; // R13
                                        *(--sp) = 0; // R14
                                        *(--sp) = 0; // R15
                                        #else
                                        // 32-bit: task_switch_asm does: popa, popf, pop ebp, ret
                                        // Stack layout from HIGH to LOW:
                                        //   Return address (entry_point)
                                        //   Saved EBP (for pop ebp)
                                        //   EFLAGS (for popf)
                                        //   EAX, ECX, EDX, EBX, ESP(dummy), EBP, ESI, EDI (for popa)

                                        // Return address for the final RET in task_switch_asm
                                        *(--sp) = (stack_word_t)(uintptr_t)entry_point;

                                        // Saved EBP for 'pop ebp'
                                        *(--sp) = 0;

                                        // EFLAGS to be restored by POPF (keep IF set)
                                        *(--sp) = 0x202;

                                        // Values consumed by POPA (push in reverse order so that the first POPA writes EDI)
                                        *(--sp) = 0; // EAX
                                        *(--sp) = 0; // ECX
                                        *(--sp) = 0; // EDX
                                        *(--sp) = 0; // EBX
                                        *(--sp) = 0; // Dummy ESP
                                        *(--sp) = 0; // EBP
                                        *(--sp) = 0; // ESI
                                        *(--sp) = 0; // EDI
                                        #endif

                                        return sp;
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

                                        memory_set((uint8*)new_task, 0, sizeof(task_t));
                                        strncpy(new_task->name, name, 31);
                                        new_task->name[31] = '\0';
                                        new_task->id = next_task_id++;
                                        new_task->pgrp = new_task->id;  // Default to own PID as process group
                                        new_task->session = new_task->id;  // Default to own PID as session
                                        new_task->tty_fd = -1;  // No controlling terminal by default
                                        new_task->state = TASK_STATE_READY;
                                        new_task->page_directory = vmm_get_current_page_directory(); // Use current kernel PD
                                        new_task->priority = 1; // Default priority
                                        new_task->ticks_left = 0; // Will be set by scheduler
                                        new_task->pending_signals = 0;
                                        new_task->last_active_tick = 0;
                                        new_task->next = 0;
                                        new_task->exit_code = 0;
                                        memory_set((uint8*)new_task->exit_reason, 0, sizeof(new_task->exit_reason));
                                        new_task->exit_reason[0] = '\0';
                                        new_task->uid = 0;
                                        new_task->gid = 0;
                                        new_task->groups_mask = 1; // root group bit
                                        new_task->user_heap_base = 0;
                                        new_task->user_heap_limit = 0;
                                        new_task->user_brk = 0;

                                        // No ELF info for kernel tasks
                                        memory_set((uint8*)&new_task->elf_info, 0, sizeof(elf_load_info_t));

                                        // Allocate kernel stack
                                        uintptr_t kernel_stack_vaddr = (uintptr_t)kmalloc_aligned(KERNEL_STACK_SIZE, MEMORY_PAGE_SIZE);
                                        if (!kernel_stack_vaddr) {
                                            print_colored("[TASK] Failed to allocate kernel stack for task: ", 0x0C, 0x00);
                                            print(name);
                                            print("\n");
                                            kfree(new_task);
                                            return 0;
                                        }
                                        new_task->kernel_stack_base = kernel_stack_vaddr;

                                        // Set up kernel stack for entry point
                                        stack_word_t* stack_ptr = (stack_word_t*)(kernel_stack_vaddr + KERNEL_STACK_SIZE);
                                        stack_ptr = prepare_kernel_task_stack(entry_point, stack_ptr);
                                        if (!stack_ptr) {
                                            kfree((void*)kernel_stack_vaddr);
                                            kfree(new_task);
                                            return 0;
                                        }

                                        new_task->kernel_stack = (uintptr_t)stack_ptr;

                                        print("[TASK] Created kernel task '");
                                        print(name);
                                        print("' with ID: ");
                                        print(int_to_string(new_task->id));
                                        print("\n");

                                        print("[TASK] About to initialize task queue...\n");

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

                                        memory_set((uint8*)kernel_task, 0, sizeof(task_t));
                                        strncpy(kernel_task->name, "kernel", 31);
                                        kernel_task->name[31] = '\0';
                                        kernel_task->id = next_task_id++;
                                        kernel_task->state = TASK_STATE_RUNNING;

                                        kernel_task->page_directory = vmm_get_current_page_directory(); // Use current kernel PD

                                        // The initial kernel stack is statically allocated. Its top address is passed here.
                                        // For the initial kernel task, we don't save/restore a full CPU state in the same way
                                        // as user tasks. Its context is the kernel itself.
                                        // This field will be updated by the first context switch *away* from the kernel_task.
                                        print("[TASK] Accessing initial_kernel_stack...\n");
                                        kernel_task->kernel_stack_base = (uintptr_t)initial_kernel_stack;
                                        kernel_task->kernel_stack = (uintptr_t)&initial_kernel_stack[KERNEL_STACK_SIZE];
                                        kernel_task->priority = 1;
                                        kernel_task->ticks_left = 0;
                                        kernel_task->pending_signals = 0;
                                        kernel_task->last_active_tick = 0;
                                        kernel_task->next = 0;
                                        kernel_task->exit_code = 0;
                                        memory_set((uint8*)kernel_task->exit_reason, 0, sizeof(kernel_task->exit_reason));
                                        kernel_task->uid = 0;
                                        kernel_task->gid = 0;
                                        kernel_task->groups_mask = 1;
                                        kernel_task->user_heap_base = 0;
                                        kernel_task->user_heap_limit = 0;
                                        kernel_task->user_brk = 0;
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
                                        // Keep idle out of the runnable pool unless explicitly needed
                                        idle_task->state = TASK_STATE_WAITING;

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
                                            debuglog(DEBUG_ERROR, "[TASK] kmalloc failed while creating task '%s'\n", name ? name : "(null)");
                                            print_colored("[TASK] Failed to allocate memory for new task: ", 0x0C, 0x00);
                                            print(name);
                                            print("\n");
                                            spinlock_release(&task_scheduler_lock);
                                            return 0;
                                        }

                                        memory_set((uint8*)new_task, 0, sizeof(task_t));
                                        strncpy(new_task->name, name, 31);
                                        new_task->name[31] = '\0';
                                        new_task->id = next_task_id++;
                                        new_task->pgrp = new_task->id;  // Default to own PID as process group
                                        new_task->session = new_task->id;  // Default to own PID as session
                                        new_task->tty_fd = -1;  // No controlling terminal by default
                                        new_task->state = TASK_STATE_READY;
                                        new_task->priority = 1; // Default priority
                                        new_task->ticks_left = 0; // Will be set by scheduler
                                        new_task->next = 0; // Will be added to ready queue later
                                        new_task->exit_code = 0;
                                        memory_set((uint8*)new_task->exit_reason, 0, sizeof(new_task->exit_reason));
                                        new_task->uid = auth_active_uid();
                                        new_task->gid = auth_active_gid();
                                        new_task->groups_mask = auth_active_groups_mask();
                                        new_task->last_active_tick = 0;
                                        new_task->user_heap_base = 0;
                                        new_task->user_heap_limit = 0;
                                        new_task->user_brk = 0;
                                        uint32 heap_base = 0;
                                        uint32 heap_limit = 0;
                                        uint32 initial_heap_end = 0;
                                        bool heap_mapped = false;

                                        // 1. Load ELF into a new page directory
                                        debuglog(DEBUG_INFO, "[TASK] About to call elf_load_executable\n");
                                        elf_load_info_t elf_info;
                                        int status = elf_load_executable(elf_data, elf_size, &elf_info);
                                        debuglog(DEBUG_INFO, "[TASK] After elf_load_executable, status=%d\n", status);
                                        if (status != 0 || !elf_info.valid || elf_info.entry_point == 0) {
                                            debuglog(DEBUG_ERROR, "[TASK] elf_load_executable failed for '%s' (status=%d, valid=%u, entry=0x%x)\n",
                                                     name ? name : "(null)", status, elf_info.valid, elf_info.entry_point);
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
                                        debuglog(DEBUG_INFO, "[TASK] Set page_directory to %p\n", (void*)elf_info.page_directory);

                                        // Sync kernel PDEs to ensure task has access to all kernel resources
                                        vmm_sync_kernel_pdes(new_task->page_directory);

                                        // 2. Allocate and map user stack for the new task
                                        // vmm_map_page takes the target directory as a parameter, so we don't need to
                                        // switch page directories. The kernel stays in its own address space.
                                        page_directory_t* task_pd = (page_directory_t*)elf_info.page_directory;
                                        uint32 stack_frames[USER_STACK_SIZE];
                                        int stack_pages_mapped = 0;

                                        debuglog(DEBUG_INFO, "[TASK] Allocating user stack for '%s' (%d pages), pd=0x%x\n", name ? name : "(null)", USER_STACK_SIZE, (uint32)task_pd);

                                        for (int i = 0; i < USER_STACK_SIZE; i++) {
                                            uint32 p_addr = pmm_alloc_frame();
                                            if (!p_addr) {
                                                debuglog(DEBUG_ERROR, "[TASK] User stack frame allocation failed for '%s' at page %d\n",
                                                         name ? name : "(null)", i);
                                                print_colored("[TASK] Failed to allocate user stack frame for task: ", 0x0C, 0x00);
                                                print(name);
                                                print("\n");
                                                goto stack_fail;
                                            }

                                            uint32 stack_va = USER_STACK_TOP - (i + 1) * MEMORY_PAGE_SIZE;
                                            memory_result_t map_res = vmm_map_page(task_pd, stack_va, p_addr,
                                                                                   PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
                                            if (map_res != MEMORY_OK && map_res != MEMORY_ERROR_ALREADY_MAPPED) {
                                                debuglog(DEBUG_ERROR, "[TASK] Failed to map user stack page %d for '%s' (res=%d, va=0x%x)\n",
                                                         i, name ? name : "(null)", map_res, stack_va);
                                                print_colored("[TASK] Failed to map user stack page for task: ", 0x0C, 0x00);
                                                print(name);
                                                print("\n");
                                                pmm_free_frame(p_addr);
                                                goto stack_fail;
                                            }

                                            // Zero the stack frame - using temporary mapping to access the physical address
                                            // Since PMM may allocate frames above identity mapping limit, we need proper mapping
                                            // For now, skip zeroing as the pages will be zero when allocated from PMM anyway
                                            debuglog(DEBUG_INFO, "[TASK] vmm_zero_phys skipped - PMM should return zeroed pages\n");

                                            stack_frames[stack_pages_mapped++] = p_addr;
                                        }
                                        debuglog(DEBUG_INFO, "[TASK] User stack mapped successfully\n");

                                        // Establish a per-task heap just below the user stack with a small guard.
                                        uint32 stack_base = USER_STACK_TOP - (USER_STACK_SIZE * MEMORY_PAGE_SIZE);
                                        uint32 guard_bytes = USER_HEAP_GUARD_PAGES * MEMORY_PAGE_SIZE;
                                        heap_base = memory_align_up(elf_info.base_address + elf_info.total_size, MEMORY_PAGE_SIZE);
                                        if (heap_base < MEMORY_USER_START) {
                                            heap_base = MEMORY_USER_START;
                                        }
                                        heap_limit = (stack_base > guard_bytes) ? (stack_base - guard_bytes) : stack_base;
                                        if (heap_base >= heap_limit) {
                                            debuglog(DEBUG_ERROR, "[TASK] Heap range overlaps stack for '%s' (heap_base=0x%x, limit=0x%x)\n",
                                                     name ? name : "(null)", heap_base, heap_limit);
                                            print_colored("[TASK] Failed to reserve heap range for task\n", 0x0C, 0x00);
                                            goto stack_fail;
                                        }

                                        initial_heap_end = heap_base + MEMORY_PAGE_SIZE;
                                        if (initial_heap_end > heap_limit) {
                                            initial_heap_end = heap_limit;
                                        }

                                        if (!task_map_user_pages(task_pd,
                                            heap_base,
                                            initial_heap_end,
                                            PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE)) {
                                            debuglog(DEBUG_ERROR, "[TASK] Failed to map initial heap page for '%s'\n", name ? name : "(null)");
                                        print_colored("[TASK] Failed to map initial heap page\n", 0x0C, 0x00);
                                        goto stack_fail;
                                            }
                                            heap_mapped = true;
                                            debuglog(DEBUG_INFO, "[TASK] User heap mapped: 0x%x - 0x%x\n", heap_base, initial_heap_end);

                                            // No need to switch page directories - we stayed in kernel space
                                            new_task->user_heap_base = heap_base;
                                            new_task->user_heap_limit = heap_limit;
                                            new_task->user_brk = heap_base;

                                            // 3. Allocate a kernel stack for the new task
                                            // We need 2 pages for the kernel stack (8KB)
                                            uintptr_t kernel_stack_vaddr = (uintptr_t)kmalloc_aligned(KERNEL_STACK_SIZE, MEMORY_PAGE_SIZE); // Allocate 8KB aligned
                                            if (!kernel_stack_vaddr) {
                                                debuglog(DEBUG_ERROR, "[TASK] Kernel stack allocation failed for '%s'\n", name ? name : "(null)");
                                                print_colored("[TASK] Failed to allocate kernel stack for task: ", 0x0C, 0x00);
                                                print(name);
                                                print("\n");
                                                goto stack_fail;
                                            }
                                            new_task->kernel_stack_base = kernel_stack_vaddr; // Store the base address

                                            // The stack grows downwards, so the "top" is the highest address
                                            uintptr_t kernel_stack_top = kernel_stack_vaddr + KERNEL_STACK_SIZE;

                                            // 3. Set up for initial user-mode entry
                                            // Build the complete kernel stack frame so task_switch_asm can:
                                            //   popa, popf, pop ebp, ret -> jumps to task_start_usermode_asm
                                            //   task_start_usermode_asm does IRET -> drops to ring 3
                                            //
                                            // This pre-built frame includes:
                                            //   - IRET frame (SS, ESP, EFLAGS, CS, EIP for user mode)
                                            //   - task_switch_asm frame (return addr, EBP, EFLAGS, 8 GPRs for POPA)
                                            setup_initial_cpu_state(new_task,
                                                                    elf_info.entry_point,
                                                                    USER_STACK_TOP,
                                                                    kernel_stack_top);

                                            debuglog(DEBUG_INFO, "[TASK] setup_initial_cpu_state done: kernel_stack=0x%x\n", (uint32)new_task->kernel_stack);

                                            // Store user mode info for debugging purposes
                                            new_task->needs_usermode_entry = true;
                                            new_task->usermode_entry_point = elf_info.entry_point;
                                            new_task->usermode_stack_top = USER_STACK_TOP;

                                            debuglog(DEBUG_INFO, "[TASK] ELF entry point: 0x%x\n", elf_info.entry_point);


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

                                            debuglog(DEBUG_INFO, "[TASK] Created task ID: %u (%s) ELF entry: 0x%x, kernel_stack: 0x%x, page_dir: 0x%x\n",
                                                     new_task->id, name, new_task->elf_info.entry_point,
                                                     new_task->kernel_stack, (uint32)new_task->page_directory);

                                            // Skip debug information about entry point bytes for now as it may cause page faults
                                            // TODO: Add safe memory reading function for debug output

                                            spinlock_release(&task_scheduler_lock);
                                            return new_task;

                                            stack_fail:
                                            // No need to switch page directories - we stayed in kernel space
                                            // Clean up allocated resources
                                            if (heap_mapped) {
                                                task_unmap_user_pages(task_pd, heap_base, initial_heap_end);
                                            }
                                            vmm_destroy_page_directory(task_pd);
                                            for (int j = 0; j < stack_pages_mapped; j++) {
                                                pmm_free_frame(stack_frames[j]);
                                            }
                                            kfree(new_task);
                                            spinlock_release(&task_scheduler_lock);
                                            return 0;
                                    }

                                    // Create a kernel-level task that runs a function in kernel space
                                    task_t* task_create_kernel(void (*entry_point)(void), const char* name, uint32 stack_size) {
                                        if (!entry_point || !name) {
                                            return 0;
                                        }

                                        spinlock_acquire(&task_scheduler_lock);

                                        task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
                                        if (!new_task) {
                                            debuglog(DEBUG_ERROR, "[TASK] kmalloc failed while creating kernel task '%s'\n", name);
                                            spinlock_release(&task_scheduler_lock);
                                            return 0;
                                        }

                                        // Initialize task structure
                                        memory_set((uint8*)new_task, 0, sizeof(task_t));
                                        strncpy(new_task->name, name, 31);
                                        new_task->name[31] = '\0';
                                        new_task->id = next_task_id++;
                                        new_task->pgrp = new_task->id;  // Default to own PID as process group
                                        new_task->session = new_task->id;  // Default to own PID as session
                                        new_task->tty_fd = -1;  // No controlling terminal by default
                                        new_task->state = TASK_STATE_READY;
                                        new_task->priority = 1;
                                        new_task->ticks_left = 0;
                                        new_task->exit_code = 0;
                                        memory_set((uint8*)new_task->exit_reason, 0, sizeof(new_task->exit_reason));
                                        new_task->uid = 0; // Kernel UID
                                        new_task->gid = 0; // Kernel GID
                                        new_task->groups_mask = 0;
                                        new_task->last_active_tick = 0;

                                        // Use kernel page directory
                                        new_task->page_directory = vmm_get_current_page_directory();

                                        // Allocate kernel stack
                                        if (stack_size == 0) {
                                            stack_size = KERNEL_STACK_SIZE;
                                        }
                                        void* kernel_stack = kmalloc_aligned(stack_size, MEMORY_PAGE_SIZE);
                                        if (!kernel_stack) {
                                            debuglog(DEBUG_ERROR, "[TASK] Failed to allocate kernel stack for task '%s'\n", name);
                                            kfree(new_task);
                                            spinlock_release(&task_scheduler_lock);
                                            return 0;
                                        }

                                        new_task->kernel_stack_base = (uintptr_t)kernel_stack;

                                        // Set up kernel stack for entry point - CRITICAL: must call prepare_kernel_task_stack!
                                        // The stack frame must be properly prepared for task_switch_asm to work correctly.
                                        stack_word_t* stack_top = (stack_word_t*)((uintptr_t)kernel_stack + stack_size);
                                        stack_word_t* stack_ptr = prepare_kernel_task_stack(entry_point, stack_top);
                                        if (!stack_ptr) {
                                            debuglog(DEBUG_ERROR, "[TASK] Failed to prepare kernel stack for task '%s'\n", name);
                                            kfree(kernel_stack);
                                            kfree(new_task);
                                            spinlock_release(&task_scheduler_lock);
                                            return 0;
                                        }

                                        new_task->kernel_stack = (uintptr_t)stack_ptr;

                                        // Store entry point info for debugging purposes
                                        new_task->elf_info.entry_point = (uint32)(uintptr_t)entry_point;
                                        new_task->elf_info.valid = true;

                                        // Add to ready queue
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

                                        print("[TASK] Created kernel task ID: ");
                                        print(int_to_string(new_task->id));
                                        print(" (");
                                        print(name);
                                        print(") entry: 0x");
                                        print_hex((uint32)entry_point);
                                        print("\n");

                                        spinlock_release(&task_scheduler_lock);
                                        return new_task;
                                    }

                                    /**
                                     * Clone the current task (for fork syscall)
                                     * Creates a child process that is a copy of the current process
                                     */
                                    task_t* task_clone_current(void) {
                                        spinlock_acquire(&task_scheduler_lock);

                                        if (!current_task) {
                                            spinlock_release(&task_scheduler_lock);
                                            return NULL;
                                        }

                                        // Allocate new task structure
                                        task_t* child_task = (task_t*)kmalloc(sizeof(task_t));
                                        if (!child_task) {
                                            debuglog(DEBUG_ERROR, "[TASK] kmalloc failed while cloning task '%s'\n",
                                                     current_task->name);
                                            spinlock_release(&task_scheduler_lock);
                                            return NULL;
                                        }

                                        // Copy task structure from parent
                                        memory_copy((const char*)child_task, (char*)current_task, sizeof(task_t));

                                        // Set child-specific fields
                                        child_task->id = next_task_id++;
                                        child_task->state = TASK_STATE_READY;
                                        child_task->next = NULL; // Will be added to ready queue

                                        // Copy name with " (child)" suffix
                                        char child_name[32];
                                        memory_copy(child_name, current_task->name, 32);
                                        strncat(child_name, " (child)", 32 - strlen(child_name) - 1);
                                        strncpy(child_task->name, child_name, 31);
                                        child_task->name[31] = '\0';

                                        // Allocate new kernel stack for the child
                                        void* kernel_stack = kmalloc_aligned(KERNEL_STACK_SIZE, MEMORY_PAGE_SIZE);
                                        if (!kernel_stack) {
                                            debuglog(DEBUG_ERROR, "[TASK] Failed to allocate kernel stack for cloned task '%s'\n",
                                                     child_task->name);
                                            kfree(child_task);
                                            spinlock_release(&task_scheduler_lock);
                                            return NULL;
                                        }

                                        child_task->kernel_stack_base = (uintptr_t)kernel_stack;

                                        // Copy the current task's kernel stack
                                        memory_copy(kernel_stack, (void*)current_task->kernel_stack_base, KERNEL_STACK_SIZE);
                                        child_task->kernel_stack = (uintptr_t)kernel_stack +
                                        (current_task->kernel_stack - current_task->kernel_stack_base);

                                        // TODO: Implement full COW memory copying
                                        // For now, share the parent's page directory (not correct, but allows basic fork to work)
                                        child_task->page_directory = current_task->page_directory;

                                        // Reset child-specific fields
                                        child_task->exit_code = 0;
                                        memory_set((uint8*)child_task->exit_reason, 0, sizeof(child_task->exit_reason));
                                        child_task->pending_signals = 0;
                                        child_task->sleep_until_tick = 0;
                                        child_task->last_active_tick = 0;
                                        child_task->needs_usermode_entry = false; // Child starts as if already in usermode

                                        // For fork: Set up child to return 0 from syscall
                                        // The syscall return value is stored in the EAX register on the stack
                                        // We need to modify the child's kernel stack so fork() returns 0
                                        if (child_task->kernel_stack && child_task->kernel_stack_base) {
                                            // The syscall frame is on the kernel stack. We need to modify the saved EAX
                                            // This is architecture-specific and depends on the syscall stub layout
                                            // For now, we'll set a flag that the syscall handler can check
                                            child_task->exit_code = FORK_CHILD_RETURN; // Special marker for child process
                                        }

                                        // Add child to ready queue
                                        if (ready_queue_head == 0) {
                                            ready_queue_head = child_task;
                                            child_task->next = child_task; // Point to itself for a single-element circular list
                                        } else {
                                            // Find the tail of the circular list
                                            task_t* head = ready_queue_head;
                                            while (head->next != ready_queue_head) {
                                                head = head->next;
                                            }
                                            head->next = child_task;
                                            child_task->next = ready_queue_head;
                                        }

                                        debuglog(DEBUG_INFO, "[TASK] Cloned task '%s' (PID %u) -> child '%s' (PID %u)\n",
                                                 current_task->name, current_task->id, child_task->name, child_task->id);

                                        spinlock_release(&task_scheduler_lock);
                                        return child_task;
                                    }

                                    void task_switch(task_t* next_task) {
                                        debuglog(DEBUG_INFO, "[TASK] task_switch ENTRY: next_task=0x%x\n", (uint32)next_task);
                                        if (!next_task) {
                                            print("[TASK] ERROR: Attempted to switch to null task\n");
                                            return;
                                        }

                                        debuglog(DEBUG_INFO, "[TASK] current_task=0x%x\n", (uint32)current_task);
                                        if (!current_task || current_task == next_task) {
                                            debuglog(DEBUG_INFO, "[TASK] task_switch early return: no switch needed\n");
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

                                        debuglog(DEBUG_INFO, "[TASK] Switching: cur_pd=0x%x, next_pd=0x%x, next_kstack=0x%x\n",
                                                 (uint32)current_task->page_directory, (uint32)next_task->page_directory,
                                                 (uint32)next_task->kernel_stack);

                                        task_t* prev_task = current_task;
                                        current_task = next_task;

                                        // Debug: log a few initial context switches to confirm user tasks run.
                                        // NOTE: This runs BEFORE cli — it is safe to call print() here because
                                        // interrupts are still enabled. Do not move any print() calls after the
                                        // cli below; framebuffer TTY code must not run with IRQs disabled.
                                        static int switch_debug_budget = 8;
                                        if (switch_debug_budget > 0) {
                                            print("[TASK] Switch: ");
                                            print(int_to_string(prev_task ? prev_task->id : 0));
                                            print(" -> ");
                                            print(int_to_string(next_task->id));
                                            print(" (state=");
                                            switch (next_task->state) {
                                                case TASK_STATE_RUNNING: print("RUNNING"); break;
                                                case TASK_STATE_READY: print("READY"); break;
                                                case TASK_STATE_WAITING: print("WAITING"); break;
                                                case TASK_STATE_TERMINATED: print("TERMINATED"); break;
                                                default: print("UNKNOWN"); break;
                                            }
                                            print(")\n");
                                            switch_debug_budget--;
                                        }

                                        if (next_task->needs_usermode_entry) {
                                            print("[TASK] First usermode entry: entry=0x");
                                            print_hex((uint32_t)next_task->usermode_entry_point);
                                            print(" stack=0x");
                                            print_hex((uint32_t)next_task->usermode_stack_top);
                                            print("\n");
                                            next_task->needs_usermode_entry = false;
                                        }

                                        gdt_set_kernel_stack(next_task->kernel_stack_base + KERNEL_STACK_SIZE);

                                        // Sync any kernel PDEs that were created after this task's page directory
                                        // was originally built (e.g. new kernel heap page tables).  The new task's
                                        // PD is a shallow copy — it shares existing page tables but misses any PDE
                                        // that was added later.  This one-pass sync makes those visible.
                                        // Must happen before cli so any kmalloc inside vmm_sync_kernel_pdes is safe.
                                        if (next_task->page_directory != vmm_get_current_page_directory()) {
                                            vmm_sync_kernel_pdes(next_task->page_directory);
                                        }

                                        // CRITICAL: Disable interrupts NOW — immediately before the context switch.
                                        // A timer interrupt between here and task_switch_asm would re-enter the
                                        // scheduler with current_task already updated, corrupting state.
                                        // Interrupts are re-enabled by POPF inside task_switch_asm, which restores
                                        // the saved EFLAGS (IF=1) from the new task's kernel stack frame.
                                        __asm__ __volatile__("cli");

                                        // Perform the actual context switch
                                        // - Save prev_task's kernel ESP into &prev_task->kernel_stack
                                        // - Load next_task's kernel ESP from next_task->kernel_stack
                                        // - Switch to next_task->page_directory
                                        // - Restore registers and return (which for new tasks jumps to task_start_usermode_asm)

                                        // Debug: Log PDE info for the new task's kernel stack.
                                        // IMPORTANT: pde->frame is a *physical* frame number. Do NOT dereference
                                        // (pde->frame << PAGE_SHIFT) as a virtual pointer — once paging is on,
                                        // physical addresses are only accessible via their mapped virtual aliases.
                                        // Dereferencing a physical address directly will page-fault if that frame
                                        // is not identity-mapped (frames above 0x08000000 are not in Forest OS).
                                        {
                                            page_directory_t* new_pd = next_task->page_directory;
                                            uint32 kstack_va = next_task->kernel_stack;
                                            uint32 pd_idx = kstack_va / MEMORY_PAGE_SIZE / 1024;
                                            uint32 pt_idx = (kstack_va / MEMORY_PAGE_SIZE) % 1024;
                                            page_entry_t* pde = &(*new_pd)[pd_idx];
                                            debuglog(DEBUG_INFO, "[TASK] new_pd[0x%x] (PDE %u): present=%d, frame=0x%x\n",
                                                     pd_idx, pd_idx, pde->present, pde->frame);
                                            if (pde->present) {
                                                /* Only log the physical address — do NOT read through it as a virt ptr */
                                                debuglog(DEBUG_INFO, "[TASK] PT phys=0x%x PTE idx=%u (not dereferenced — phys addr)\n",
                                                         pde->frame << MEMORY_PAGE_SHIFT, pt_idx);
                                            }
                                        }

                                        // NOTE: page_directory_t* in Forest OS IS the physical address
                                        // (vmm_create_page_directory returns phys frame ptr directly).
                                        // There is no vmm_pdir_phys() — the pointer IS the CR3 value.
                                        uint32 next_cr3 = (uint32)next_task->page_directory;
                                        debuglog(DEBUG_INFO, "[TASK] Before task_switch_asm: prev->kstack=0x%x, next->kstack=0x%x, next->cr3=0x%x\n",
                                                 (uint32)&prev_task->kernel_stack, (uint32)next_task->kernel_stack, next_cr3);
                                        task_switch_asm(&prev_task->kernel_stack,
                                                        next_task->kernel_stack,
                                                        next_cr3);

                                        // Note: For returning tasks, we reach here after they're switched back.
                                        // For new usermode tasks, we never return here - IRET jumps to userspace.
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

                                    bool task_exists(uint32 pid) {
                                        if (!ready_queue_head) {
                                            return false;
                                        }
                                        task_t* current = ready_queue_head;
                                        do {
                                            if (current && current->id == pid) {
                                                return true;
                                            }
                                            current = current->next;
                                        } while (current && current != ready_queue_head);
                                        return false;
                                    }

                                    int32 task_get_exit_code(uint32 pid) {
                                        task_t* task = NULL;
                                        task_t* current = ready_queue_head;
                                        if (current) {
                                            do {
                                                if (current->id == pid) {
                                                    task = current;
                                                    break;
                                                }
                                                current = current->next;
                                            } while (current != ready_queue_head);
                                        }
                                        if (task && task->state == TASK_STATE_TERMINATED) {
                                            return task->exit_code;
                                        }
                                        return -1; // Not terminated or not found
                                    }

                                    int32 task_wait_pid(uint32 pid) {
                                        while (task_exists(pid)) {
                                            int32 exit_code = task_get_exit_code(pid);
                                            if (exit_code != -1) {
                                                return exit_code;
                                            }
                                            // Yield to allow scheduler to run
                                            task_schedule();
                                            __asm__ __volatile__("hlt");
                                        }
                                        return -1; // Task not found
                                    }

                                    uint32 task_get_last_active_tick(uint32 pid) {
                                        uint32 tick = 0;
                                        spinlock_acquire(&task_scheduler_lock);

                                        if (ready_queue_head) {
                                            task_t* current = ready_queue_head;
                                            do {
                                                if (current && current->id == pid) {
                                                    tick = current->last_active_tick;
                                                    break;
                                                }
                                                current = current->next;
                                            } while (current && current != ready_queue_head);
                                        }

                                        spinlock_release(&task_scheduler_lock);
                                        return tick;
                                    }

                                    void task_mark_active(void) {
                                        if (!current_task) {
                                            return;
                                        }
                                        current_task->last_active_tick = timer_get_ticks();
                                    }

                                    // ============================================================================
                                    // Foreground Task API - Priority scheduling for GUI applications
                                    // ============================================================================

                                    void task_set_foreground(task_t* task) {
                                        spinlock_acquire(&task_scheduler_lock);
                                        foreground_task = task;
                                        if (task) {
                                            debuglog(DEBUG_INFO, "[TASK] Set foreground task: PID %u (%s)\n", task->id, task->name);
                                        }
                                        spinlock_release(&task_scheduler_lock);
                                    }

                                    void task_clear_foreground(void) {
                                        spinlock_acquire(&task_scheduler_lock);
                                        if (foreground_task) {
                                            debuglog(DEBUG_INFO, "[TASK] Cleared foreground task: PID %u\n", foreground_task->id);
                                        }
                                        foreground_task = NULL;
                                        spinlock_release(&task_scheduler_lock);
                                    }

                                    task_t* task_get_foreground(void) {
                                        return foreground_task;
                                    }

                                    bool task_is_foreground(task_t* task) {
                                        return task && task == foreground_task;
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

                                    // Forward declaration for deferred cleanup
                                    static void task_process_deferred_cleanup(void);

                                    void task_schedule(void) {
                                        // Process any deferred task cleanups first (safe to do now that we're scheduling)
                                        task_process_deferred_cleanup();

                                        uint32 current_ticks = timer_get_ticks();

                                        // Poll mouse for constant movement detection when switching tasks
                                        // This ensures mouse position is always up-to-date for GUI applications
                                        ps2_mouse_poll();

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

                                            // Special case: if this is the only task
                                            if (current_task->next == current_task) {
                                                ready_queue_head = NULL;
                                                current_task = NULL;
                                                kernel_panic("Last task terminated - no tasks remaining!");
                                            }

                                            task_destroy(current_task);

                                            // If ready_queue_head became null, there are no more tasks.
                                            if (ready_queue_head == NULL) {
                                                kernel_panic("No more tasks to schedule after terminating a task!");
                                            }

                                            /*
                                             * IMPORTANT: keep current_task pointing at the terminating context
                                             * until task_switch() runs. Repointing current_task here can make
                                             * task_switch() think we're already on the destination task and skip
                                             * the switch, returning to the exiting task's halt loop.
                                             */
                                        }

                                        if (!ready_queue_head) {
                                            print("[TASK] WARNING: No tasks in ready queue, creating idle task\n");
                                            // We need at least one task to run
                                            kernel_panic("No tasks available to schedule!");
                                        }

                                        // PRIORITY: Always prefer foreground task if it's runnable
                                        // This ensures GUI apps get responsive scheduling
                                        task_t* next_task = NULL;
                                        bool found_runnable = false;

                                        if (foreground_task &&
                                            (foreground_task->state == TASK_STATE_READY || foreground_task->state == TASK_STATE_RUNNING) &&
                                            !(foreground_task->pending_signals & SIGKILL) &&
                                            !foreground_task->is_background) {
                                            // Foreground task is runnable - use it (unless it's a background task)
                                            next_task = foreground_task;
                                        found_runnable = true;
                                            }

                                            // If no foreground or foreground not runnable, do normal round-robin
                                            if (!found_runnable) {
                                                next_task = current_task;
                                                if (!next_task) {
                                                    next_task = ready_queue_head;
                                                }
                                            }

                                            task_t* initial_scan_start = next_task;
                                            int scan_count = 0; // Prevent infinite loops

                                            // Only scan if we haven't already found a runnable task (foreground)
                                            while (!found_runnable) {
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

                                                // Stop if we've scanned the whole queue
                                                if (next_task == initial_scan_start) {
                                                    break;
                                                }
                                            }

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

                                        /*
                                         * Scheduler may destroy the current task while still executing on its
                                         * kernel stack/context. In that case, defer unsafe frees to avoid
                                         * self-destruction (use-after-free / active CR3 teardown).
                                         */
                                        uintptr_t current_esp;
                                        #if ARCH_64BIT
                                        __asm__ __volatile__("mov %%rsp, %0" : "=r"(current_esp));
                                        #else
                                        __asm__ __volatile__("mov %%esp, %0" : "=r"(current_esp));
                                        #endif
                                        uintptr_t stack_start = task->kernel_stack_base;
                                        uintptr_t stack_end = stack_start + KERNEL_STACK_SIZE;
                                        bool destroying_current_context =
                                        (task == current_task) ||
                                        (current_esp >= stack_start && current_esp < stack_end);

                                        // Clear foreground status if this task was the foreground task
                                        // This is done WITHOUT acquiring the lock since we may already hold it
                                        if (task == foreground_task) {
                                            debuglog(DEBUG_INFO, "[TASK] Foreground task %u being destroyed\n", task->id);
                                            foreground_task = NULL;
                                        }

                                        // Emit a final notice about the task's termination context.
                                        print("[TASK] PID ");
                                        print(int_to_string(task->id));
                                        print(" terminated");
                                        if (task->exit_reason[0]) {
                                            print(" (");
                                            print(task->exit_reason);
                                            print(")");
                                        }
                                        print(" with code ");
                                        print(int_to_string(task->exit_code));
                                        print("\n");

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

                                        /*
                                         * Do not destroy the page directory that is currently active on the CPU.
                                         * The scheduler can call task_destroy(current_task) while still executing
                                         * on that task context; freeing its page-directory frame in-place leaves
                                         * CR3 pointing at reclaimed memory and can stall/crash post-exit handoff.
                                         * Defer cleanup in this case (leak for now, correctness first).
                                         */
                                        page_directory_t* active_pd = vmm_get_current_page_directory();
                                        if (task->page_directory &&
                                            task->page_directory != active_pd &&
                                            !destroying_current_context) {
                                            vmm_destroy_page_directory(task->page_directory);
                                            } else if (task->page_directory) {
                                                // Defer cleanup - add to list for later processing
                                                print("[TASK] Deferring page-directory cleanup for PID ");
                                                print(int_to_string(task->id));
                                                print("\n");

                                                spinlock_acquire(&deferred_cleanup_lock);
                                                if (deferred_cleanup_count < MAX_DEFERRED_CLEANUP) {
                                                    deferred_cleanup_tasks[deferred_cleanup_count++] = task;
                                                } else {
                                                    print("[TASK] WARNING: Deferred cleanup list full, leaking memory!\n");
                                                }
                                                spinlock_release(&deferred_cleanup_lock);

                                                // Don't free task struct yet - we need it for deferred cleanup
                                                return;
                                            }

                                            if (!destroying_current_context) {
                                                // Safe to free - we're not on this task's live context.
                                                kfree((void*)task->kernel_stack_base);
                                                kfree(task);
                                            } else {
                                                print("[TASK] WARNING: Deferring kernel stack cleanup (in use)\n");
                                                print("[TASK] WARNING: Deferring task-struct cleanup (in use)\n");
                                            }

                                            // If the destroyed task was the current task, a reschedule will be necessary.
                                            // This scenario should primarily be handled by the scheduler itself,
                                            // which would call task_destroy for a terminated task and then reschedule.
                                    }

                                    // Process deferred task cleanup - call after context switch when safe
                                    void task_process_deferred_cleanup(void) {
                                        spinlock_acquire(&deferred_cleanup_lock);

                                        uint32_t count = deferred_cleanup_count;
                                        deferred_cleanup_count = 0;  // Reset count before processing

                                        // Copy the list locally so we can release the lock
                                        task_t* local_list[MAX_DEFERRED_CLEANUP];
                                        for (uint32_t i = 0; i < count; i++) {
                                            local_list[i] = deferred_cleanup_tasks[i];
                                            deferred_cleanup_tasks[i] = NULL;
                                        }

                                        spinlock_release(&deferred_cleanup_lock);

                                        // Process cleanups without holding the lock
                                        for (uint32_t i = 0; i < count; i++) {
                                            task_t* task = local_list[i];
                                            if (task && task->page_directory) {
                                                print("[TASK] Processing deferred cleanup for PID ");
                                                print(int_to_string(task->id));
                                                print("\n");

                                                // Now safe to clean up page directory - we're on different context
                                                vmm_destroy_page_directory(task->page_directory);
                                                task->page_directory = NULL;

                                                // Free kernel stack and task struct
                                                kfree((void*)task->kernel_stack_base);
                                                kfree(task);

                                                print("[TASK] Deferred cleanup complete for PID ");
                                                print(int_to_string(task->id));
                                                print("\n");
                                            }
                                        }
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

void task_send_signal(uint32 pid, int signal) {
    if (signal < 1 || signal > 31) {
        return;
    }

    if (pid == 0) {
        // Send to current process group
        if (!current_task) {
            return;
        }
        task_send_signal_to_pgrp(current_task->pgrp, signal);
        return;
    }

    if (pid < 0) {
        // Send to process group (absolute value of pid)
        uint32 pgrp = (uint32)-pid;
        task_send_signal_to_pgrp(pgrp, signal);
        return;
    }

    // Find the target task
    task_t* target_task = ready_queue_head;
    if (target_task) {
        do {
            if (target_task->id == (uint32)pid) {
                target_task->pending_signals |= (1 << (signal - 1));
                if (target_task->state == TASK_STATE_WAITING) {
                    target_task->state = TASK_STATE_READY;
                }
                return;
            }
            target_task = target_task->next;
        } while (target_task != ready_queue_head);
    }
}

void task_send_signal_to_pgrp(uint32 pgrp, int signal) {
    if (signal < 1 || signal > 31) {
        return;
    }

    task_t* current = ready_queue_head;
    if (current) {
        do {
            if (current->pgrp == pgrp) {
                current->pending_signals |= (1 << (signal - 1));
                if (current->state == TASK_STATE_WAITING) {
                    current->state = TASK_STATE_READY;
                }
            }
            current = current->next;
        } while (current != ready_queue_head);
    }
}

void task_yield(void) {
    // Yield CPU to next task by calling the scheduler
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

                                    void task_terminate_current(int signal) {
                                        (void)signal;  /* Signal type for future use */

                                        if (!current_task) {
                                            print("[TASK] Cannot terminate - no current task\n");
                                            return;
                                        }

                                        print("[TASK] Terminating current task (ID: ");
                                        print(int_to_string(current_task->id));
                                        print(") with signal ");
                                        print(int_to_string(signal));
                                        print("\n");

                                        /* Mark the task for termination */
                                        current_task->state = TASK_STATE_TERMINATED;
                                        current_task->pending_signals |= SIGKILL;

                                        /* Reschedule to allow another task to run */
                                        task_schedule();

                                        /* If we return here, halt (shouldn't happen) */
                                        while (1) {
                                            __asm__ volatile("hlt");
                                        }
                                    }

                                    // Graceful task exit with a reason and code. This is intended to be called
                                    // from sys_exit or any fatal user-mode path to surface the failure on the
                                    // TTY instead of silently halting.
                                    void task_exit(int code, const char* reason) {
                                        if (!current_task) {
                                            print("[TASK] task_exit called with no current task\n");
                                            return;
                                        }

                                        // Record exit details
                                        current_task->exit_code = code;
                                        memory_set((uint8*)current_task->exit_reason, 0, sizeof(current_task->exit_reason));
                                        if (reason) {
                                            // Truncate to fit
                                            for (size_t i = 0; i + 1 < sizeof(current_task->exit_reason) && reason[i]; i++) {
                                                current_task->exit_reason[i] = reason[i];
                                            }
                                        }

                                        print("[TASK] PID ");
                                        print(int_to_string(current_task->id));
                                        print(" exiting with code ");
                                        print(int_to_string(code));
                                        if (current_task->exit_reason[0]) {
                                            print(" (");
                                            print(current_task->exit_reason);
                                            print(")");
                                        }
                                        print("\n");

                                        // Mark and reschedule; scheduler will reclaim resources
                                        current_task->state = TASK_STATE_TERMINATED;
                                        current_task->pending_signals |= SIGKILL;
                                        task_schedule();

                                        // Should not return; if it does, halt to avoid running a dead task.
                                        while (1) {
                                            __asm__ __volatile__("hlt");
                                        }
                                    }
