/*
 * Forest OS Enhanced Panic Handler
 * 
 * Professional kernel panic handler with advanced debugging capabilities,
 * superior visual interface, and comprehensive system analysis.
 * 
 * Features:
 * - Intelligent register capture with atomic operations
 * - Advanced memory corruption detection and analysis
 * - Professional TUI with full-screen debugging interfaces
 * - Error classification and recovery suggestions
 * - Interactive command processor with extensive help
 * - Stack unwinding and call trace analysis
 * - Hardware state inspection and validation
 * - Safe keyboard input with timeout handling
 */

#include "include/panic.h"
#include "include/screen.h"
#include "include/system.h"
#include "include/util.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/hardware.h"
#include "include/ps2_mouse.h"
#include "include/ps2_controller.h"
#include "include/kb.h"
#include "include/debuglog.h"

// =============================================================================
// CONSTANTS AND CONFIGURATION
// =============================================================================

#define PANIC_VERSION "2.0 Professional"
#define PANIC_ENABLE_MOUSE 0
#define PANIC_DEBUGLOG_ENABLED 1
#define MAX_STACK_FRAMES 16
#define MAX_MEMORY_REGIONS 8
#define DEBUG_DELAY_MS 100
#define PANIC_MEMORY_SCROLL_RANGE 64
#define PANIC_MEMORY_STEP_BYTES 16

// Color scheme for professional appearance
#define COLOR_ERROR       0x04  // Red background
#define COLOR_WARNING     0x06  // Orange background  
#define COLOR_INFO        0x03  // Cyan background
#define COLOR_SUCCESS     0x02  // Green background
#define COLOR_NORMAL      0x01  // Blue background
#define COLOR_HIGHLIGHT   0x05  // Purple background

#define FG_WHITE    0x0F
#define FG_YELLOW   0x0E
#define FG_CYAN     0x0B
#define FG_GREEN    0x0A
#define FG_RED      0x0C
#define FG_GRAY     0x08

// =============================================================================
// ENHANCED DATA STRUCTURES
// =============================================================================

typedef enum {
    PANIC_TYPE_GENERAL,
    PANIC_TYPE_PAGE_FAULT,
    PANIC_TYPE_DOUBLE_FAULT,
    PANIC_TYPE_STACK_OVERFLOW,
    PANIC_TYPE_MEMORY_CORRUPTION,
    PANIC_TYPE_HARDWARE_FAILURE,
    PANIC_TYPE_ASSERTION_FAILED,
    PANIC_TYPE_KERNEL_OOPS
} panic_type_t;

typedef struct {
    uint32 eax, ebx, ecx, edx;
    uint32 esi, edi, ebp, esp;
    uint32 eip, eflags;
    uint32 cr0, cr2, cr3, cr4;
    uint16 cs, ds, es, fs, gs, ss;
    uint32 return_address;
    uint64 timestamp;
} cpu_state_t;

typedef struct {
    uint32 address;
    uint32 caller;
    bool valid;
} stack_frame_t;

typedef struct {
    const char* message;
    const char* file;
    const char* function;
    uint32 line;
    panic_type_t type;
    cpu_state_t cpu_state;
    stack_frame_t stack_trace[MAX_STACK_FRAMES];
    uint32 stack_frame_count;
    uint32 error_code;
    bool recoverable;
    bool manual_stack_valid;
    uint32 manual_stack[MAX_STACK_FRAMES];
    uint32 manual_stack_count;
    int32 scroll_offsets[PANIC_SCREEN_MAX];
} panic_context_t;

typedef struct {
    const char* name;
    uint32 start_addr;
    uint32 end_addr;
    const char* description;
} panic_memory_region_t;

// =============================================================================
// GLOBAL STATE
// =============================================================================

static panic_context_t g_panic_context;
static bool g_panic_initialized = false;
static uint32 g_panic_count = 0;

#if PANIC_ENABLE_MOUSE
// Mouse support for panic interface
static bool panic_mouse_enabled = false;
static tui_mouse_state_t panic_mouse_state;
static ps2_mouse_state_t panic_ps2_mouse_state;
static uint8 panic_mouse_packet[3];
static uint8 panic_mouse_packet_index = 0;

#define PANIC_MOUSE_SYNC_BIT 0x08
#else
#define panic_mouse_enabled false
#endif

static int32 panic_scroll_get(panic_screen_id_t screen) {
    if (screen >= PANIC_SCREEN_MAX) return 0;
    return g_panic_context.scroll_offsets[screen];
}

static void panic_scroll_set(panic_screen_id_t screen, int32 value) {
    if (screen >= PANIC_SCREEN_MAX) return;
    g_panic_context.scroll_offsets[screen] = value;
}

static bool panic_can_read_range(uint32 start, uint32 length, const char** failure_reason) {
    if (failure_reason) *failure_reason = NULL;
    if (length == 0) {
        if (failure_reason) *failure_reason = "Zero-length memory request";
        return false;
    }
    
    if (start + length < start) {
        if (failure_reason) *failure_reason = "Address calculation overflowed";
        return false;
    }
    
    if (start < 0x1000) {
        if (failure_reason) *failure_reason = "Address below 0x1000 is unmapped";
        return false;
    }
    
    if ((start + length) > MEMORY_MAX_ADDR) {
        if (failure_reason) *failure_reason = "Address exceeds maximum mapped memory";
        return false;
    }
    
    page_directory_t* dir = vmm_get_current_page_directory();
    if (!dir) {
        if (failure_reason) *failure_reason = "No active page directory";
        return false;
    }
    
    uint32 first_page = memory_align_down(start, MEMORY_PAGE_SIZE);
    uint32 last_page = memory_align_down(start + length - 1, MEMORY_PAGE_SIZE);
    
    for (uint32 addr = first_page; ; addr += MEMORY_PAGE_SIZE) {
        if (vmm_get_physical_addr(dir, addr) == 0) {
            if (failure_reason) *failure_reason = "Region not mapped in current page directory";
            return false;
        }
        if (addr == last_page) {
            break;
        }
    }
    
    return true;
}

static const panic_memory_region_t g_memory_regions[MAX_MEMORY_REGIONS] = {
    {"Low Memory",        0x00000000, 0x000FFFFF, "Real mode memory"},
    {"Extended Memory",   0x00100000, 0x3FFFFFFF, "Available RAM"},
    {"Kernel Space",      0xC0000000, 0xFFFFFFFF, "Kernel virtual memory"},
    {"Video Memory",      0x000A0000, 0x000BFFFF, "VGA frame buffer"},
    {"BIOS ROM",          0x000F0000, 0x000FFFFF, "System BIOS"},
    {"Hardware I/O",      0xFEC00000, 0xFEE00000, "APIC/IOAPIC"},
    {"PCI Config",        0x80000000, 0x8FFFFFFF, "PCI configuration"},
    {"Reserved",          0x40000000, 0x7FFFFFFF, "Reserved/unmapped"}
};

// =============================================================================
// ATOMIC REGISTER CAPTURE
// =============================================================================

static void capture_cpu_state_atomic(cpu_state_t* state) {
    uint32 temp_esp, temp_ebp;
    
    // Capture registers with minimal interference
    __asm__ __volatile__(
        "pushf\n\t"
        "cli\n\t"
        "mov %%eax, %0\n\t"
        "mov %%ebx, %1\n\t"
        "mov %%ecx, %2\n\t"
        "mov %%edx, %3\n\t"
        "mov %%esi, %4\n\t"
        "mov %%edi, %5\n\t"
        "mov %%esp, %6\n\t"
        "mov %%ebp, %7\n\t"
        : "=m"(state->eax), "=m"(state->ebx), "=m"(state->ecx), "=m"(state->edx),
          "=m"(state->esi), "=m"(state->edi), "=m"(temp_esp), "=m"(temp_ebp)
        :
        : "memory"
    );
    
    // Adjust ESP to account for pushf/cli
    state->esp = temp_esp + 8;
    state->ebp = temp_ebp;
    
    // Capture instruction pointer
    __asm__ __volatile__(
        "call 1f\n"
        "1: pop %0"
        : "=r"(state->eip)
    );
    
    // Capture flags (from stack after pushf)
    __asm__ __volatile__(
        "pushf\n\t"
        "pop %0"
        : "=r"(state->eflags)
    );
    
    // Capture control registers safely
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(state->cr0));
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(state->cr2));
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(state->cr3));
    __asm__ __volatile__("mov %%cr4, %0" : "=r"(state->cr4));
    
    // Capture segment registers
    __asm__ __volatile__("mov %%cs, %0" : "=r"(state->cs));
    __asm__ __volatile__("mov %%ds, %0" : "=r"(state->ds));
    __asm__ __volatile__("mov %%es, %0" : "=r"(state->es));
    __asm__ __volatile__("mov %%fs, %0" : "=r"(state->fs));
    __asm__ __volatile__("mov %%gs, %0" : "=r"(state->gs));
    __asm__ __volatile__("mov %%ss, %0" : "=r"(state->ss));
    
    // Capture return address from stack
    if (state->esp >= 0x1000 && state->esp < 0xFFFFF000) {
        state->return_address = *((uint32*)state->esp);
    } else {
        state->return_address = 0xDEADBEEF;
    }
    
    // Restore interrupts
    __asm__ __volatile__("popf");
}

// =============================================================================
// INTELLIGENT PANIC TYPE DETECTION
// =============================================================================

