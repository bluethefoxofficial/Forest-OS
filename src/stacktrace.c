#include "stacktrace.h"
#include "debuglog.h"

#ifndef ARCH_64BIT
#if defined(__x86_64__) || defined(_M_X64)
#define ARCH_64BIT 1
#else
#define ARCH_64BIT 0
#endif
#endif

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
int stacktrace_init_symbols(void) { return 0; }
#else

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

void stacktrace_print(const stacktrace_t *trace) {
    if (!trace) return;
    
    debuglog(DEBUG_INFO, "Stack trace (%zu frames):\n", trace->frame_count);
    for (size_t i = 0; i < trace->frame_count; i++) {
        stacktrace_print_frame(&trace->frames[i], i);
    }
}

void stacktrace_print_current(void) {
    stacktrace_t trace;
    if (stacktrace_capture(&trace) == 0) {
        stacktrace_print(&trace);
    }
}

void stacktrace_print_frame(const stack_frame_t *frame, size_t index) {
    debuglog(DEBUG_INFO, "  #%zu: 0x%08x (bp=0x%08x)\n", 
             index, (uint32_t)frame->ip, (uint32_t)frame->bp);
}

const char *stacktrace_resolve_symbol(uintptr_t address) {
    // Symbol resolution not implemented yet
    return NULL;
}

int stacktrace_init_symbols(void) {
    // Symbol initialization not implemented yet
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
