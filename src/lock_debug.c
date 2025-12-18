#include "include/lock_debug.h"
#include "include/screen.h"
#include "include/util.h"
#include "include/memory.h"
#include "include/string.h"

#if LOCK_DEBUG_ENABLED

static lock_debug_info_t* debug_lock_list = NULL;
static spinlock_t debug_list_lock = SPINLOCK_INIT("debug_list");

void lock_debug_init(void) {
    debug_lock_list = NULL;
}

void lock_debug_register_lock(const char* name, void* lock_ptr) {
    (void)lock_ptr;
    
    spinlock_acquire(&debug_list_lock);
    
    lock_debug_info_t* existing = debug_lock_list;
    while (existing) {
        if (strcmp(existing->name, name) == 0) {
            spinlock_release(&debug_list_lock);
            return;
        }
        existing = existing->next;
    }
    
    lock_debug_info_t* new_info = (lock_debug_info_t*)kmalloc(sizeof(lock_debug_info_t));
    if (new_info) {
        new_info->name = name;
        new_info->acquisition_count = 0;
        new_info->contention_count = 0;
        new_info->max_hold_time = 0;
        new_info->total_hold_time = 0;
        new_info->last_acquired_at = 0;
        new_info->current_holder_cpu = 0;
        new_info->currently_held = false;
        new_info->next = debug_lock_list;
        debug_lock_list = new_info;
    }
    
    spinlock_release(&debug_list_lock);
}

void lock_debug_record_acquire(const char* name, uint32 timestamp) {
    spinlock_acquire(&debug_list_lock);
    
    lock_debug_info_t* info = debug_lock_list;
    while (info) {
        if (strcmp(info->name, name) == 0) {
            info->last_acquired_at = timestamp;
            info->currently_held = true;
            info->acquisition_count++;
            break;
        }
        info = info->next;
    }
    
    spinlock_release(&debug_list_lock);
}

void lock_debug_record_release(const char* name, uint32 timestamp) {
    spinlock_acquire(&debug_list_lock);
    
    lock_debug_info_t* info = debug_lock_list;
    while (info) {
        if (strcmp(info->name, name) == 0) {
            if (info->currently_held && info->last_acquired_at != 0) {
                uint32 hold_time = timestamp - info->last_acquired_at;
                info->total_hold_time += hold_time;
                if (hold_time > info->max_hold_time) {
                    info->max_hold_time = hold_time;
                }
            }
            info->currently_held = false;
            break;
        }
        info = info->next;
    }
    
    spinlock_release(&debug_list_lock);
}

void lock_debug_record_contention(const char* name) {
    spinlock_acquire(&debug_list_lock);
    
    lock_debug_info_t* info = debug_lock_list;
    while (info) {
        if (strcmp(info->name, name) == 0) {
            info->contention_count++;
            break;
        }
        info = info->next;
    }
    
    spinlock_release(&debug_list_lock);
}

void lock_debug_print_stats(void) {
    print_colored("=== LOCK DEBUGGING STATISTICS ===\n", 0x0E, 0x00);
    
    spinlock_acquire(&debug_list_lock);
    
    lock_debug_info_t* info = debug_lock_list;
    while (info) {
        print("Lock: ");
        print(info->name);
        print("\n");
        print("  Acquisitions: ");
        print(int_to_string(info->acquisition_count));
        print("\n");
        print("  Contentions: ");
        print(int_to_string(info->contention_count));
        print("\n");
        print("  Max hold time: ");
        print(int_to_string(info->max_hold_time));
        print(" cycles\n");
        
        if (info->acquisition_count > 0) {
            uint32 avg_hold_time = info->total_hold_time / info->acquisition_count;
            print("  Avg hold time: ");
            print(int_to_string(avg_hold_time));
            print(" cycles\n");
        }
        
        print("  Currently held: ");
        print(info->currently_held ? "YES" : "NO");
        print("\n");
        
        if (info->acquisition_count > 0 && info->contention_count > 0) {
            uint32 contention_ratio = (info->contention_count * 100) / info->acquisition_count;
            print("  Contention ratio: ");
            print(int_to_string(contention_ratio));
            print("%\n");
        }
        print("\n");
        
        info = info->next;
    }
    
    spinlock_release(&debug_list_lock);
    
    print_colored("=== END LOCK STATISTICS ===\n", 0x0E, 0x00);
}

void lock_debug_detect_deadlocks(void) {
    print_colored("=== DEADLOCK DETECTION ===\n", 0x0C, 0x00);
    
    spinlock_acquire(&debug_list_lock);
    
    bool deadlock_detected = false;
    lock_debug_info_t* info = debug_lock_list;
    
    while (info) {
        if (info->currently_held) {
            uint32 current_time = get_timestamp();
            uint32 hold_duration = current_time - info->last_acquired_at;
            
            if (hold_duration > (info->max_hold_time * 10) && hold_duration > 1000000) {
                print_colored("POTENTIAL DEADLOCK: Lock '", 0x0C, 0x00);
                print(info->name);
                print_colored("' held for excessive time: ", 0x0C, 0x00);
                print(int_to_string(hold_duration));
                print(" cycles\n");
                deadlock_detected = true;
            }
        }
        info = info->next;
    }
    
    if (!deadlock_detected) {
        print_colored("No deadlocks detected.\n", 0x0A, 0x00);
    }
    
    spinlock_release(&debug_list_lock);
    
    print_colored("=== END DEADLOCK DETECTION ===\n", 0x0C, 0x00);
}

#endif // LOCK_DEBUG_ENABLED