static panic_type_t classify_panic(const char* message, uint32 error_code, const cpu_state_t* cpu) {
    if (!message) return PANIC_TYPE_GENERAL;
    
    // Page fault detection
    if (strstr(message, "page fault") || strstr(message, "segmentation") || cpu->cr2 != 0) {
        return PANIC_TYPE_PAGE_FAULT;
    }
    
    // Stack overflow detection
    if (cpu->esp < 0x1000 || cpu->esp > 0xFFFFF000 || 
        strstr(message, "stack") || strstr(message, "overflow")) {
        return PANIC_TYPE_STACK_OVERFLOW;
    }
    
    // Memory corruption
    if (strstr(message, "corruption") || strstr(message, "heap") || 
        strstr(message, "double free") || strstr(message, "use after free")) {
        return PANIC_TYPE_MEMORY_CORRUPTION;
    }
    
    // Hardware failure
    if (strstr(message, "hardware") || strstr(message, "NMI") || 
        strstr(message, "machine check") || strstr(message, "thermal")) {
        return PANIC_TYPE_HARDWARE_FAILURE;
    }
    
    // Assertion failure
    if (strstr(message, "assert") || strstr(message, "ASSERT") || 
        strstr(message, "BUG") || strstr(message, "WARN")) {
        return PANIC_TYPE_ASSERTION_FAILED;
    }
    
    // Double fault
    if (strstr(message, "double fault") || error_code == 8) {
        return PANIC_TYPE_DOUBLE_FAULT;
    }
    
    return PANIC_TYPE_GENERAL;
}

// =============================================================================
// ADVANCED STACK UNWINDING
// =============================================================================

static uint32 unwind_stack(const cpu_state_t* cpu, stack_frame_t* frames, uint32 max_frames) {
    uint32 frame_count = 0;
    uint32* ebp = (uint32*)cpu->ebp;
    
    // Validate initial frame pointer
    if ((uint32)ebp < 0x1000 || (uint32)ebp > 0xFFFFF000) {
        return 0;
    }
    
    // Add current EIP as first frame
    frames[frame_count].address = cpu->eip;
    frames[frame_count].caller = cpu->return_address;
    frames[frame_count].valid = true;
    frame_count++;
    
    // Traverse stack frames
    for (uint32 i = 1; i < max_frames && frame_count < max_frames; i++) {
        // Validate frame pointer
        if ((uint32)ebp < 0x1000 || (uint32)ebp > 0xFFFFF000 ||
            (uint32)ebp <= (uint32)frames[frame_count-1].address) {
            break;
        }
        
        uint32* next_ebp = (uint32*)ebp[0];
        uint32 return_addr = ebp[1];
        
        // Validate return address
        if (return_addr < 0xC0000000 || return_addr > 0xFFFFFFFF) {
            break;
        }
        
        frames[frame_count].address = return_addr;
        frames[frame_count].caller = (frame_count > 0) ? frames[frame_count-1].address : 0;
        frames[frame_count].valid = true;
        frame_count++;
        
        ebp = next_ebp;
    }
    
    return frame_count;
}

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

static void format_hex32(uint32 value, char* buffer) {
    static const char hex_chars[] = "0123456789ABCDEF";
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buffer[2 + i] = hex_chars[(value >> (28 - (i * 4))) & 0xF];
    }
    buffer[10] = '\0';
}

static void format_decimal(uint32 value, char* buffer) {
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    
    char temp[16];
    int pos = 0;
    while (value > 0) {
        temp[pos++] = '0' + (value % 10);
        value /= 10;
    }
    
    for (int i = 0; i < pos; i++) {
        buffer[i] = temp[pos - 1 - i];
    }
    buffer[pos] = '\0';
}

#if PANIC_DEBUGLOG_ENABLED
static void panic_debuglog_write_register(const char* name, uint32 value) {
    char hex[12];
    format_hex32(value, hex);
    debuglog_write("    ");
    debuglog_write(name);
    debuglog_write(" = ");
    debuglog_write(hex);
    debuglog_write("\n");
}

static void panic_debuglog_emit_registers(const cpu_state_t* cpu) {
    debuglog_write("[PANIC][GDB] Register state:\n");
    panic_debuglog_write_register("EAX", cpu->eax);
    panic_debuglog_write_register("EBX", cpu->ebx);
    panic_debuglog_write_register("ECX", cpu->ecx);
    panic_debuglog_write_register("EDX", cpu->edx);
    panic_debuglog_write_register("ESI", cpu->esi);
    panic_debuglog_write_register("EDI", cpu->edi);
    panic_debuglog_write_register("EBP", cpu->ebp);
    panic_debuglog_write_register("ESP", cpu->esp);
    panic_debuglog_write_register("EIP", cpu->eip);
    panic_debuglog_write_register("EFLAGS", cpu->eflags);
    panic_debuglog_write_register("CR0", cpu->cr0);
    panic_debuglog_write_register("CR2", cpu->cr2);
    panic_debuglog_write_register("CR3", cpu->cr3);
    panic_debuglog_write_register("CR4", cpu->cr4);
}

static void panic_debuglog_emit_stack(const panic_context_t* ctx) {
    debuglog_write("[PANIC][GDB] Call stack:\n");
    if (ctx->stack_frame_count == 0) {
        debuglog_write("    (no frames available)\n");
        return;
    }

    for (uint32 i = 0; i < ctx->stack_frame_count; i++) {
        char addr[12];
        char caller[12];
        format_hex32(ctx->stack_trace[i].address, addr);
        format_hex32(ctx->stack_trace[i].caller, caller);

        debuglog_write("    #");
        debuglog_write_dec(i);
        debuglog_write("  ");
        debuglog_write(addr);
        debuglog_write("  caller=");
        debuglog_write(caller);
        debuglog_write(ctx->stack_trace[i].valid ? " [valid]\n" : " [invalid]\n");
    }
}

static void panic_debuglog_emit_manual_stack(const panic_context_t* ctx) {
    if (!ctx->manual_stack_valid || ctx->manual_stack_count == 0) {
        return;
    }

    debuglog_write("[PANIC][GDB] Manual stack snapshot:\n");
    for (uint32 i = 0; i < ctx->manual_stack_count; i++) {
        char addr[12];
        format_hex32(ctx->manual_stack[i], addr);
        debuglog_write("    snapshot[");
        debuglog_write_dec(i);
        debuglog_write("] = ");
        debuglog_write(addr);
        debuglog_write("\n");
    }
}

static void panic_debuglog_emit_meta(const panic_context_t* ctx) {
    debuglog_write("[PANIC][GDB] Context:\n");
    if (ctx->message) {
        debuglog_write("    message: ");
        debuglog_write(ctx->message);
        debuglog_write("\n");
    }
    if (ctx->file) {
        debuglog_write("    location: ");
        debuglog_write(ctx->file);
        if (ctx->line) {
            debuglog_write(":");
            debuglog_write_dec(ctx->line);
        }
        if (ctx->function) {
            debuglog_write(" (");
            debuglog_write(ctx->function);
            debuglog_write(")");
        }
        debuglog_write("\n");
    }
    debuglog_write("    panic count: ");
    debuglog_write_dec(g_panic_count);
    debuglog_write("\n");
}

static void panic_debuglog_emit(const panic_context_t* ctx) {
    if (!debuglog_is_ready()) {
        return;
    }

    debuglog_write("\n[PANIC][GDB] ===== Forest OS panic detected =====\n");
    panic_debuglog_emit_meta(ctx);
    panic_debuglog_emit_registers(&ctx->cpu_state);
    panic_debuglog_emit_stack(ctx);
    panic_debuglog_emit_manual_stack(ctx);
    debuglog_write("[PANIC][GDB] =====================================\n");
}
#endif

static const char* get_panic_type_name(panic_type_t type) {
    switch (type) {
        case PANIC_TYPE_PAGE_FAULT: return "Page Fault";
        case PANIC_TYPE_DOUBLE_FAULT: return "Double Fault";
        case PANIC_TYPE_STACK_OVERFLOW: return "Stack Overflow";
        case PANIC_TYPE_MEMORY_CORRUPTION: return "Memory Corruption";
        case PANIC_TYPE_HARDWARE_FAILURE: return "Hardware Failure";
        case PANIC_TYPE_ASSERTION_FAILED: return "Assertion Failed";
        case PANIC_TYPE_KERNEL_OOPS: return "Kernel Oops";
        default: return "General Panic";
    }
}

static int get_panic_type_color(panic_type_t type) {
    switch (type) {
        case PANIC_TYPE_PAGE_FAULT: return COLOR_ERROR;
        case PANIC_TYPE_DOUBLE_FAULT: return COLOR_ERROR;
        case PANIC_TYPE_STACK_OVERFLOW: return COLOR_WARNING;
        case PANIC_TYPE_MEMORY_CORRUPTION: return COLOR_ERROR;
        case PANIC_TYPE_HARDWARE_FAILURE: return COLOR_ERROR;
        case PANIC_TYPE_ASSERTION_FAILED: return COLOR_WARNING;
        case PANIC_TYPE_KERNEL_OOPS: return COLOR_INFO;
        default: return COLOR_NORMAL;
    }
}

// =============================================================================
// SAFE KEYBOARD INPUT
// =============================================================================

#if PANIC_ENABLE_MOUSE


static void panic_ps2_mouse_callback(ps2_mouse_event_t* ps2_event) {
    if (!ps2_event) return;
    
    // Update local PS/2 mouse state
    panic_ps2_mouse_state.x = ps2_event->x;
    panic_ps2_mouse_state.y = ps2_event->y;
    panic_ps2_mouse_state.left_button = ps2_event->left_button;
    panic_ps2_mouse_state.right_button = ps2_event->right_button;
    panic_ps2_mouse_state.middle_button = ps2_event->middle_button;
    
    // Convert to TUI mouse event and process
    tui_mouse_event_t tui_event;
    tui_event.type = ps2_event->left_button ? TUI_MOUSE_CLICK : TUI_MOUSE_MOVE;
    tui_event.x = ps2_event->x;
    tui_event.y = ps2_event->y;
    tui_event.left_button = ps2_event->left_button;
    tui_event.right_button = ps2_event->right_button;
    tui_event.middle_button = ps2_event->middle_button;
    
    // Constrain to screen bounds
    if (tui_event.x < 0) tui_event.x = 0;
    if (tui_event.y < 0) tui_event.y = 0;
    if (tui_event.x >= (int)screen_width) tui_event.x = screen_width - 1;
    if (tui_event.y >= (int)screen_height) tui_event.y = screen_height - 1;
    
    tui_process_mouse_event(&tui_event);
}

