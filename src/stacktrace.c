#include "stacktrace.h"
#include "debuglog.h"
#include "string.h"
#include "util.h"

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

// Known function addresses for symbol resolution
typedef struct {
    uintptr_t addr;
    const char* name;
} symbol_entry_t;

static symbol_entry_t kernel_symbols[] = {
    { 0x001D191E, "task_start_usermode_asm" },
    { 0x001D19EA, "task_switch_asm" },
    { 0x001545E4, "task_schedule" },
    { 0x001547F5, "schedule_tail" },
    { 0x0018DB1E, "isr128_stub" },
    { 0x00154500, "task_exit" },
    { 0x00154000, "syscall_handle" },
    { 0x00153000, "interrupt_dispatch" },
    { 0x00152000, "exception_handler" },
    { 0x00151000, "handle_exception" },
    { 0x00150000, "general_protection_fault" },
    { 0, NULL }
};

// Syscall name mapping
static const char* syscall_names[] = {
    "exit", "fork", "read", "write", "open", "close", "wait4", "link", "unlink",
    "execve", "chdir", "time", "mknod", "chmod", "chown", "getpid", "setuid",
    "getuid", "geteuid", "ptrace", "alarm", "pause", "utime", "access", "nice",
    "stat", "fstat", "lseek", "getpid", "getuid", "dup", "pipe", "times",
    "brk", "setgid", "getgid", "geteuid", "getegid", "acct", "umount", "lock",
    "ioctl", "reboot", "symlink", "readlink", "dup2", "mkdup", "pause", "mprotect",
    "munmap", "truncate", "ftruncate", "getpgrp", "getgroups", "setgroups", "getdents",
    "select", "poll", "madvise", "socket", "bind", "connect", "accept", "getpeername",
    "getsockname", "socketpair", "sendto", "recvfrom", "shutdown", "sendmsg", "recvmsg",
    "wait4", "kill", "uname", "semget", "semop", "semctl", "shmget", "shmat",
    "shmctl", "dup3", "pipe2", "inotify_init1", "pread64", "pwrite64", "getcwd",
    "membarrier", "memfd_create", "execveat", "preadv", "pwritev", "process_vm_readv",
    "process_vm_writev", "setsid", "getsid", "gettid", "setpgid", "getpgid", "getppid"
};

#if ARCH_64BIT
int stacktrace_capture(stacktrace_t *trace) {
    if (!trace) return -1;
    trace->frame_count = 0;
    trace->symbols_available = false;
    return 0;
}

void stacktrace_print(const stacktrace_t *trace) { (void)trace; }
void stacktrace_print_current(void) {}
void stacktrace_print_frame(const stack_frame_t *frame, size_t index) {
    (void)frame; (void)index;
}
const char *stacktrace_resolve_symbol(uintptr_t address) { (void)address; return NULL; }
int stacktrace_init_symbols(void) { 
    return 0;
}
#else

static const char* get_syscall_name(uint32_t syscall_num) {
    if (syscall_num < sizeof(syscall_names)/sizeof(syscall_names[0])) {
        return syscall_names[syscall_num];
    }
    return NULL;
}

int stacktrace_capture(stacktrace_t *trace) {
    if (!trace) return -1;
    
    trace->frame_count = 0;
    trace->symbols_available = false;
    
    // Simple stack walking using frame pointers
    uintptr_t *ebp;
    __asm__ volatile ("mov %%ebp, %0" : "=r"(ebp));
    
    for (size_t i = 0; i < MAX_STACK_FRAMES && ebp != NULL; i++) {
        if ((uintptr_t)ebp < 0x1000 || (uintptr_t)ebp > 0xFFFFE000) {
            break; // Invalid frame pointer
        }
        
        trace->frames[i].bp = (uintptr_t)ebp;
        trace->frames[i].ip = ebp[1]; // Return address
        trace->frames[i].symbol = NULL;
        trace->frame_count++;
        
        ebp = (uintptr_t*)ebp[0]; // Next frame
    }
    
    return 0;
}

const char *stacktrace_resolve_symbol(uintptr_t address) {
    // Check for known kernel symbols
    for (int i = 0; kernel_symbols[i].name != NULL; i++) {
        // Check if address is within a few bytes of known symbol
        if (address >= kernel_symbols[i].addr && 
            address < kernel_symbols[i].addr + 0x100) {
            return kernel_symbols[i].name;
        }
    }
    
    // Check if this might be a syscall return address
    // Syscall handler typically starts around 0x00154000
    if (address >= 0x00154000 && address < 0x00155000) {
        return "syscall_handler";
    }
    
    return NULL;
}

static void format_frame_info(uintptr_t ip, uintptr_t bp, size_t index) {
    debuglog_write("  #");
    debuglog_write_dec(index);
    debuglog_write(": 0x");
    debuglog_write_hex((uint32_t)ip);
    debuglog_write(" (bp=0x");
    debuglog_write_hex((uint32_t)bp);
    debuglog_write(")");
    
    const char* symbol = stacktrace_resolve_symbol(ip);
    if (symbol) {
        debuglog_write(" <");
        debuglog_write(symbol);
        debuglog_write(">");
    }
    debuglog_write("\n");
}

void stacktrace_print(const stacktrace_t *trace) {
    debuglog_write("Stack trace (");
    debuglog_write_dec(trace->frame_count);
    debuglog_write(" frames):\n");
    for (size_t i = 0; i < trace->frame_count; i++) {
        const stack_frame_t *frame = &trace->frames[i];
        format_frame_info(frame->ip, frame->bp, i);
    }
}

void stacktrace_print_current(void) {
    stacktrace_t trace;
    if (stacktrace_capture(&trace) == 0) {
        stacktrace_print(&trace);
    }
}

void stacktrace_print_frame(const stack_frame_t *frame, size_t index) {
    format_frame_info(frame->ip, frame->bp, index);
}

int stacktrace_init_symbols(void) {
    return 0;
}
#endif

void stacktrace_dump_registers(void) {
    uint32_t eax, ebx, ecx, edx, esp, ebp, esi, edi;
    
    __asm__ volatile (
        "mov %%eax, %0; mov %%ebx, %1; mov %%ecx, %2; mov %%edx, %3;"
        "mov %%esp, %4; mov %%ebp, %5; mov %%esi, %6; mov %%edi, %7"
        : "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx),
          "=m"(esp), "=m"(ebp), "=m"(esi), "=m"(edi)
    );
    
    debuglog(DEBUG_INFO, "Register dump:\n");
    debuglog(DEBUG_INFO, "  EAX=0x%08x EBX=0x%08x ECX=0x%08x EDX=0x%08x\n", 
             eax, ebx, ecx, edx);
    debuglog(DEBUG_INFO, "  ESP=0x%08x EBP=0x%08x ESI=0x%08x EDI=0x%08x\n", 
             esp, ebp, esi, edi);
}

void stacktrace_print_exception(const char *exception_name, uintptr_t error_code) {
    debuglog(DEBUG_ERROR, "Exception: %s (error=0x%x)\n", 
             exception_name, (uint32_t)error_code);
    stacktrace_dump_registers();
    stacktrace_print_current();
}