static void panic_process_mouse_byte(uint8 byte) {
    // Synchronize packet on first byte
    if (panic_mouse_packet_index == 0 && !(byte & PANIC_MOUSE_SYNC_BIT)) {
        return;
    }

    panic_mouse_packet[panic_mouse_packet_index++] = byte;

    if (panic_mouse_packet_index < 3) {
        return;
    }

    panic_mouse_packet_index = 0;

    int dx = (int8)panic_mouse_packet[1];
    int dy = (int8)panic_mouse_packet[2];

    panic_ps2_mouse_state.x += dx;
    panic_ps2_mouse_state.y -= dy;

    if (panic_ps2_mouse_state.x < 0) panic_ps2_mouse_state.x = 0;
    if (panic_ps2_mouse_state.y < 0) panic_ps2_mouse_state.y = 0;
    if (panic_ps2_mouse_state.x >= (int)screen_width) panic_ps2_mouse_state.x = screen_width - 1;
    if (panic_ps2_mouse_state.y >= (int)screen_height) panic_ps2_mouse_state.y = screen_height - 1;

    panic_ps2_mouse_state.left_button   = (panic_mouse_packet[0] & 0x01) != 0;
    panic_ps2_mouse_state.right_button  = (panic_mouse_packet[0] & 0x02) != 0;
    panic_ps2_mouse_state.middle_button = (panic_mouse_packet[0] & 0x04) != 0;
    panic_ps2_mouse_state.x_overflow    = (panic_mouse_packet[0] & 0x40) != 0;
    panic_ps2_mouse_state.y_overflow    = (panic_mouse_packet[0] & 0x80) != 0;

    if (!panic_mouse_enabled) {
        return;
    }

    ps2_mouse_event_t ps2_event;
    ps2_event.dx = dx;
    ps2_event.dy = dy;
    ps2_event.x = panic_ps2_mouse_state.x;
    ps2_event.y = panic_ps2_mouse_state.y;
    ps2_event.left_button = panic_ps2_mouse_state.left_button;
    ps2_event.right_button = panic_ps2_mouse_state.right_button;
    ps2_event.middle_button = panic_ps2_mouse_state.middle_button;
    ps2_event.x_overflow = panic_ps2_mouse_state.x_overflow;
    ps2_event.y_overflow = panic_ps2_mouse_state.y_overflow;

    panic_ps2_mouse_callback(&ps2_event);
}

static void panic_poll_mouse_data(void) {
    while (ps2_mouse_data_available()) {
        uint8 mouse_byte = ps2_controller_read_data();
        panic_process_mouse_byte(mouse_byte);
    }
}

static void panic_wait_for_mouse_release(void) {
    if (!panic_mouse_enabled) {
        return;
    }

    // Keep consuming hardware packets to update button state
    while (panic_mouse_state.left_button) {
        panic_poll_mouse_data();
        for (volatile int i = 0; i < 500; i++);
    }
}

// Mouse support for panic interface


static bool panic_mouse_handler(const tui_mouse_event_t* event) {
    if (!event) return false;
    
    // Update panic mouse state
    panic_mouse_state.x = event->x;
    panic_mouse_state.y = event->y;
    panic_mouse_state.left_button = event->left_button;
    panic_mouse_state.right_button = event->right_button;
    panic_mouse_state.middle_button = event->middle_button;
    
    return true;  // Event handled
}

static void init_panic_mouse_support(void) {
    // Try to initialize mouse if not already done
    if (ps2_mouse_init() == 0) {
        // Register our callback for PS/2 mouse events
        ps2_mouse_register_event_callback(panic_ps2_mouse_callback);
        
        // Register TUI mouse handler
        tui_register_mouse_handler(panic_mouse_handler);
        
        // Enable mouse cursor
        tui_show_mouse_cursor(true);
        tui_set_mouse_position(40, 12);  // Center of screen
        
        panic_mouse_enabled = true;
        
        // Initialize mouse state
        panic_mouse_state.x = 40;
        panic_mouse_state.y = 12;
        panic_mouse_state.left_button = false;
        panic_mouse_state.right_button = false;
        panic_mouse_state.middle_button = false;
    }
}
#endif

static char wait_for_key_or_mouse(bool* mouse_clicked, int* mouse_x, int* mouse_y) {
    if (mouse_clicked) *mouse_clicked = false;
    if (mouse_x) *mouse_x = -1;
    if (mouse_y) *mouse_y = -1;

    while (true) {
        char ch;
        if (keyboard_poll_char(&ch)) {
            return ch;
        }

#if PANIC_ENABLE_MOUSE
        panic_poll_mouse_data();

        if (panic_mouse_enabled && mouse_clicked && panic_mouse_state.left_button) {
            *mouse_clicked = true;
            if (mouse_x) *mouse_x = panic_mouse_state.x;
            if (mouse_y) *mouse_y = panic_mouse_state.y;

            panic_wait_for_mouse_release();
            return 0;
        }
#endif

        for (volatile int i = 0; i < 1000; i++);
    }
}

static char wait_for_key(void) {
    return wait_for_key_or_mouse(NULL, NULL, NULL);
}

// =============================================================================
// ENHANCED VISUAL INTERFACE
// =============================================================================

static void draw_panic_banner(const panic_context_t* ctx) {
    clearScreen();
    
    // Get panic type specific color
    int panic_color = get_panic_type_color(ctx->type);
    
    // Professional header with gradient effect
    tui_draw_status_bar(0, "FOREST OS KERNEL PANIC HANDLER v" PANIC_VERSION, 
                       "PROFESSIONAL DEBUGGING INTERFACE", FG_WHITE, panic_color);
    
    // Main error display
    tui_draw_window(1, 2, 78, 6, "CRITICAL SYSTEM ERROR", FG_WHITE, panic_color);
    
    if (ctx->message) {
        tui_center_text(3, 4, 74, ctx->message, FG_YELLOW, panic_color);
    }
    
    // Panic type and classification
    char type_info[80];
    strcpy(type_info, "Error Type: ");
    strcat(type_info, get_panic_type_name(ctx->type));
    tui_center_text(3, 5, 74, type_info, FG_CYAN, panic_color);
    
    if (ctx->file) {
        char location_info[120];
        strcpy(location_info, "Location: ");
        strcat(location_info, ctx->file);
        if (ctx->line > 0) {
            strcat(location_info, " line ");
            char line_str[16];
            format_decimal(ctx->line, line_str);
            strcat(location_info, line_str);
        }
        if (ctx->function) {
            strcat(location_info, " in ");
            strcat(location_info, ctx->function);
            strcat(location_info, "()");
        }
        tui_center_text(3, 6, 74, location_info, FG_WHITE, panic_color);
    }
    
    // System state overview
    tui_draw_window(1, 9, 39, 9, "CPU STATE SUMMARY", FG_WHITE, COLOR_INFO);
    
    char hex_str[12];
    format_hex32(ctx->cpu_state.eip, hex_str);
    tui_print_table_row(3, 11, 35, "Fault Address (EIP)", hex_str, FG_YELLOW, FG_WHITE, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.esp, hex_str);
    tui_print_table_row(3, 12, 35, "Stack Pointer", hex_str, FG_YELLOW, FG_WHITE, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.cr2, hex_str);
    tui_print_table_row(3, 13, 35, "Page Fault Addr", hex_str, FG_YELLOW, 
                       ctx->cpu_state.cr2 ? FG_RED : FG_GRAY, COLOR_INFO);
    
    const char* paging_status = (ctx->cpu_state.cr0 & (1 << 31)) ? "ENABLED" : "DISABLED";
    tui_print_table_row(3, 14, 35, "Paging Status", paging_status, FG_YELLOW, FG_GREEN, COLOR_INFO);
    
    const char* int_status = (ctx->cpu_state.eflags & (1 << 9)) ? "ENABLED" : "DISABLED";
    tui_print_table_row(3, 15, 35, "Interrupts", int_status, FG_YELLOW, FG_GREEN, COLOR_INFO);
    
    if (ctx->stack_frame_count > 0) {
        char frame_details[64];
        char addr_str[12];
        char caller_str[12];
        format_hex32(ctx->stack_trace[0].address, addr_str);
        format_hex32(ctx->stack_trace[0].caller, caller_str);
        strcpy(frame_details, addr_str);
        strcat(frame_details, " -> ");
        strcat(frame_details, caller_str);
        tui_print_table_row(3, 16, 35, "Top Stack Frame", frame_details, FG_YELLOW, FG_WHITE, COLOR_INFO);
    } else if (ctx->manual_stack_valid && ctx->manual_stack_count > 0) {
        char entry_str[12];
        format_hex32(ctx->manual_stack[0], entry_str);
        tui_print_table_row(3, 16, 35, "Stack Snapshot[0]", entry_str, FG_YELLOW, FG_WHITE, COLOR_INFO);
    } else {
        tui_print_table_row(3, 16, 35, "Top Stack Frame", "Unavailable", FG_YELLOW, FG_GRAY, COLOR_INFO);
    }
    
    // Stack trace preview
    tui_draw_window(41, 9, 38, 8, "CALL STACK PREVIEW", FG_WHITE, COLOR_SUCCESS);
    
    if (ctx->stack_frame_count > 0) {
        for (uint32 i = 0; i < ctx->stack_frame_count && i < 5; i++) {
            if (ctx->stack_trace[i].valid) {
                format_hex32(ctx->stack_trace[i].address, hex_str);
                char frame_info[32];
                strcpy(frame_info, "Frame ");
                char frame_num[8];
                format_decimal(i, frame_num);
                strcat(frame_info, frame_num);
                tui_print_table_row(43, 11 + i, 34, frame_info, hex_str, FG_YELLOW, FG_WHITE, COLOR_SUCCESS);
            }
        }
    } else if (ctx->manual_stack_valid) {
        for (uint32 i = 0; i < ctx->manual_stack_count && i < 5; i++) {
            format_hex32(ctx->manual_stack[i], hex_str);
            char frame_info[32];
            strcpy(frame_info, "Stack[");
            char frame_num[8];
            format_decimal(i, frame_num);
            strcat(frame_info, frame_num);
            strcat(frame_info, "]");
            tui_print_table_row(43, 11 + i, 34, frame_info, hex_str, FG_YELLOW, FG_WHITE, COLOR_SUCCESS);
        }
    } else {
        tui_print_at(43, 12, "No valid stack trace available", FG_GRAY, COLOR_SUCCESS);
    }
    
    // Command interface
    tui_draw_window(1, 18, 78, 6, "INTERACTIVE DEBUGGER COMMANDS", FG_WHITE, COLOR_NORMAL);
    tui_print_at(3, 20, "R=CPU Registers   M=Memory   S=Stack   H=Help   A=Analysis   D=Dump   Q=Info", FG_CYAN, COLOR_NORMAL);
    tui_print_at(3, 21, "T=System Info    I=Interrupts   C=Control Regs   F=Flags   V=Virtual Memory", FG_CYAN, COLOR_NORMAL);
    tui_print_at(3, 22, "G=GDB Remote Debug Guide", FG_CYAN, COLOR_NORMAL);
    
    // Show mouse support status
    if (panic_mouse_enabled) {
        tui_print_at(3, 23, "ESC=Exit Debug Mode    SPACE=Refresh    Mouse=Click Commands", FG_GREEN, COLOR_NORMAL);
    } else {
        tui_print_at(3, 23, "ESC=Exit Debug Mode    SPACE=Refresh Display    ?=Advanced Help", FG_GREEN, COLOR_NORMAL);
    }
}

// =============================================================================
// DETAILED ANALYSIS SCREENS
// =============================================================================

static void show_cpu_registers_detailed(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "CPU REGISTERS - COMPLETE STATE",
        "All processor registers at time of panic with detailed analysis",
        "Press any key to return to main menu",
        COLOR_NORMAL, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char hex_str[12];
    int row = cy;
    
    // General purpose registers
    tui_print_section_header(cx, row++, cw, "General Purpose Registers", FG_YELLOW, COLOR_NORMAL);
    row++;
    
    format_hex32(ctx->cpu_state.eax, hex_str);
    tui_print_table_row(cx, row++, cw, "EAX (Accumulator)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.ebx, hex_str);
    tui_print_table_row(cx, row++, cw, "EBX (Base Index)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.ecx, hex_str);
    tui_print_table_row(cx, row++, cw, "ECX (Counter)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.edx, hex_str);
    tui_print_table_row(cx, row++, cw, "EDX (Data)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.esi, hex_str);
    tui_print_table_row(cx, row++, cw, "ESI (Source Index)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.edi, hex_str);
    tui_print_table_row(cx, row++, cw, "EDI (Destination Index)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Stack and Instruction Pointers", FG_YELLOW, COLOR_NORMAL);
    row++;
    
    format_hex32(ctx->cpu_state.esp, hex_str);
    const char* esp_status = (ctx->cpu_state.esp < 0x1000 || ctx->cpu_state.esp > 0xFFFFF000) ? " [INVALID!]" : " [OK]";
    char esp_info[32];
    strcpy(esp_info, hex_str);
    strcat(esp_info, esp_status);
    tui_print_table_row(cx, row++, cw, "ESP (Stack Pointer)", esp_info, FG_CYAN, 
                       (ctx->cpu_state.esp < 0x1000 || ctx->cpu_state.esp > 0xFFFFF000) ? FG_RED : FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.ebp, hex_str);
    tui_print_table_row(cx, row++, cw, "EBP (Base Pointer)", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    format_hex32(ctx->cpu_state.eip, hex_str);
    tui_print_table_row(cx, row++, cw, "EIP (Instruction Pointer)", hex_str, FG_CYAN, FG_GREEN, COLOR_NORMAL);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Processor Flags (EFLAGS)", FG_YELLOW, COLOR_NORMAL);
    row++;
    
    format_hex32(ctx->cpu_state.eflags, hex_str);
    tui_print_table_row(cx, row++, cw, "EFLAGS Register", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    // Flag breakdown
    const char* flags[] = {"Carry", "Zero", "Sign", "Interrupt", "Direction", "Overflow"};
    int flag_bits[] = {0, 6, 7, 9, 10, 11};
    
    for (int i = 0; i < 6; i++) {
        bool flag_set = (ctx->cpu_state.eflags & (1 << flag_bits[i])) != 0;
        tui_print_table_row(cx, row++, cw, flags[i], flag_set ? "SET" : "CLEAR", 
                           FG_GRAY, flag_set ? FG_GREEN : FG_RED, COLOR_NORMAL);
    }
}

static void show_memory_analysis(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "MEMORY ANALYSIS",
        "Memory manager statistics, layout, and corruption detection",
        "Press any key to return to main menu",
        COLOR_SUCCESS, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char num_str[16];
    int row = cy;
    
    // Memory statistics if available
    memory_stats_t stats = memory_get_stats();
    if (stats.total_frames > 0) {
        tui_print_section_header(cx, row++, cw, "Physical Memory Manager", FG_YELLOW, COLOR_SUCCESS);
        row++;
        
        format_decimal(stats.total_frames, num_str);
        tui_print_table_row(cx, row++, cw, "Total Pages", num_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
        
        format_decimal(stats.used_frames, num_str);
        tui_print_table_row(cx, row++, cw, "Used Pages", num_str, FG_CYAN, FG_RED, COLOR_SUCCESS);
        
        format_decimal(stats.free_frames, num_str);
        tui_print_table_row(cx, row++, cw, "Free Pages", num_str, FG_CYAN, FG_GREEN, COLOR_SUCCESS);
        
        uint32 usage_percent = (stats.total_frames > 0) ? (stats.used_frames * 100) / stats.total_frames : 0;
        format_decimal(usage_percent, num_str);
        strcat(num_str, "%");
        tui_print_table_row(cx, row++, cw, "Memory Usage", num_str, FG_CYAN, 
                           usage_percent > 90 ? FG_RED : (usage_percent > 75 ? FG_YELLOW : FG_GREEN), COLOR_SUCCESS);
        
        row++;
        tui_print_at(cx, row++, "Memory Usage Visualization:", FG_YELLOW, COLOR_SUCCESS);
        tui_draw_progress_bar(cx, row, cw - 5, stats.used_frames, stats.total_frames, FG_WHITE, COLOR_SUCCESS);
        row += 2;
    }
    
    // Memory layout
    tui_print_section_header(cx, row++, cw, "Memory Layout", FG_YELLOW, COLOR_SUCCESS);
    row++;
    
    for (int i = 0; i < MAX_MEMORY_REGIONS; i++) {
        char addr_range[32];
        char start_hex[12], end_hex[12];
        format_hex32(g_memory_regions[i].start_addr, start_hex);
        format_hex32(g_memory_regions[i].end_addr, end_hex);
        strcpy(addr_range, start_hex);
        strcat(addr_range, " - ");
        strcat(addr_range, end_hex);
        
        tui_print_table_row(cx, row++, cw, g_memory_regions[i].name, addr_range, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    }
}

static int32 draw_stack_analysis_content(const panic_context_t* ctx, int32 scroll, int32* out_max_scroll) {
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char hex_str[12];
    int row = cy;
    
    // Stack information
    tui_print_section_header(cx, row++, cw, "Stack State", FG_YELLOW, COLOR_WARNING);
    row++;
    
    format_hex32(ctx->cpu_state.esp, hex_str);
    tui_print_table_row(cx, row++, cw, "Stack Pointer (ESP)", hex_str, FG_CYAN, FG_WHITE, COLOR_WARNING);
    
    format_hex32(ctx->cpu_state.ebp, hex_str);
    tui_print_table_row(cx, row++, cw, "Base Pointer (EBP)", hex_str, FG_CYAN, FG_WHITE, COLOR_WARNING);
    
    bool stack_valid = (ctx->cpu_state.esp >= 0x1000 && ctx->cpu_state.esp < 0xFFFFF000);
    tui_print_table_row(cx, row++, cw, "Stack Validity", stack_valid ? "VALID" : "CORRUPTED", 
                       FG_CYAN, stack_valid ? FG_GREEN : FG_RED, COLOR_WARNING);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Call Stack Backtrace", FG_YELLOW, COLOR_WARNING);
    row++;
    
    tui_print_at(cx, row++, "Frame  Address     Caller      Status", FG_CYAN, COLOR_WARNING);
    tui_print_at(cx, row++, "-----  ----------  ----------  ------", FG_GRAY, COLOR_WARNING);
    
    int list_start = row;
    int visible_rows = (cy + ch - 2) - list_start;
    if (visible_rows < 4) visible_rows = 4;
    
    bool has_manual = ctx->manual_stack_valid && ctx->manual_stack_count > 0;
    int total_rows = ctx->stack_frame_count + (has_manual ? (int)ctx->manual_stack_count + 1 : 0);
    
    int32 max_scroll = (total_rows > visible_rows) ? (total_rows - visible_rows) : 0;
    if (scroll < 0) scroll = 0;
    if (scroll > max_scroll) scroll = max_scroll;
    if (out_max_scroll) *out_max_scroll = max_scroll;
    
    int displayed = 0;
    for (int idx = scroll; idx < total_rows && displayed < visible_rows; idx++) {
        if (idx < (int)ctx->stack_frame_count) {
            char frame_num[8], addr_str[12], caller_str[12];
            format_decimal(idx, frame_num);
            format_hex32(ctx->stack_trace[idx].address, addr_str);
            format_hex32(ctx->stack_trace[idx].caller, caller_str);
            char line[80];
            strcpy(line, "  ");
            strcat(line, frame_num);
            strcat(line, "    ");
            strcat(line, addr_str);
            strcat(line, "  ");
            strcat(line, caller_str);
            strcat(line, "  ");
            strcat(line, ctx->stack_trace[idx].valid ? "OK" : "INVALID");
            tui_print_at(cx, row++, line, ctx->stack_trace[idx].valid ? FG_WHITE : FG_RED, COLOR_WARNING);
        } else if (has_manual && idx == (int)ctx->stack_frame_count) {
            tui_print_at(cx, row++, "-- Recorded Stack Snapshot --", FG_YELLOW, COLOR_WARNING);
        } else if (has_manual) {
            uint32 manual_index = idx - ctx->stack_frame_count - 1;
            if (manual_index < ctx->manual_stack_count) {
                char entry_label[32];
                char entry_value[12];
                strcpy(entry_label, "Entry ");
                char idx_str[8];
                format_decimal(manual_index, idx_str);
                strcat(entry_label, idx_str);
                format_hex32(ctx->manual_stack[manual_index], entry_value);
                tui_print_table_row(cx, row++, cw, entry_label, entry_value, FG_CYAN, FG_WHITE, COLOR_WARNING);
            }
        }
        displayed++;
    }
    
    if (total_rows == 0) {
        tui_print_at(cx, row, "No valid stack frames found - stack may be corrupted", FG_RED, COLOR_WARNING);
    } else if (max_scroll > 0) {
        tui_draw_scrollbar(cx + cw - 2, list_start, visible_rows, visible_rows, total_rows, scroll, FG_YELLOW, COLOR_WARNING);
    }
    
    return scroll;
}

static void show_stack_analysis(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "STACK ANALYSIS",
        "Complete call stack unwinding and stack memory inspection",
        "Use W/S to scroll, other key to return",
        COLOR_WARNING, FG_WHITE, FG_YELLOW
    };
    
    int32 scroll = panic_scroll_get(PANIC_SCREEN_STACK);
    bool viewing = true;
    while (viewing) {
        tui_fullscreen_clear(&theme);
        tui_fullscreen_header(&theme);
        tui_fullscreen_footer(&theme);
        
        int32 max_scroll = 0;
        scroll = draw_stack_analysis_content(ctx, scroll, &max_scroll);
        
        char key = wait_for_key();
        if (key == 'w' || key == 'W') {
            if (scroll > 0) scroll--;
        } else if (key == 's' || key == 'S') {
            if (scroll < max_scroll) scroll++;
        } else {
            viewing = false;
        }
    }
    
    panic_scroll_set(PANIC_SCREEN_STACK, scroll);
}

static void show_advanced_analysis(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "ADVANCED ANALYSIS",
        "Error classification, recovery suggestions, and system impact assessment",
        "Press any key to return to main menu",
        COLOR_WARNING, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    int row = cy;
    
    tui_print_section_header(cx, row++, cw, "Panic Classification", FG_YELLOW, COLOR_WARNING);
    row++;
    
    tui_print_table_row(cx, row++, cw, "Error Type", get_panic_type_name(ctx->type), FG_CYAN, FG_WHITE, COLOR_WARNING);
    tui_print_table_row(cx, row++, cw, "Severity", ctx->type == PANIC_TYPE_DOUBLE_FAULT ? "CRITICAL" : "HIGH", 
                       FG_CYAN, ctx->type == PANIC_TYPE_DOUBLE_FAULT ? FG_RED : FG_YELLOW, COLOR_WARNING);
    tui_print_table_row(cx, row++, cw, "Recoverable", ctx->recoverable ? "YES" : "NO", 
                       FG_CYAN, ctx->recoverable ? FG_GREEN : FG_RED, COLOR_WARNING);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Probable Causes", FG_YELLOW, COLOR_WARNING);
    row++;
    
    switch (ctx->type) {
        case PANIC_TYPE_PAGE_FAULT:
            tui_print_at(cx, row++, "• Accessing unmapped memory region", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Invalid pointer dereference", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Stack overflow or underflow", FG_WHITE, COLOR_WARNING);
            break;
        case PANIC_TYPE_MEMORY_CORRUPTION:
            tui_print_at(cx, row++, "• Buffer overflow or underflow", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Use after free vulnerability", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Double free corruption", FG_WHITE, COLOR_WARNING);
            break;
        case PANIC_TYPE_STACK_OVERFLOW:
            tui_print_at(cx, row++, "• Infinite recursion", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Large local variable allocation", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Stack corruption from buffer overflow", FG_WHITE, COLOR_WARNING);
            break;
        default:
            tui_print_at(cx, row++, "• General kernel error condition", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Hardware failure or incompatibility", FG_WHITE, COLOR_WARNING);
            tui_print_at(cx, row++, "• Driver malfunction", FG_WHITE, COLOR_WARNING);
            break;
    }
    
    row++;
    tui_print_section_header(cx, row++, cw, "Recovery Suggestions", FG_YELLOW, COLOR_WARNING);
    row++;
    tui_print_at(cx, row++, "1. Check all pointer arithmetic and array bounds", FG_GREEN, COLOR_WARNING);
    tui_print_at(cx, row++, "2. Verify memory allocation/deallocation pairs", FG_GREEN, COLOR_WARNING);
    tui_print_at(cx, row++, "3. Review recent code changes for bugs", FG_GREEN, COLOR_WARNING);
    tui_print_at(cx, row++, "4. Test with memory debugging tools", FG_GREEN, COLOR_WARNING);
}

static int32 clamp_memory_scroll(int32 scroll) {
    if (scroll < -PANIC_MEMORY_SCROLL_RANGE) return -PANIC_MEMORY_SCROLL_RANGE;
    if (scroll > PANIC_MEMORY_SCROLL_RANGE) return PANIC_MEMORY_SCROLL_RANGE;
    return scroll;
}

static int32 draw_memory_dump_content(const panic_context_t* ctx, int32 scroll) {
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    int row = cy;
    scroll = clamp_memory_scroll(scroll);
    
    tui_print_section_header(cx, row++, cw, "Memory Around Fault Address", FG_YELLOW, COLOR_INFO);
    row++;
    
    uint32 fault_addr = ctx->cpu_state.eip;
    uint32 base_fault = fault_addr > 32 ? fault_addr - 32 : 0;
    int64 adjusted_fault = (int64)base_fault + ((int64)scroll * PANIC_MEMORY_STEP_BYTES);
    if (adjusted_fault < 0) adjusted_fault = 0;
    uint32 fault_start = (uint32)adjusted_fault;
    const char* fault_reason = NULL;
    if (panic_can_read_range(fault_start, 64, &fault_reason)) {
        tui_draw_hex_viewer(cx, row, cw - 4, 8, (void*)fault_start, fault_start, FG_WHITE, COLOR_INFO);
        row += 8;
    } else {
        tui_print_at(cx, row++, "Cannot display faulting instruction bytes:", FG_RED, COLOR_INFO);
        tui_print_at(cx, row++, fault_reason ? fault_reason : "Unknown mapping failure", FG_RED, COLOR_INFO);
    }
    
    row++;
    tui_print_section_header(cx, row++, cw, "Stack Memory", FG_YELLOW, COLOR_INFO);
    row++;
    
    uint32 stack_addr = ctx->cpu_state.esp;
    uint32 base_stack = stack_addr > 16 ? stack_addr - 16 : 0;
    int64 adjusted_stack = (int64)base_stack + ((int64)scroll * PANIC_MEMORY_STEP_BYTES);
    if (adjusted_stack < 0) adjusted_stack = 0;
    uint32 stack_start = (uint32)adjusted_stack;
    const char* stack_reason = NULL;
    if (panic_can_read_range(stack_start, 64, &stack_reason)) {
        tui_draw_hex_viewer(cx, row, cw - 4, 6, (void*)stack_start, stack_start, FG_WHITE, COLOR_INFO);
        row += 6;
    } else {
        tui_print_at(cx, row++, "Cannot display stack memory:", FG_RED, COLOR_INFO);
        tui_print_at(cx, row++, stack_reason ? stack_reason : "Unknown mapping failure", FG_RED, COLOR_INFO);
    }
    
    char offset_label[32];
    strcpy(offset_label, "Offset (");
    strcat(offset_label, scroll >= 0 ? "+" : "-");
    int32 abs_offset = scroll >= 0 ? scroll : -scroll;
    char offset_num[16];
    format_decimal(abs_offset * PANIC_MEMORY_STEP_BYTES, offset_num);
    strcat(offset_label, offset_num);
    strcat(offset_label, " bytes)");
    tui_print_at(cx, row + 1, offset_label, FG_CYAN, COLOR_INFO);
    
    int scroll_height = ch - 6;
    int total_positions = PANIC_MEMORY_SCROLL_RANGE * 2 + 1;
    int position = scroll + PANIC_MEMORY_SCROLL_RANGE;
    tui_draw_scrollbar(cx + cw - 2, cy + 2, scroll_height, 1, total_positions, position, FG_YELLOW, COLOR_INFO);
    
    return scroll;
}

static void show_memory_dump(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "MEMORY DUMP",
        "Raw memory viewer around fault address and stack pointer",
        "Use W/S to scroll, other key to return",
        COLOR_INFO, FG_WHITE, FG_YELLOW
    };
    
    int32 scroll = panic_scroll_get(PANIC_SCREEN_MEMORY);
    bool viewing = true;
    while (viewing) {
        tui_fullscreen_clear(&theme);
        tui_fullscreen_header(&theme);
        tui_fullscreen_footer(&theme);
        
        scroll = draw_memory_dump_content(ctx, scroll);
        char key = wait_for_key();
        if (key == 'w' || key == 'W') {
            if (scroll > -PANIC_MEMORY_SCROLL_RANGE) scroll--;
        } else if (key == 's' || key == 'S') {
            if (scroll < PANIC_MEMORY_SCROLL_RANGE) scroll++;
        } else {
            viewing = false;
        }
    }
    
    panic_scroll_set(PANIC_SCREEN_MEMORY, scroll);
}

static void show_system_info(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "SYSTEM INFORMATION",
        "Hardware configuration and kernel state information",
        "Press any key to return to main menu",
        COLOR_SUCCESS, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char hex_str[12];
    int row = cy;
    
    tui_print_section_header(cx, row++, cw, "Kernel Information", FG_YELLOW, COLOR_SUCCESS);
    row++;
    
    tui_print_table_row(cx, row++, cw, "OS Name", "Forest OS", FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    tui_print_table_row(cx, row++, cw, "Kernel Version", PANIC_VERSION, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    
    char panic_count_str[16];
    format_decimal(g_panic_count, panic_count_str);
    tui_print_table_row(cx, row++, cw, "Panic Count", panic_count_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Hardware Configuration", FG_YELLOW, COLOR_SUCCESS);
    row++;
    
    const cpuid_info_t* cpu = hardware_get_cpuid_info();
    const char* vendor = cpu->cpuid_supported ? cpu->vendor_id : "Unavailable";
    const char* brand = cpu->cpuid_supported ? cpu->brand_string : "Unknown";
    tui_print_table_row(cx, row++, cw, "CPU Vendor", vendor, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    tui_print_table_row(cx, row++, cw, "CPU Brand", brand, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    
    if (cpu->cpuid_supported) {
        char sig_hex[12];
        format_hex32(cpu->signature, sig_hex);
        tui_print_table_row(cx, row++, cw, "CPU Signature", sig_hex, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
        
        char logical_str[12];
        format_decimal(cpu->logical_processor_count, logical_str);
        tui_print_table_row(cx, row++, cw, "Logical CPUs", logical_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
        
        tui_print_table_row(cx, row++, cw, "Hypervisor Present", 
                            cpu->hypervisor_present ? "Yes" : "No", FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    }
    
    tui_print_table_row(cx, row++, cw, "CPU Features", hardware_get_feature_summary(), 
                        FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    
    // Memory size estimation
    memory_stats_t stats = memory_get_stats();
    if (stats.total_memory_kb > 0) {
        uint32 total_mb = stats.total_memory_kb / 1024;
        char mem_str[16];
        format_decimal(total_mb, mem_str);
        strcat(mem_str, " MB");
        tui_print_table_row(cx, row++, cw, "Total Memory", mem_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    }
    
    // Boot method
    tui_print_table_row(cx, row++, cw, "Boot Method", "Multiboot", FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Current State", FG_YELLOW, COLOR_SUCCESS);
    row++;
    
    format_hex32(ctx->cpu_state.eip, hex_str);
    tui_print_table_row(cx, row++, cw, "Executing At", hex_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    
    const char* privilege = (ctx->cpu_state.cs & 3) == 0 ? "KERNEL" : "USER";
    tui_print_table_row(cx, row++, cw, "Privilege Level", privilege, FG_CYAN, FG_GREEN, COLOR_SUCCESS);
}

static void show_interrupt_state(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "INTERRUPT STATE",
        "Interrupt controller status and exception information",
        "Press any key to return to main menu",
        COLOR_WARNING, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    int row = cy;
    
    tui_print_section_header(cx, row++, cw, "Interrupt Flag State", FG_YELLOW, COLOR_WARNING);
    row++;
    
    bool interrupts_enabled = (ctx->cpu_state.eflags & (1 << 9)) != 0;
    tui_print_table_row(cx, row++, cw, "Interrupt Flag", interrupts_enabled ? "ENABLED" : "DISABLED", 
                       FG_CYAN, interrupts_enabled ? FG_GREEN : FG_RED, COLOR_WARNING);
    
    bool trap_flag = (ctx->cpu_state.eflags & (1 << 8)) != 0;
    tui_print_table_row(cx, row++, cw, "Trap Flag", trap_flag ? "SET" : "CLEAR", 
                       FG_CYAN, trap_flag ? FG_YELLOW : FG_WHITE, COLOR_WARNING);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Exception Information", FG_YELLOW, COLOR_WARNING);
    row++;
    
    if (ctx->cpu_state.cr2 != 0) {
        char hex_str[12];
        format_hex32(ctx->cpu_state.cr2, hex_str);
        tui_print_table_row(cx, row++, cw, "Page Fault Address", hex_str, FG_CYAN, FG_RED, COLOR_WARNING);
        
        // Decode page fault error code if available
        uint32 error = ctx->error_code;
        tui_print_table_row(cx, row++, cw, "Page Present", (error & 1) ? "YES" : "NO", 
                           FG_CYAN, (error & 1) ? FG_GREEN : FG_RED, COLOR_WARNING);
        tui_print_table_row(cx, row++, cw, "Write Access", (error & 2) ? "YES" : "NO", 
                           FG_CYAN, FG_WHITE, COLOR_WARNING);
        tui_print_table_row(cx, row++, cw, "User Mode", (error & 4) ? "YES" : "NO", 
                           FG_CYAN, FG_WHITE, COLOR_WARNING);
    }
    
    row++;
    tui_print_section_header(cx, row++, cw, "PIC Status", FG_YELLOW, COLOR_WARNING);
    row++;
    
    // Read PIC status if safe
    tui_print_at(cx, row++, "Master PIC: Status not safely readable during panic", FG_GRAY, COLOR_WARNING);
    tui_print_at(cx, row++, "Slave PIC:  Status not safely readable during panic", FG_GRAY, COLOR_WARNING);
}

static void show_control_registers(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "CONTROL REGISTERS",
        "CR0-CR4 control registers and segment selectors",
        "Press any key to return to main menu",
        COLOR_INFO, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char hex_str[12];
    int row = cy;
    
    tui_print_section_header(cx, row++, cw, "Control Registers", FG_YELLOW, COLOR_INFO);
    row++;
    
    format_hex32(ctx->cpu_state.cr0, hex_str);
    tui_print_table_row(cx, row++, cw, "CR0 (System Control)", hex_str, FG_CYAN, FG_WHITE, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.cr2, hex_str);
    tui_print_table_row(cx, row++, cw, "CR2 (Page Fault Address)", hex_str, FG_CYAN, 
                       ctx->cpu_state.cr2 ? FG_RED : FG_GRAY, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.cr3, hex_str);
    tui_print_table_row(cx, row++, cw, "CR3 (Page Directory)", hex_str, FG_CYAN, FG_WHITE, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.cr4, hex_str);
    tui_print_table_row(cx, row++, cw, "CR4 (Extended Features)", hex_str, FG_CYAN, FG_WHITE, COLOR_INFO);
    
    row++;
    tui_print_section_header(cx, row++, cw, "CR0 Flag Breakdown", FG_YELLOW, COLOR_INFO);
    row++;
    
    tui_print_table_row(cx, row++, cw, "Protection Enable", (ctx->cpu_state.cr0 & 1) ? "ON" : "OFF", 
                       FG_GRAY, (ctx->cpu_state.cr0 & 1) ? FG_GREEN : FG_RED, COLOR_INFO);
    tui_print_table_row(cx, row++, cw, "Paging", (ctx->cpu_state.cr0 & (1 << 31)) ? "ON" : "OFF", 
                       FG_GRAY, (ctx->cpu_state.cr0 & (1 << 31)) ? FG_GREEN : FG_RED, COLOR_INFO);
    tui_print_table_row(cx, row++, cw, "Write Protect", (ctx->cpu_state.cr0 & (1 << 16)) ? "ON" : "OFF", 
                       FG_GRAY, (ctx->cpu_state.cr0 & (1 << 16)) ? FG_GREEN : FG_RED, COLOR_INFO);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Segment Selectors", FG_YELLOW, COLOR_INFO);
    row++;
    
    char seg_str[8];
    format_hex32(ctx->cpu_state.cs, seg_str);
    seg_str[6] = '\0'; // Truncate to 16-bit
    tui_print_table_row(cx, row++, cw, "Code Segment (CS)", seg_str, FG_CYAN, FG_WHITE, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.ds, seg_str);
    seg_str[6] = '\0';
    tui_print_table_row(cx, row++, cw, "Data Segment (DS)", seg_str, FG_CYAN, FG_WHITE, COLOR_INFO);
    
    format_hex32(ctx->cpu_state.ss, seg_str);
    seg_str[6] = '\0';
    tui_print_table_row(cx, row++, cw, "Stack Segment (SS)", seg_str, FG_CYAN, FG_WHITE, COLOR_INFO);
}

static void show_cpu_flags_detailed(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "CPU FLAGS DETAIL",
        "Complete EFLAGS register analysis and interpretation",
        "Press any key to return to main menu",
        COLOR_NORMAL, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char hex_str[12];
    int row = cy;
    
    format_hex32(ctx->cpu_state.eflags, hex_str);
    tui_print_section_header(cx, row++, cw, "EFLAGS Register", FG_YELLOW, COLOR_NORMAL);
    row++;
    tui_print_table_row(cx, row++, cw, "Raw Value", hex_str, FG_CYAN, FG_WHITE, COLOR_NORMAL);
    
    row++;
    tui_print_section_header(cx, row++, cw, "Status Flags", FG_YELLOW, COLOR_NORMAL);
    row++;
    
    struct { const char* name; int bit; const char* desc; } flags[] = {
        {"Carry Flag (CF)", 0, "Arithmetic carry/borrow"},
        {"Parity Flag (PF)", 2, "Even number of 1 bits in result"},
        {"Auxiliary Carry (AF)", 4, "Carry from bit 3 to bit 4"},
        {"Zero Flag (ZF)", 6, "Result of operation was zero"},
        {"Sign Flag (SF)", 7, "Result was negative"},
        {"Trap Flag (TF)", 8, "Single-step debugging mode"},
        {"Interrupt Flag (IF)", 9, "Interrupts enabled/disabled"},
        {"Direction Flag (DF)", 10, "String operation direction"},
        {"Overflow Flag (OF)", 11, "Signed arithmetic overflow"},
        {"IOPL", 12, "I/O privilege level (bits 12-13)"},
        {"Nested Task (NT)", 14, "Nested task flag"},
        {"Resume Flag (RF)", 16, "Resume from debug exception"},
        {"Virtual Mode (VM)", 17, "Virtual 8086 mode"},
        {"Alignment Check (AC)", 18, "Alignment check enabled"},
        {"Virtual Interrupt (VIF)", 19, "Virtual interrupt flag"},
        {"Virtual Interrupt Pending (VIP)", 20, "Virtual interrupt pending"},
        {"ID Flag", 21, "CPUID instruction available"}
    };
    
    for (int i = 0; i < 17 && row < cy + ch - 2; i++) {
        bool flag_set = (ctx->cpu_state.eflags & (1 << flags[i].bit)) != 0;
        tui_print_table_row(cx, row++, cw, flags[i].name, flag_set ? "SET" : "CLEAR", 
                           FG_GRAY, flag_set ? FG_GREEN : FG_RED, COLOR_NORMAL);
    }
}

static void show_virtual_memory(const panic_context_t* ctx) {
    tui_fullscreen_theme_t theme = {
        "VIRTUAL MEMORY",
        "Page table analysis and virtual memory management status",
        "Press any key to return to main menu",
        COLOR_SUCCESS, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    
    char hex_str[12];
    int row = cy;
    
    tui_print_section_header(cx, row++, cw, "Virtual Memory Manager", FG_YELLOW, COLOR_SUCCESS);
    row++;
    
    bool paging_enabled = (ctx->cpu_state.cr0 & (1 << 31)) != 0;
    tui_print_table_row(cx, row++, cw, "Paging Status", paging_enabled ? "ENABLED" : "DISABLED", 
                       FG_CYAN, paging_enabled ? FG_GREEN : FG_RED, COLOR_SUCCESS);
    
    if (paging_enabled) {
        format_hex32(ctx->cpu_state.cr3, hex_str);
        tui_print_table_row(cx, row++, cw, "Page Directory", hex_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
        
        // Analyze fault address if present
        if (ctx->cpu_state.cr2 != 0) {
            row++;
            tui_print_section_header(cx, row++, cw, "Page Fault Analysis", FG_YELLOW, COLOR_SUCCESS);
            row++;
            
            format_hex32(ctx->cpu_state.cr2, hex_str);
            tui_print_table_row(cx, row++, cw, "Fault Address", hex_str, FG_CYAN, FG_RED, COLOR_SUCCESS);
            
            uint32 pde_index = (ctx->cpu_state.cr2 >> 22) & 0x3FF;
            uint32 pte_index = (ctx->cpu_state.cr2 >> 12) & 0x3FF;
            
            char index_str[16];
            format_decimal(pde_index, index_str);
            tui_print_table_row(cx, row++, cw, "Page Directory Entry", index_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
            
            format_decimal(pte_index, index_str);
            tui_print_table_row(cx, row++, cw, "Page Table Entry", index_str, FG_CYAN, FG_WHITE, COLOR_SUCCESS);
        }
    }
    
    row++;
    tui_print_section_header(cx, row++, cw, "Address Space Layout", FG_YELLOW, COLOR_SUCCESS);
    row++;
    
    tui_print_table_row(cx, row++, cw, "User Space", "0x00000000 - 0xBFFFFFFF", FG_CYAN, FG_WHITE, COLOR_SUCCESS);
    tui_print_table_row(cx, row++, cw, "Kernel Space", "0xC0000000 - 0xFFFFFFFF", FG_CYAN, FG_WHITE, COLOR_SUCCESS);
        tui_print_table_row(cx, row++, cw, "Current EIP Region", 
                       ctx->cpu_state.eip >= 0xC0000000 ? "KERNEL" : "USER", 
                       FG_CYAN, ctx->cpu_state.eip >= 0xC0000000 ? FG_GREEN : FG_YELLOW, COLOR_SUCCESS);
}

static void show_gdb_debugger_help(void) {
    tui_fullscreen_theme_t theme = {
        "QEMU / GDB REMOTE DEBUG GUIDE",
        "Reference for attaching debuggers during Forest OS panics",
        "Press any key to return to main menu",
        COLOR_INFO, FG_WHITE, FG_YELLOW
    };
    
    tui_fullscreen_clear(&theme);
    tui_fullscreen_header(&theme);
    tui_fullscreen_footer(&theme);
    
    int cx, cy, cw, ch;
    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
    int row = cy;
    
    tui_print_section_header(cx, row++, cw, "Connecting to QEMU's GDB Stub", FG_YELLOW, COLOR_INFO);
    row++;
    
    const char* connect_steps[] = {
        "1. Launch QEMU with -s -S so it opens TCP port 1234 and pauses at reset.",
        "2. Start GDB with symbols (e.g. `gdb iso/boot/kernel.bin`).",
        "3. Run: set arch i386:x86-64:intel",
        "4. Then: target remote localhost:1234",
        "5. If symbols are stripped: symbol-file iso/boot/kernel.bin",
        "6. Continue / hbreak to control execution from there."
    };
    
    for (uint32 i = 0; i < sizeof(connect_steps)/sizeof(connect_steps[0]); i++) {
        tui_print_at(cx + 2, row++, connect_steps[i], FG_WHITE, COLOR_INFO);
    }
    
    row++;
    tui_print_section_header(cx, row++, cw, "Handling Long Mode Register Layout", FG_YELLOW, COLOR_INFO);
    row++;
    
    const char* long_mode_desc[] = {
        "After Forest OS enables long mode QEMU exposes 64-bit registers.",
        "GDB initially stays in 32-bit mode and reports: Remote 'g' packet reply is too long.",
        "Solution: reconnect with the generic x86-64 architecture once the breakpoint hits:"
    };
    
    for (uint32 i = 0; i < sizeof(long_mode_desc)/sizeof(long_mode_desc[0]); i++) {
        tui_print_at(cx + 2, row++, long_mode_desc[i], FG_WHITE, COLOR_INFO);
    }
    
    tui_print_at(cx + 4, row++, "(gdb) disconnect", FG_CYAN, COLOR_INFO);
    tui_print_at(cx + 4, row++, "(gdb) set arch i386:x86-64", FG_CYAN, COLOR_INFO);
    tui_print_at(cx + 4, row++, "(gdb) target remote localhost:1234", FG_CYAN, COLOR_INFO);
    tui_print_at(cx + 2, row++, "Using i386:x86-64:intel for the first attach keeps early breakpoints valid.", FG_WHITE, COLOR_INFO);
    
    row += 2;
    tui_print_section_header(cx, row++, cw, "Alternate Helpers", FG_YELLOW, COLOR_INFO);
    row++;
    
    const char* helper_text[] = {
        "- Patch GDB (remote.c) to resize oversized 'g' packets automatically (OSDev link).",
        "- Patch QEMU gdbstub.c to always report 64-bit registers (breaks pure 32-bit guests).",
        "- Simple workaround: only connect after long mode is enabled (scripted delay).",
        "- Sample script: setsid qemu-system-x86_64 -s -S ... & sleep 5 && gdb kernel.bin -x qemudbg."
    };
    
    for (uint32 i = 0; i < sizeof(helper_text)/sizeof(helper_text[0]); i++) {
        tui_print_at(cx + 2, row++, helper_text[i], FG_WHITE, COLOR_INFO);
    }
    
    row += 2;
    tui_print_section_header(cx, row++, cw, "Quick Tips", FG_YELLOW, COLOR_INFO);
    row++;
    
    const char* tips[] = {
        "- Keep debug info with `objcopy --only-keep-debug` to load symbols quickly.",
        "- Prefer hbreak for low-level breakpoints to avoid skipped traps.",
        "- Combine serial logging (-serial file:serial.log) with GDB for extra context.",
        "- Panic-triggered INT3 instructions sync immediately with a connected debugger."
    };
    
    for (uint32 i = 0; i < sizeof(tips)/sizeof(tips[0]); i++) {
        tui_print_at(cx + 2, row++, tips[i], FG_WHITE, COLOR_INFO);
    }
}

// =============================================================================
// INTERACTIVE COMMAND PROCESSOR
// =============================================================================

// Mouse interaction areas for panic interface
typedef struct {
    int x, y, width, height;
    char command;
    const char* label;
} panic_clickable_area_t;

#if PANIC_ENABLE_MOUSE
static const panic_clickable_area_t clickable_areas[] = {
    {3, 20, 2, 1, 'r', "R=CPU Registers"},
    {19, 20, 2, 1, 'm', "M=Memory"},
    {30, 20, 2, 1, 's', "S=Stack"},
    {40, 20, 2, 1, 'h', "H=Help"},
    {50, 20, 2, 1, 'a', "A=Analysis"},
    {63, 20, 2, 1, 'd', "D=Dump"},
    {72, 20, 2, 1, 'q', "Q=Info"},
    {3, 21, 2, 1, 't', "T=System Info"},
    {17, 21, 2, 1, 'i', "I=Interrupts"},
    {31, 21, 2, 1, 'c', "C=Control Regs"},
    {48, 21, 2, 1, 'f', "F=Flags"},
    {58, 21, 2, 1, 'v', "V=Virtual Memory"},
    {3, 22, 2, 1, 'g', "G=GDB Guide"}
};

static char check_mouse_click(int mouse_x, int mouse_y) {
    for (int i = 0; i < 12; i++) {
        if (tui_is_point_in_rect(mouse_x, mouse_y, 
                                clickable_areas[i].x, clickable_areas[i].y,
                                clickable_areas[i].width, clickable_areas[i].height)) {
            return clickable_areas[i].command;
        }
    }
    return 0;  // No command area clicked
}
#endif

static void process_debug_commands(const panic_context_t* ctx) {
    while (true) {
        draw_panic_banner(ctx);

        char cmd = 0;
#if PANIC_ENABLE_MOUSE
        if (panic_mouse_enabled) {
            tui_draw_status_bar(24, "WAITING FOR INPUT", "Click command or type key, ESC to halt", 
                               FG_WHITE, COLOR_NORMAL);
        } else {
            tui_draw_status_bar(24, "WAITING FOR COMMAND", "Type a command letter or ESC to halt", 
                               FG_WHITE, COLOR_NORMAL);
        }

        bool mouse_clicked = false;
        int mouse_x = -1, mouse_y = -1;
        cmd = wait_for_key_or_mouse(&mouse_clicked, &mouse_x, &mouse_y);

        if (mouse_clicked && cmd == 0) {
            cmd = check_mouse_click(mouse_x, mouse_y);
            if (cmd == 0) {
                continue;
            }
        }
#else
        tui_draw_status_bar(24, "WAITING FOR COMMAND", "Type a command letter or ESC to halt", 
                           FG_WHITE, COLOR_NORMAL);
        cmd = wait_for_key();
#endif
        
        switch (cmd) {
            case 'r': case 'R':
                show_cpu_registers_detailed(ctx);
                wait_for_key();
                break;
                
            case 'm': case 'M':
                show_memory_analysis(ctx);
                wait_for_key();
                break;
                
            case 's': case 'S':
                show_stack_analysis(ctx);
                wait_for_key();
                break;
                
            case 'a': case 'A':
                show_advanced_analysis(ctx);
                wait_for_key();
                break;
                
            case 'd': case 'D':
                show_memory_dump(ctx);
                wait_for_key();
                break;
                
            case 't': case 'T':
                show_system_info(ctx);
                wait_for_key();
                break;
                
            case 'i': case 'I':
                show_interrupt_state(ctx);
                wait_for_key();
                break;
                
            case 'c': case 'C':
                show_control_registers(ctx);
                wait_for_key();
                break;
                
            case 'f': case 'F':
                show_cpu_flags_detailed(ctx);
                wait_for_key();
                break;
                
            case 'v': case 'V':
                show_virtual_memory(ctx);
                wait_for_key();
                break;
            
            case 'g': case 'G':
                show_gdb_debugger_help();
                wait_for_key();
                break;
                
            case 'q': case 'Q':
                show_system_info(ctx);
                wait_for_key();
                break;

            case 'h': case 'H': case '?':
                // Show comprehensive help
                {
                    tui_fullscreen_theme_t theme = {
                        "PANIC DEBUGGER HELP",
                        "Complete reference for all debugging commands",
                        "Press any key to return to main menu",
                        COLOR_INFO, FG_WHITE, FG_YELLOW
                    };
                    
                    tui_fullscreen_clear(&theme);
                    tui_fullscreen_header(&theme);
                    tui_fullscreen_footer(&theme);
                    
                    int cx, cy, cw, ch;
                    tui_fullscreen_content_area(&cx, &cy, &cw, &ch);
                    
                    int row = cy;
                    tui_print_section_header(cx, row++, cw, "Available Commands", FG_YELLOW, COLOR_INFO);
                    row++;
                    
                    const char* help_items[] = {
                        "R", "CPU Registers - Complete processor state analysis",
                        "M", "Memory Analysis - PMM statistics and memory layout",
                        "S", "Stack Analysis - Call stack and stack memory inspection",
                        "A", "Advanced Analysis - Error classification and suggestions",
                        "D", "Memory Dump - Raw memory content viewer",
                        "T", "System Info - Hardware and kernel information",
                        "I", "Interrupt State - Interrupt and exception analysis",
                        "C", "Control Registers - CR0, CR2, CR3, CR4 and segments",
                        "F", "CPU Flags - Detailed EFLAGS analysis",
                        "V", "Virtual Memory - VMM status and page tables",
                        "Q", "System Info - Quick system information display",
                        "H/?", "Help - Show this help screen",
                        "ESC", "Exit Debug Mode - Halt system (use with caution)",
                        "SPACE", "Refresh Display - Redraw main interface"
                    };
                    
                    for (int i = 0; i < 28; i += 2) {
                        tui_print_table_row(cx, row++, cw, help_items[i], help_items[i+1], 
                                           FG_CYAN, FG_WHITE, COLOR_INFO);
                    }
                }
                wait_for_key();
                break;
                
            case ' ':
                // Refresh display
                break;
                
            case 27: // ESC
                tui_draw_window(20, 10, 40, 6, "CONFIRM HALT", FG_WHITE, COLOR_ERROR);
                tui_print_at(22, 12, "Are you sure you want to halt?", FG_WHITE, COLOR_ERROR);
                tui_print_at(22, 13, "Y=Yes, N=No", FG_YELLOW, COLOR_ERROR);
                
                char confirm = wait_for_key();
                if (confirm == 'y' || confirm == 'Y') {
                    clearScreen();
                    tui_center_text(0, 12, 80, "FOREST OS KERNEL PANIC - SYSTEM HALTED", FG_WHITE, COLOR_ERROR);
                    tui_center_text(0, 13, 80, "Safe to power off", FG_GRAY, COLOR_ERROR);
                    __asm__ __volatile__("hlt");
                    return;
                }
                break;
                
            default:
                tui_draw_window(25, 15, 30, 5, "UNKNOWN COMMAND", FG_WHITE, COLOR_ERROR);
                tui_print_at(27, 17, "Unknown command. Press H for help.", FG_WHITE, COLOR_ERROR);
                wait_for_key();
                break;
        }
    }
}

static void kernel_panic_trigger(const char* message,
                                 const char* file,
                                 uint32 line,
                                 const char* func,
                                 const uint32* manual_stack,
                                 uint32 manual_entries) {
    // Disable interrupts immediately
    __asm__ __volatile__("cli");
    
    // Increment panic counter
    g_panic_count++;
    
    // Initialize panic context
    g_panic_context.message = message ? message : "Unknown panic";
    g_panic_context.file = file;
    g_panic_context.line = line;
    g_panic_context.function = func;
    g_panic_context.error_code = 0;
    g_panic_context.recoverable = false;
    g_panic_context.manual_stack_valid = false;
    g_panic_context.manual_stack_count = 0;
    for (int i = 0; i < PANIC_SCREEN_MAX; i++) {
        g_panic_context.scroll_offsets[i] = 0;
    }
    
    if (manual_stack && manual_entries > 0) {
        uint32 count = manual_entries > MAX_STACK_FRAMES ? MAX_STACK_FRAMES : manual_entries;
        for (uint32 i = 0; i < count; i++) {
            g_panic_context.manual_stack[i] = manual_stack[i];
        }
        g_panic_context.manual_stack_count = count;
        g_panic_context.manual_stack_valid = true;
    }
    
    // Capture CPU state atomically
    capture_cpu_state_atomic(&g_panic_context.cpu_state);
    
    // Classify panic type
    g_panic_context.type = classify_panic(message, 0, &g_panic_context.cpu_state);
    
    // Unwind stack
    g_panic_context.stack_frame_count = unwind_stack(&g_panic_context.cpu_state, 
                                                     g_panic_context.stack_trace, 
                                                     MAX_STACK_FRAMES);
    
    // Mark as initialized
    g_panic_initialized = true;

#if PANIC_DEBUGLOG_ENABLED
    panic_debuglog_emit(&g_panic_context);
#endif
    
#if PANIC_ENABLE_MOUSE
    init_panic_mouse_support();
#endif
    
    // Enter interactive debugger
    process_debug_commands(&g_panic_context);
}

// =============================================================================
// MAIN PANIC HANDLER IMPLEMENTATION
// =============================================================================

void kernel_panic_annotated(const char* message, const char* file, uint32 line, const char* func) {
    kernel_panic_trigger(message, file, line, func, NULL, 0);
}

void kernel_panic_with_stack(const char* message, const uint32* stack_entries, uint32 entry_count) {
    kernel_panic_trigger(message, "assembly/boot.asm", 0, "early_boot", stack_entries, entry_count);
}

#undef kernel_panic
void kernel_panic(const char* message) {
    kernel_panic_trigger(message, NULL, 0, NULL, NULL, 0);
}

// =============================================================================
// PANIC HANDLER INFORMATION INTERFACE
// =============================================================================

uint32 panic_get_count(void) {
    return g_panic_count;
}

bool panic_is_active(void) {
    return g_panic_initialized;
}

const char* panic_get_last_message(void) {
    return g_panic_initialized ? g_panic_context.message : NULL;
}

void kernel_panic_set_scroll(panic_screen_id_t screen, int32 offset) {
    panic_scroll_set(screen, offset);
}
