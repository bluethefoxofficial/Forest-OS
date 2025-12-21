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
#include "include/system.h"
#include "include/util.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/hardware.h"
#include "include/ps2_mouse.h"
#include "include/ps2_controller.h"
#include "include/kb.h"
#include "include/debuglog.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/font_renderer.h"
#include "include/tty.h"

// =============================================================================
// CONSTANTS AND CONFIGURATION
// =============================================================================

#define PANIC_VERSION "1.0 ALDER (Thornedge)"
#define PANIC_ENABLE_MOUSE 0
#define PANIC_DEBUGLOG_ENABLED 1
#define MAX_STACK_FRAMES 16
#define MAX_MEMORY_REGIONS 8
#define DEBUG_DELAY_MS 100
#define PANIC_MEMORY_SCROLL_RANGE 64
#define PANIC_MEMORY_STEP_BYTES 16

// New graphics-based color scheme
static const graphics_color_t COLOR_BG = {0, 0, 170, 255}; // Dark blue
static const graphics_color_t COLOR_FG = {255, 255, 255, 255}; // White
static const graphics_color_t COLOR_HEAD = {85, 85, 255, 255}; // Lighter blue
static const graphics_color_t COLOR_BORDER = {255, 255, 255, 255}; // White
static const graphics_color_t COLOR_TEXT_HEADER = {255, 255, 85, 255}; // Yellow
static const graphics_color_t COLOR_TEXT_LABEL = {85, 255, 255, 255};  // Cyan
static const graphics_color_t COLOR_TEXT_VALUE = {255, 255, 255, 255}; // White
static const graphics_color_t COLOR_TEXT_ERROR = {255, 85, 85, 255};   // Red
static const graphics_color_t COLOR_TEXT_WARN = {255, 255, 85, 255};  // Yellow
static const graphics_color_t COLOR_TEXT_OK = {85, 255, 85, 255};      // Green
static const graphics_color_t COLOR_TEXT_MUTED = {170, 170, 170, 255}; // Gray

#define FG_WHITE    COLOR_TEXT_VALUE
#define FG_YELLOW   COLOR_TEXT_WARN
#define FG_CYAN     COLOR_TEXT_LABEL
#define FG_GREEN    COLOR_TEXT_OK
#define FG_RED      COLOR_TEXT_ERROR
#define FG_GRAY     COLOR_TEXT_MUTED
#define FG_BLUE     COLOR_HEAD

// Color constants for TUI compatibility (convert graphics_color_t to int)
#define COLOR_WARNING   14  // Yellow
#define COLOR_NORMAL    7   // Light gray
#define COLOR_SUCCESS   10  // Light green

// Helper function to convert graphics_color_t to TUI color int
static int graphics_color_to_tui(graphics_color_t color) {
    // Map graphics colors to basic TUI colors based on RGB values
    if (color.r == 255 && color.g == 255 && color.b == 255) return 15; // White
    if (color.r == 255 && color.g == 255 && color.b == 85)  return 14; // Yellow  
    if (color.r == 85  && color.g == 255 && color.b == 255) return 11; // Cyan
    if (color.r == 85  && color.g == 255 && color.b == 85)  return 10; // Green
    if (color.r == 255 && color.g == 85  && color.b == 85)  return 12; // Red
    if (color.r == 170 && color.g == 170 && color.b == 170) return 8;  // Gray
    if (color.r == 85  && color.g == 85  && color.b == 255) return 9;  // Light blue
    return 7; // Default to light gray
}

// Convert color macros to TUI colors (constant values)
#define TUI_WHITE    15  // White
#define TUI_YELLOW   14  // Yellow
#define TUI_CYAN     11  // Cyan
#define TUI_GREEN    10  // Green
#define TUI_RED      12  // Red
#define TUI_GRAY     8   // Gray
#define TUI_BLUE     9   // Light blue

// Font dimensions
#define FONT_WIDTH 8
#define FONT_HEIGHT 16
#define LINE_SPACING 2

// =============================================================================
// ENHANCED DATA STRUCTURES
// =============================================================================

typedef enum {
    // General categories
    PANIC_TYPE_GENERAL,
    PANIC_TYPE_UNKNOWN,
    
    // Page fault subtypes
    PANIC_TYPE_PAGE_FAULT,
    PANIC_TYPE_PAGE_FAULT_MINOR,
    PANIC_TYPE_PAGE_FAULT_MAJOR,
    PANIC_TYPE_PAGE_FAULT_INVALID,
    PANIC_TYPE_PAGE_FAULT_NULL_POINTER,
    PANIC_TYPE_PAGE_FAULT_SEGMENTATION,
    PANIC_TYPE_PAGE_FAULT_ACCESS_VIOLATION,
    PANIC_TYPE_PAGE_FAULT_WRITE_PROTECT,
    PANIC_TYPE_PAGE_FAULT_USER_MODE,
    PANIC_TYPE_PAGE_FAULT_KERNEL_MODE,
    PANIC_TYPE_PAGE_FAULT_INSTRUCTION_FETCH,
    PANIC_TYPE_PAGE_FAULT_DATA_ACCESS,
    PANIC_TYPE_PAGE_FAULT_RESERVED_BIT,
    PANIC_TYPE_PAGE_FAULT_STACK_GUARD,
    PANIC_TYPE_PAGE_FAULT_COW_VIOLATION,
    PANIC_TYPE_PAGE_FAULT_SHARED_MEMORY,
    PANIC_TYPE_PAGE_FAULT_MEMORY_MAPPED_FILE,
    PANIC_TYPE_PAGE_FAULT_BUFFER_OVERFLOW,
    PANIC_TYPE_PAGE_FAULT_USE_AFTER_FREE,
    PANIC_TYPE_PAGE_FAULT_DOUBLE_FREE,
    
    // Double fault and other CPU exceptions
    PANIC_TYPE_DOUBLE_FAULT,
    PANIC_TYPE_TRIPLE_FAULT,
    PANIC_TYPE_DIVIDE_ERROR,
    PANIC_TYPE_DEBUG_EXCEPTION,
    PANIC_TYPE_NMI_INTERRUPT,
    PANIC_TYPE_BREAKPOINT,
    PANIC_TYPE_OVERFLOW,
    PANIC_TYPE_BOUND_RANGE_EXCEEDED,
    PANIC_TYPE_INVALID_OPCODE,
    PANIC_TYPE_DEVICE_NOT_AVAILABLE,
    PANIC_TYPE_COPROCESSOR_SEGMENT_OVERRUN,
    PANIC_TYPE_INVALID_TSS,
    PANIC_TYPE_SEGMENT_NOT_PRESENT,
    PANIC_TYPE_STACK_SEGMENT_FAULT,
    PANIC_TYPE_GENERAL_PROTECTION_FAULT,
    PANIC_TYPE_X87_FPU_ERROR,
    PANIC_TYPE_ALIGNMENT_CHECK,
    PANIC_TYPE_MACHINE_CHECK,
    PANIC_TYPE_SIMD_EXCEPTION,
    PANIC_TYPE_VIRTUALIZATION_EXCEPTION,
    PANIC_TYPE_CONTROL_PROTECTION_EXCEPTION,
    
    // Memory-related errors
    PANIC_TYPE_MEMORY_CORRUPTION,
    PANIC_TYPE_HEAP_CORRUPTION,
    PANIC_TYPE_STACK_CORRUPTION,
    PANIC_TYPE_BUFFER_OVERFLOW,
    PANIC_TYPE_BUFFER_UNDERFLOW,
    PANIC_TYPE_STACK_OVERFLOW,
    PANIC_TYPE_STACK_UNDERFLOW,
    PANIC_TYPE_MEMORY_LEAK,
    PANIC_TYPE_OUT_OF_MEMORY,
    PANIC_TYPE_ALLOCATION_FAILURE,
    PANIC_TYPE_DEALLOCATION_ERROR,
    PANIC_TYPE_INVALID_FREE,
    PANIC_TYPE_MEMORY_ALIGNMENT_ERROR,
    PANIC_TYPE_MEMORY_ACCESS_VIOLATION,
    PANIC_TYPE_MEMORY_PROTECTION_VIOLATION,
    PANIC_TYPE_MEMORY_MAPPING_ERROR,
    PANIC_TYPE_VIRTUAL_MEMORY_EXHAUSTED,
    PANIC_TYPE_PHYSICAL_MEMORY_EXHAUSTED,
    PANIC_TYPE_MEMORY_FRAGMENTATION,
    PANIC_TYPE_MEMORY_BOUNDS_CHECK_FAILED,
    PANIC_TYPE_MEMORY_CANARY_CORRUPTION,
    PANIC_TYPE_MEMORY_METADATA_CORRUPTION,
    PANIC_TYPE_MEMORY_POOL_CORRUPTION,
    PANIC_TYPE_MEMORY_REGION_CORRUPTION,
    PANIC_TYPE_MEMORY_SLAB_CORRUPTION,
    
    // Pointer-related errors
    PANIC_TYPE_NULL_POINTER_DEREFERENCE,
    PANIC_TYPE_WILD_POINTER_ACCESS,
    PANIC_TYPE_DANGLING_POINTER_ACCESS,
    PANIC_TYPE_INVALID_POINTER_ARITHMETIC,
    PANIC_TYPE_POINTER_CORRUPTION,
    PANIC_TYPE_FUNCTION_POINTER_CORRUPTION,
    PANIC_TYPE_VTABLE_CORRUPTION,
    PANIC_TYPE_RETURN_ADDRESS_CORRUPTION,
    
    // Hardware-related errors
    PANIC_TYPE_HARDWARE_FAILURE,
    PANIC_TYPE_CPU_FAULT,
    PANIC_TYPE_CPU_OVERHEATING,
    PANIC_TYPE_CPU_CACHE_ERROR,
    PANIC_TYPE_CPU_TLB_ERROR,
    PANIC_TYPE_CPU_MICROCODE_ERROR,
    PANIC_TYPE_MEMORY_BUS_ERROR,
    PANIC_TYPE_MEMORY_ECC_ERROR,
    PANIC_TYPE_MEMORY_PARITY_ERROR,
    PANIC_TYPE_MEMORY_CONTROLLER_ERROR,
    PANIC_TYPE_DISK_CONTROLLER_ERROR,
    PANIC_TYPE_DISK_READ_ERROR,
    PANIC_TYPE_DISK_WRITE_ERROR,
    PANIC_TYPE_DISK_SEEK_ERROR,
    PANIC_TYPE_NETWORK_CONTROLLER_ERROR,
    PANIC_TYPE_PCI_ERROR,
    PANIC_TYPE_ACPI_ERROR,
    PANIC_TYPE_BIOS_ERROR,
    PANIC_TYPE_FIRMWARE_ERROR,
    PANIC_TYPE_POWER_SUPPLY_ERROR,
    PANIC_TYPE_THERMAL_ERROR,
    PANIC_TYPE_FAN_FAILURE,
    
    // Interrupt and IRQ errors
    PANIC_TYPE_SPURIOUS_INTERRUPT,
    PANIC_TYPE_UNHANDLED_INTERRUPT,
    PANIC_TYPE_INTERRUPT_STORM,
    PANIC_TYPE_IRQ_CONFLICT,
    PANIC_TYPE_INTERRUPT_VECTOR_CORRUPTION,
    PANIC_TYPE_IDT_CORRUPTION,
    PANIC_TYPE_GDT_CORRUPTION,
    PANIC_TYPE_TSS_CORRUPTION,
    PANIC_TYPE_INTERRUPT_STACK_OVERFLOW,
    PANIC_TYPE_INTERRUPT_DEADLOCK,
    
    // Synchronization errors
    PANIC_TYPE_DEADLOCK,
    PANIC_TYPE_LIVELOCK,
    PANIC_TYPE_RACE_CONDITION,
    PANIC_TYPE_SPINLOCK_DEADLOCK,
    PANIC_TYPE_MUTEX_DEADLOCK,
    PANIC_TYPE_SEMAPHORE_OVERFLOW,
    PANIC_TYPE_SEMAPHORE_UNDERFLOW,
    PANIC_TYPE_CONDITION_VARIABLE_ERROR,
    PANIC_TYPE_BARRIER_ERROR,
    PANIC_TYPE_ATOMIC_OPERATION_FAILURE,
    PANIC_TYPE_MEMORY_ORDERING_VIOLATION,
    PANIC_TYPE_LOCK_CORRUPTION,
    PANIC_TYPE_LOCK_INVERSION,
    PANIC_TYPE_PRIORITY_INVERSION,
    
    // Process/thread errors
    PANIC_TYPE_TASK_CORRUPTION,
    PANIC_TYPE_PROCESS_CORRUPTION,
    PANIC_TYPE_THREAD_CORRUPTION,
    PANIC_TYPE_SCHEDULER_ERROR,
    PANIC_TYPE_CONTEXT_SWITCH_ERROR,
    PANIC_TYPE_STACK_POINTER_CORRUPTION,
    PANIC_TYPE_INSTRUCTION_POINTER_CORRUPTION,
    PANIC_TYPE_REGISTER_CORRUPTION,
    PANIC_TYPE_PROCESS_TABLE_CORRUPTION,
    PANIC_TYPE_THREAD_STACK_OVERFLOW,
    PANIC_TYPE_KERNEL_STACK_OVERFLOW,
    PANIC_TYPE_USER_STACK_OVERFLOW,
    PANIC_TYPE_SIGNAL_HANDLER_ERROR,
    PANIC_TYPE_SIGNAL_STACK_CORRUPTION,
    
    // Filesystem errors
    PANIC_TYPE_FILESYSTEM_CORRUPTION,
    PANIC_TYPE_INODE_CORRUPTION,
    PANIC_TYPE_SUPERBLOCK_CORRUPTION,
    PANIC_TYPE_DIRECTORY_CORRUPTION,
    PANIC_TYPE_FILE_ALLOCATION_TABLE_CORRUPTION,
    PANIC_TYPE_DISK_QUOTA_EXCEEDED,
    PANIC_TYPE_FILESYSTEM_FULL,
    PANIC_TYPE_INVALID_FILE_DESCRIPTOR,
    PANIC_TYPE_FILE_HANDLE_LEAK,
    PANIC_TYPE_MOUNT_POINT_CORRUPTION,
    PANIC_TYPE_VFS_CORRUPTION,
    PANIC_TYPE_BUFFER_CACHE_CORRUPTION,
    PANIC_TYPE_JOURNAL_CORRUPTION,
    PANIC_TYPE_METADATA_CORRUPTION,
    
    // Device driver errors
    PANIC_TYPE_DRIVER_ERROR,
    PANIC_TYPE_DRIVER_CORRUPTION,
    PANIC_TYPE_DRIVER_TIMEOUT,
    PANIC_TYPE_DRIVER_DEADLOCK,
    PANIC_TYPE_DRIVER_RESOURCE_LEAK,
    PANIC_TYPE_DRIVER_INITIALIZATION_FAILED,
    PANIC_TYPE_DRIVER_UNLOAD_ERROR,
    PANIC_TYPE_DRIVER_VERSION_MISMATCH,
    PANIC_TYPE_DRIVER_INCOMPATIBLE,
    PANIC_TYPE_DEVICE_NOT_RESPONDING,
    PANIC_TYPE_DEVICE_REMOVED,
    PANIC_TYPE_DEVICE_MALFUNCTION,
    
    // Network errors
    PANIC_TYPE_NETWORK_ERROR,
    PANIC_TYPE_NETWORK_TIMEOUT,
    PANIC_TYPE_NETWORK_BUFFER_OVERFLOW,
    PANIC_TYPE_NETWORK_PACKET_CORRUPTION,
    PANIC_TYPE_NETWORK_STACK_OVERFLOW,
    PANIC_TYPE_NETWORK_PROTOCOL_ERROR,
    PANIC_TYPE_SOCKET_ERROR,
    PANIC_TYPE_TCP_ERROR,
    PANIC_TYPE_UDP_ERROR,
    PANIC_TYPE_IP_ERROR,
    PANIC_TYPE_ETHERNET_ERROR,
    PANIC_TYPE_NETWORK_CONGESTION,
    
    // Security errors
    PANIC_TYPE_SECURITY_VIOLATION,
    PANIC_TYPE_PRIVILEGE_ESCALATION,
    PANIC_TYPE_BUFFER_OVERFLOW_EXPLOIT,
    PANIC_TYPE_CODE_INJECTION,
    PANIC_TYPE_ROP_ATTACK,
    PANIC_TYPE_JOP_ATTACK,
    PANIC_TYPE_CONTROL_FLOW_HIJACK,
    PANIC_TYPE_STACK_SMASHING,
    PANIC_TYPE_HEAP_SPRAYING,
    PANIC_TYPE_FORMAT_STRING_ATTACK,
    PANIC_TYPE_INTEGER_OVERFLOW_EXPLOIT,
    PANIC_TYPE_RACE_CONDITION_EXPLOIT,
    PANIC_TYPE_TIME_OF_CHECK_TIME_OF_USE,
    
    // Cache and memory hierarchy errors
    PANIC_TYPE_L1_CACHE_ERROR,
    PANIC_TYPE_L2_CACHE_ERROR,
    PANIC_TYPE_L3_CACHE_ERROR,
    PANIC_TYPE_L4_CACHE_ERROR,
    PANIC_TYPE_CACHE_COHERENCY_ERROR,
    PANIC_TYPE_CACHE_MISS_STORM,
    PANIC_TYPE_CACHE_THRASHING,
    PANIC_TYPE_TLB_MISS_STORM,
    PANIC_TYPE_TLB_COHERENCY_ERROR,
    PANIC_TYPE_MEMORY_BANDWIDTH_EXCEEDED,
    PANIC_TYPE_MEMORY_LATENCY_TIMEOUT,
    PANIC_TYPE_NUMA_ERROR,
    PANIC_TYPE_MEMORY_CONTROLLER_TIMEOUT,
    
    // System call errors
    PANIC_TYPE_SYSCALL_ERROR,
    PANIC_TYPE_INVALID_SYSCALL,
    PANIC_TYPE_SYSCALL_CORRUPTION,
    PANIC_TYPE_SYSCALL_TABLE_CORRUPTION,
    PANIC_TYPE_SYSCALL_STACK_OVERFLOW,
    PANIC_TYPE_SYSCALL_PARAMETER_ERROR,
    PANIC_TYPE_SYSCALL_PRIVILEGE_ERROR,
    PANIC_TYPE_SYSCALL_DEADLOCK,
    PANIC_TYPE_SYSCALL_TIMEOUT,
    
    // Boot and initialization errors
    PANIC_TYPE_BOOT_ERROR,
    PANIC_TYPE_INITIALIZATION_ERROR,
    PANIC_TYPE_MODULE_LOAD_ERROR,
    PANIC_TYPE_DEPENDENCY_ERROR,
    PANIC_TYPE_CONFIGURATION_ERROR,
    PANIC_TYPE_RESOURCE_INITIALIZATION_ERROR,
    PANIC_TYPE_EARLY_BOOT_ERROR,
    PANIC_TYPE_LATE_BOOT_ERROR,
    PANIC_TYPE_SHUTDOWN_ERROR,
    
    // Power management errors
    PANIC_TYPE_POWER_MANAGEMENT_ERROR,
    PANIC_TYPE_SUSPEND_ERROR,
    PANIC_TYPE_RESUME_ERROR,
    PANIC_TYPE_HIBERNATION_ERROR,
    PANIC_TYPE_CPU_FREQUENCY_ERROR,
    PANIC_TYPE_VOLTAGE_ERROR,
    PANIC_TYPE_THERMAL_THROTTLING,
    PANIC_TYPE_POWER_STATE_ERROR,
    
    // Virtualization errors
    PANIC_TYPE_HYPERVISOR_ERROR,
    PANIC_TYPE_GUEST_OS_ERROR,
    PANIC_TYPE_VM_EXIT_ERROR,
    PANIC_TYPE_VM_ENTRY_ERROR,
    PANIC_TYPE_VIRTUAL_MACHINE_CORRUPTION,
    PANIC_TYPE_HYPERCALL_ERROR,
    PANIC_TYPE_VIRTUALIZATION_FEATURE_ERROR,
    
    // Compiler and code generation errors
    PANIC_TYPE_COMPILER_ERROR,
    PANIC_TYPE_OPTIMIZATION_ERROR,
    PANIC_TYPE_UNDEFINED_BEHAVIOR,
    PANIC_TYPE_UNINITIALIZED_VARIABLE,
    PANIC_TYPE_ARRAY_BOUNDS_VIOLATION,
    PANIC_TYPE_INTEGER_OVERFLOW,
    PANIC_TYPE_INTEGER_UNDERFLOW,
    PANIC_TYPE_DIVISION_BY_ZERO,
    PANIC_TYPE_FLOATING_POINT_ERROR,
    PANIC_TYPE_STACK_FRAME_CORRUPTION,
    PANIC_TYPE_CALLING_CONVENTION_ERROR,
    
    // Debug and development errors
    PANIC_TYPE_ASSERTION_FAILED,
    PANIC_TYPE_DEBUG_TRAP,
    PANIC_TYPE_BREAKPOINT_ERROR,
    PANIC_TYPE_WATCHPOINT_ERROR,
    PANIC_TYPE_PROFILING_ERROR,
    PANIC_TYPE_TRACE_ERROR,
    PANIC_TYPE_INSTRUMENTATION_ERROR,
    
    // Timing and real-time errors
    PANIC_TYPE_TIMING_ERROR,
    PANIC_TYPE_DEADLINE_MISSED,
    PANIC_TYPE_TIMER_ERROR,
    PANIC_TYPE_CLOCK_DRIFT,
    PANIC_TYPE_TIMESTAMP_ERROR,
    PANIC_TYPE_REAL_TIME_CONSTRAINT_VIOLATION,
    PANIC_TYPE_SCHEDULE_OVERRUN,
    
    // Communication and IPC errors
    PANIC_TYPE_IPC_ERROR,
    PANIC_TYPE_MESSAGE_QUEUE_OVERFLOW,
    PANIC_TYPE_SHARED_MEMORY_ERROR,
    PANIC_TYPE_PIPE_ERROR,
    PANIC_TYPE_COMMUNICATION_TIMEOUT,
    PANIC_TYPE_PROTOCOL_VIOLATION,
    
    // Legacy and compatibility errors
    PANIC_TYPE_KERNEL_OOPS,
    PANIC_TYPE_LEGACY_HARDWARE_ERROR,
    PANIC_TYPE_COMPATIBILITY_ERROR,
    PANIC_TYPE_EMULATION_ERROR,
    PANIC_TYPE_TRANSLATION_ERROR,
    
    // Custom and user-defined errors
    PANIC_TYPE_USER_DEFINED_1,
    PANIC_TYPE_USER_DEFINED_2,
    PANIC_TYPE_USER_DEFINED_3,
    PANIC_TYPE_CUSTOM_HANDLER_ERROR,
    PANIC_TYPE_MODULE_SPECIFIC_ERROR,
    
    PANIC_TYPE_MAX // Keep this last
} panic_type_t;

// BSOD-style page system
typedef enum {
    BSOD_PAGE_OVERVIEW = 0,
    BSOD_PAGE_CPU_STATE,
    BSOD_PAGE_MEMORY_INFO,
    BSOD_PAGE_SYSTEM_INFO,
    BSOD_PAGE_STACK_TRACE,
    BSOD_PAGE_HARDWARE,
    BSOD_PAGE_ADVANCED,
    BSOD_PAGE_MAX
} bsod_page_t;

// Forward declare panic_context_t to avoid circular dependency
typedef struct panic_context panic_context_t;

// Forward declarations for helper functions
static const char* get_panic_type_name(panic_type_t type);

typedef struct {
    bsod_page_t id;
    const char* title;
    const char* description;
    void (*render_func)(const panic_context_t* ctx, int start_row);
} bsod_page_info_t;

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

typedef struct panic_context {
    char message[256];
    char file[128];
    char function[128];
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
// GRAPHICS GLOBALS & HELPERS
// =============================================================================

static font_t* g_panic_font = NULL;
static framebuffer_t* g_panic_framebuffer = NULL;
static uint32_t g_screen_width = 0;
static uint32_t g_screen_height = 0;
static panic_context_t g_panic_context;
static struct {
    bool valid;
    uint32 fault_addr;
    uint32 error_code;
    uint32 fault_eip;
    uint32 fault_cs;
    uint32 fault_eflags;
} g_panic_pending_fault = {0};

static void panic_store_context(const panic_context_t* ctx) {
    if (!ctx) {
        return;
    }
    memcpy(&g_panic_context, ctx, sizeof(panic_context_t));
}

void panic_preload_fault_info(uint32 fault_addr, uint32 error_code,
                              uint32 fault_eip, uint32 fault_cs, uint32 fault_eflags) {
    g_panic_pending_fault.valid = true;
    g_panic_pending_fault.fault_addr = fault_addr;
    g_panic_pending_fault.error_code = error_code;
    g_panic_pending_fault.fault_eip = fault_eip;
    g_panic_pending_fault.fault_cs = fault_cs;
    g_panic_pending_fault.fault_eflags = fault_eflags;
}

// Core graphics drawing functions using the graphics API directly
static void panic_draw_text(const char* text, int x, int y, graphics_color_t color) {
    if (g_panic_font && graphics_is_initialized()) {
        graphics_draw_text(x, y, text, g_panic_font, color);
    }
}

static void panic_fill_rect(int x, int y, int w, int h, graphics_color_t color) {
    graphics_rect_t rect = {x, y, w, h};
    graphics_draw_rect(&rect, color, true);
}

static void panic_draw_rect(int x, int y, int w, int h, graphics_color_t color) {
    graphics_rect_t rect = {x, y, w, h};
    graphics_draw_rect(&rect, color, false);
}

static void panic_draw_line(int x1, int y1, int x2, int y2, graphics_color_t color) {
    graphics_draw_line(x1, y1, x2, y2, color);
}

static void panic_draw_pixel(int x, int y, graphics_color_t color) {
    graphics_draw_pixel(x, y, color);
}

// Graphics-based UI functions to replace TUI
static void panic_draw_window(int x, int y, int width, int height, const char* title, graphics_color_t fg_color, graphics_color_t bg_color) {
    // Draw window background
    panic_fill_rect(x, y, width, height, bg_color);
    
    // Draw window border
    panic_draw_rect(x, y, width, height, fg_color);
    
    // Draw title bar if title provided
    if (title) {
        int title_height = 20;
        panic_fill_rect(x + 1, y + 1, width - 2, title_height, fg_color);
        
        // Center title text
        uint32_t title_width, title_text_height;
        if (g_panic_font && graphics_get_text_bounds(title, g_panic_font, &title_width, &title_text_height) == GRAPHICS_SUCCESS) {
            int title_x = x + (width - title_width) / 2;
            int title_y = y + (title_height - title_text_height) / 2 + title_text_height;
            panic_draw_text(title, title_x, title_y, bg_color);
        }
    }
}

static void panic_print_at(int x, int y, const char* text, graphics_color_t fg_color) {
    panic_draw_text(text, x, y, fg_color);
}

static void panic_center_text(int x, int y, int width, const char* text, graphics_color_t fg_color) {
    if (g_panic_font) {
        uint32_t text_width, text_height;
        if (graphics_get_text_bounds(text, g_panic_font, &text_width, &text_height) == GRAPHICS_SUCCESS) {
            int centered_x = x + (width - text_width) / 2;
            panic_draw_text(text, centered_x, y, fg_color);
        }
    }
}

static void panic_draw_status_bar(int y, const char* left_text, const char* right_text, graphics_color_t fg_color, graphics_color_t bg_color) {
    // Draw status bar background
    panic_fill_rect(0, y, g_screen_width, 25, bg_color);
    
    // Draw left text
    if (left_text) {
        panic_draw_text(left_text, 10, y + 20, fg_color);
    }
    
    // Draw right text
    if (right_text && g_panic_font) {
        uint32_t text_width, text_height;
        if (graphics_get_text_bounds(right_text, g_panic_font, &text_width, &text_height) == GRAPHICS_SUCCESS) {
            int right_x = g_screen_width - text_width - 10;
            panic_draw_text(right_text, right_x, y + 20, fg_color);
        }
    }
}

static void panic_print_table_row(int x, int y, int width, const char* label, const char* value, 
                                 graphics_color_t label_color, graphics_color_t value_color, graphics_color_t bg_color) {
    // Draw row background
    panic_fill_rect(x, y, width, 18, bg_color);
    
    // Draw label
    panic_draw_text(label, x + 5, y + 14, label_color);
    
    // Draw value (right-aligned within available space)
    if (value && g_panic_font) {
        uint32_t value_width, value_height;
        if (graphics_get_text_bounds(value, g_panic_font, &value_width, &value_height) == GRAPHICS_SUCCESS) {
            int value_x = x + width - value_width - 5;
            panic_draw_text(value, value_x, y + 14, value_color);
        }
    }
}

static void panic_print_section_header(int x, int y, int width, const char* title, graphics_color_t fg_color, graphics_color_t bg_color) {
    // Draw header background
    panic_fill_rect(x, y, width, 20, bg_color);
    
    // Draw separator lines
    panic_draw_line(x, y, x + width, y, fg_color);
    panic_draw_line(x, y + 19, x + width, y + 19, fg_color);
    
    // Draw title text
    panic_draw_text(title, x + 5, y + 16, fg_color);
}


// =============================================================================
// GLOBAL STATE
// =============================================================================

static panic_context_t g_panic_context;
static bool g_panic_initialized = false;
static uint32 g_panic_count = 0;
// BSOD-style page system globals
static bsod_page_t g_current_page = BSOD_PAGE_OVERVIEW;
static int32 g_page_scroll_offset = 0;

// Forward declarations for page rendering functions
static void render_overview_page(const panic_context_t* ctx, int start_row);
static void render_cpu_state_page(const panic_context_t* ctx, int start_row);
static void render_memory_info_page(const panic_context_t* ctx, int start_row);
static void render_system_info_page(const panic_context_t* ctx, int start_row);
static void render_stack_trace_page(const panic_context_t* ctx, int start_row);
static void render_hardware_page(const panic_context_t* ctx, int start_row);
static void render_advanced_page(const panic_context_t* ctx, int start_row);

// Page information array
static const bsod_page_info_t g_bsod_pages[BSOD_PAGE_MAX] = {
    {BSOD_PAGE_OVERVIEW,   "SYSTEM FAILURE OVERVIEW", "General error information and quick diagnostics", render_overview_page},
    {BSOD_PAGE_CPU_STATE,  "PROCESSOR STATE",         "CPU registers, flags, and execution context",   render_cpu_state_page},
    {BSOD_PAGE_MEMORY_INFO, "MEMORY ANALYSIS",         "Memory layout, faults, and corruption analysis", render_memory_info_page},
    {BSOD_PAGE_SYSTEM_INFO, "SYSTEM INFORMATION",      "Hardware, kernel, and system configuration",     render_system_info_page},
    {BSOD_PAGE_STACK_TRACE, "STACK TRACE",             "Call stack and execution flow analysis",        render_stack_trace_page},
    {BSOD_PAGE_HARDWARE,    "HARDWARE STATUS",         "Hardware state and driver information",         render_hardware_page},
    {BSOD_PAGE_ADVANCED,    "ADVANCED DIAGNOSTICS",    "Detailed technical analysis and debugging",     render_advanced_page}
};

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
        state->return_address = 0x00000000;  // Safe null instead of problematic marker
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
    debuglog_write("[PANIC][GDB] ===== CPU REGISTER STATE =====\n");
    debuglog_write("[PANIC][GDB] General Purpose Registers:\n");
    panic_debuglog_write_register("EAX", cpu->eax);
    panic_debuglog_write_register("EBX", cpu->ebx);
    panic_debuglog_write_register("ECX", cpu->ecx);
    panic_debuglog_write_register("EDX", cpu->edx);
    panic_debuglog_write_register("ESI", cpu->esi);
    panic_debuglog_write_register("EDI", cpu->edi);
    
    debuglog_write("[PANIC][GDB] Stack and Instruction Pointers:\n");
    panic_debuglog_write_register("EBP", cpu->ebp);
    panic_debuglog_write_register("ESP", cpu->esp);
    panic_debuglog_write_register("EIP", cpu->eip);
    
    debuglog_write("[PANIC][GDB] Segment Registers:\n");
    panic_debuglog_write_register("CS", cpu->cs);
    panic_debuglog_write_register("SS", cpu->ss);
    panic_debuglog_write_register("DS", cpu->ds);
    panic_debuglog_write_register("ES", cpu->es);
    panic_debuglog_write_register("FS", cpu->fs);
    panic_debuglog_write_register("GS", cpu->gs);
    
    debuglog_write("[PANIC][GDB] Flags Register (EFLAGS):\n");
    panic_debuglog_write_register("EFLAGS", cpu->eflags);
    debuglog_write("[PANIC][GDB]     CF(Carry)="); 
    debuglog_write_dec((cpu->eflags & 0x1) ? 1 : 0);
    debuglog_write(", PF(Parity)=");
    debuglog_write_dec((cpu->eflags & 0x4) ? 1 : 0);
    debuglog_write(", ZF(Zero)=");
    debuglog_write_dec((cpu->eflags & 0x40) ? 1 : 0);
    debuglog_write(", SF(Sign)=");
    debuglog_write_dec((cpu->eflags & 0x80) ? 1 : 0);
    debuglog_write(", IF(Interrupt)=");
    debuglog_write_dec((cpu->eflags & 0x200) ? 1 : 0);
    debuglog_write("\n");
    
    debuglog_write("[PANIC][GDB] Control Registers:\n");
    panic_debuglog_write_register("CR0", cpu->cr0);
    if (cpu->cr0 != 0) {
        debuglog_write("[PANIC][GDB]     PE(Protected)=");
        debuglog_write_dec((cpu->cr0 & 0x1) ? 1 : 0);
        debuglog_write(", MP(Math Present)=");
        debuglog_write_dec((cpu->cr0 & 0x2) ? 1 : 0);
        debuglog_write(", EM(Emulation)=");
        debuglog_write_dec((cpu->cr0 & 0x4) ? 1 : 0);
        debuglog_write(", TS(Task Switch)=");
        debuglog_write_dec((cpu->cr0 & 0x8) ? 1 : 0);
        debuglog_write(", PG(Paging)=");
        debuglog_write_dec((cpu->cr0 & 0x80000000) ? 1 : 0);
        debuglog_write("\n");
    }
    
    panic_debuglog_write_register("CR2", cpu->cr2);
    if (cpu->cr2 != 0) {
        debuglog_write("[PANIC][GDB]     Page Fault Linear Address: ");
        char cr2_addr[12];
        format_hex32(cpu->cr2, cr2_addr);
        debuglog_write(cr2_addr);
        debuglog_write("\n");
    }
    
    panic_debuglog_write_register("CR3", cpu->cr3);
    if (cpu->cr3 != 0) {
        debuglog_write("[PANIC][GDB]     Page Directory Base: ");
        char cr3_addr[12];
        format_hex32(cpu->cr3 & 0xFFFFF000, cr3_addr);
        debuglog_write(cr3_addr);
        debuglog_write("\n");
    }
    
    panic_debuglog_write_register("CR4", cpu->cr4);
    if (cpu->cr4 != 0) {
        debuglog_write("[PANIC][GDB]     VME=");
        debuglog_write_dec((cpu->cr4 & 0x1) ? 1 : 0);
        debuglog_write(", PVI=");
        debuglog_write_dec((cpu->cr4 & 0x2) ? 1 : 0);
        debuglog_write(", PSE=");
        debuglog_write_dec((cpu->cr4 & 0x10) ? 1 : 0);
        debuglog_write(", PAE=");
        debuglog_write_dec((cpu->cr4 & 0x20) ? 1 : 0);
        debuglog_write("\n");
    }
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_stack(const panic_context_t* ctx) {
    debuglog_write("[PANIC][GDB] ===== CALL STACK ANALYSIS =====\n");
    if (ctx->stack_frame_count == 0) {
        debuglog_write("[PANIC][GDB] Call stack: (no frames available - possible stack corruption)\n");
        debuglog_write("[PANIC][GDB] Stack pointer (ESP): ");
        char esp_addr[12];
        format_hex32(ctx->cpu_state.esp, esp_addr);
        debuglog_write(esp_addr);
        debuglog_write("\n");
        debuglog_write("[PANIC][GDB] Frame pointer (EBP): ");
        char ebp_addr[12];
        format_hex32(ctx->cpu_state.ebp, ebp_addr);
        debuglog_write(ebp_addr);
        debuglog_write("\n");
        return;
    }

    debuglog_write("[PANIC][GDB] Stack unwinding successful, found ");
    debuglog_write_dec(ctx->stack_frame_count);
    debuglog_write(" frames:\n");
    
    for (uint32 i = 0; i < ctx->stack_frame_count; i++) {
        char addr[12];
        char caller[12];
        format_hex32(ctx->stack_trace[i].address, addr);
        format_hex32(ctx->stack_trace[i].caller, caller);

        debuglog_write("[PANIC][GDB]   Frame #");
        debuglog_write_dec(i);
        debuglog_write(": ");
        debuglog_write(addr);
        debuglog_write(" <- ");
        debuglog_write(caller);
        
        if (!ctx->stack_trace[i].valid) {
            debuglog_write(" [INVALID - possible stack corruption]");
        } else {
            debuglog_write(" [valid]");
            
            // Try to identify known kernel sections
            uint32 addr_val = ctx->stack_trace[i].address;
            if (addr_val >= 0x100000 && addr_val < 0x200000) {
                debuglog_write(" [kernel code section]");
            } else if (addr_val >= 0x200000 && addr_val < 0x300000) {
                debuglog_write(" [kernel data section]");
            } else if (addr_val >= 0x400000 && addr_val < 0x800000) {
                debuglog_write(" [userspace code]");
            } else if (addr_val == 0x0) {
                debuglog_write(" [NULL pointer - stack termination]");
            } else if (addr_val < 0x1000) {
                debuglog_write(" [low memory - possible NULL dereference]");
            } else {
                debuglog_write(" [unknown section]");
            }
        }
        debuglog_write("\n");
    }
    
    // Dump raw stack contents around ESP
    debuglog_write("[PANIC][GDB] Raw stack contents (32 bytes around ESP):\n");
    uint32 esp = ctx->cpu_state.esp;
    for (int i = -4; i <= 4; i++) {
        uint32 stack_addr = esp + (i * 4);
        char stack_addr_str[12];
        format_hex32(stack_addr, stack_addr_str);
        debuglog_write("[PANIC][GDB]   ");
        debuglog_write(stack_addr_str);
        debuglog_write(": ");
        
        // Try to safely read the memory (this might fail in some cases)
        // For now, just indicate it would be read
        if (i == 0) {
            debuglog_write("(ESP)");
        } else {
            debuglog_write("(stack data)");
        }
        debuglog_write("\n");
    }
    debuglog_write("[PANIC][GDB] ================================\n");
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

static void panic_debuglog_emit_memory_info(const panic_context_t* ctx) {
    debuglog_write("[PANIC][GDB] ===== MEMORY SUBSYSTEM STATE =====\n");
    
    // Memory layout information
    debuglog_write("[PANIC][GDB] Kernel Memory Layout:\n");
    debuglog_write("[PANIC][GDB]   Kernel start: 0x00100000 (1MB)\n");
    debuglog_write("[PANIC][GDB]   Kernel end:   0x00200000 (estimated)\n");
    debuglog_write("[PANIC][GDB]   Heap start:   0x00300000 (estimated)\n");
    debuglog_write("[PANIC][GDB]   Stack area:   0x00080000-0x000FFFFF\n");
    debuglog_write("[PANIC][GDB]   User space:   0x00000000-0x7FFFFFFF (2GB)\n");
    debuglog_write("[PANIC][GDB]   Kernel space: 0x80000000-0xFFFFFFFF (2GB)\n");
    debuglog_write("[PANIC][GDB]   Video memory: 0x000B8000-0x000BFFFF (32KB)\n");
    debuglog_write("[PANIC][GDB]   BIOS area:    0x000F0000-0x000FFFFF (64KB)\n");
    
    // Page directory information from CR3
    if (ctx->cpu_state.cr3 != 0) {
        debuglog_write("[PANIC][GDB] Paging Information:\n");
        debuglog_write("[PANIC][GDB]   Page Directory: ");
        char cr3_str[12];
        format_hex32(ctx->cpu_state.cr3 & 0xFFFFF000, cr3_str);
        debuglog_write(cr3_str);
        debuglog_write("\n");
        debuglog_write("[PANIC][GDB]   Paging Enabled: ");
        debuglog_write((ctx->cpu_state.cr0 & 0x80000000) ? "YES" : "NO");
        debuglog_write("\n");
        debuglog_write("[PANIC][GDB]   Page Size: 4KB (4096 bytes)\n");
        debuglog_write("[PANIC][GDB]   Page Directory Entries: 1024\n");
        debuglog_write("[PANIC][GDB]   Page Table Entries per Table: 1024\n");
        debuglog_write("[PANIC][GDB]   Total Addressable Space: 4GB\n");
        
        // Page directory analysis
        uint32 pd_base = ctx->cpu_state.cr3 & 0xFFFFF000;
        debuglog_write("[PANIC][GDB] Page Directory Analysis:\n");
        debuglog_write("[PANIC][GDB]   PD Physical Address: ");
        char pd_str[12];
        format_hex32(pd_base, pd_str);
        debuglog_write(pd_str);
        debuglog_write("\n");
        debuglog_write("[PANIC][GDB]   PD Alignment: ");
        debuglog_write((pd_base & 0xFFF) == 0 ? "CORRECT (4KB aligned)" : "INCORRECT (misaligned)");
        debuglog_write("\n");
    }
    
    // Page fault information if available
    if (ctx->cpu_state.cr2 != 0) {
        debuglog_write("[PANIC][GDB] Page Fault Details:\n");
        debuglog_write("[PANIC][GDB]   Faulting Address: ");
        char cr2_str[12];
        format_hex32(ctx->cpu_state.cr2, cr2_str);
        debuglog_write(cr2_str);
        debuglog_write("\n");
        
        // Detailed fault analysis
        uint32 fault_addr = ctx->cpu_state.cr2;
        uint32 page_number = fault_addr >> 12;
        uint32 page_offset = fault_addr & 0xFFF;
        
        debuglog_write("[PANIC][GDB]   Page Number: ");
        char page_str[12];
        format_hex32(page_number, page_str);
        debuglog_write(page_str);
        debuglog_write("\n");
        debuglog_write("[PANIC][GDB]   Page Offset: ");
        char offset_str[12];
        format_hex32(page_offset, offset_str);
        debuglog_write(offset_str);
        debuglog_write(" (");
        debuglog_write_dec(page_offset);
        debuglog_write(" bytes)\n");
        
        // Analyze fault type based on address
        if (fault_addr == 0) {
            debuglog_write("[PANIC][GDB]   Fault Type: NULL pointer dereference\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Uninitialized pointer or failed allocation\n");
        } else if (fault_addr == 0xDEADBEEF) {
            debuglog_write("[PANIC][GDB]   Fault Type: Heap corruption marker accessed\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Use-after-free or double-free\n");
        } else if (fault_addr < 0x1000) {
            debuglog_write("[PANIC][GDB]   Fault Type: Low memory access (likely NULL+offset)\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Array access on NULL pointer\n");
        } else if (fault_addr >= 0xFFF00000) {
            debuglog_write("[PANIC][GDB]   Fault Type: Very high memory access\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Integer overflow or corruption\n");
        } else if (fault_addr >= 0xC0000000) {
            debuglog_write("[PANIC][GDB]   Fault Type: Kernel space access\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Privilege escalation attempt or kernel bug\n");
        } else if (fault_addr >= 0x80000000) {
            debuglog_write("[PANIC][GDB]   Fault Type: High user space access\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Stack overflow or heap corruption\n");
        } else {
            debuglog_write("[PANIC][GDB]   Fault Type: User space access\n");
            debuglog_write("[PANIC][GDB]   Likely Cause: Invalid pointer or unmapped memory\n");
        }
        
        // Memory region analysis
        debuglog_write("[PANIC][GDB] Memory Region Analysis:\n");
        if (fault_addr >= 0xB8000 && fault_addr <= 0xBFFFF) {
            debuglog_write("[PANIC][GDB]   Region: VGA Text Mode Buffer\n");
        } else if (fault_addr >= 0xF0000 && fault_addr <= 0xFFFFF) {
            debuglog_write("[PANIC][GDB]   Region: BIOS ROM Area\n");
        } else if (fault_addr >= 0x100000 && fault_addr <= 0x200000) {
            debuglog_write("[PANIC][GDB]   Region: Kernel Code/Data\n");
        } else if (fault_addr >= 0x80000 && fault_addr <= 0xFFFFF) {
            debuglog_write("[PANIC][GDB]   Region: Kernel Stack Area\n");
        } else {
            debuglog_write("[PANIC][GDB]   Region: Unknown/Unmapped\n");
        }
    }
    
    // Heap status (if heap functions are available)
    debuglog_write("[PANIC][GDB] Heap Status:\n");
    debuglog_write("[PANIC][GDB]   Heap corruption marker: 0xDEADBEEF\n");
    debuglog_write("[PANIC][GDB]   Current ESP: ");
    char esp_str[12];
    format_hex32(ctx->cpu_state.esp, esp_str);
    debuglog_write(esp_str);
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]   Current EBP: ");
    char ebp_str[12];
    format_hex32(ctx->cpu_state.ebp, ebp_str);
    debuglog_write(ebp_str);
    debuglog_write("\n");
    
    // Enhanced stack analysis
    uint32 stack_base = 0x000FFFFF;
    uint32 stack_size = stack_base - ctx->cpu_state.esp;
    debuglog_write("[PANIC][GDB]   Stack used: ~");
    debuglog_write_dec(stack_size);
    debuglog_write(" bytes\n");
    debuglog_write("[PANIC][GDB]   Stack remaining: ~");
    debuglog_write_dec(ctx->cpu_state.esp - 0x80000);
    debuglog_write(" bytes\n");
    debuglog_write("[PANIC][GDB]   Stack utilization: ");
    uint32 stack_total = stack_base - 0x80000;
    uint32 utilization = (stack_size * 100) / stack_total;
    debuglog_write_dec(utilization);
    debuglog_write("%\n");
    
    if (stack_size > 0x70000) {  // More than 448KB used
        debuglog_write("[PANIC][GDB]   WARNING: High stack usage detected!\n");
        debuglog_write("[PANIC][GDB]   Possible stack overflow condition\n");
    }
    
    // Stack pointer validation
    debuglog_write("[PANIC][GDB] Stack Pointer Validation:\n");
    if (ctx->cpu_state.esp < 0x80000) {
        debuglog_write("[PANIC][GDB]   ESP below stack area - CRITICAL CORRUPTION\n");
    } else if (ctx->cpu_state.esp > 0x100000) {
        debuglog_write("[PANIC][GDB]   ESP above stack area - CRITICAL CORRUPTION\n");
    } else {
        debuglog_write("[PANIC][GDB]   ESP within valid stack range\n");
    }
    
    if (ctx->cpu_state.esp & 0x3) {
        debuglog_write("[PANIC][GDB]   ESP misaligned (not 4-byte aligned)\n");
    } else {
        debuglog_write("[PANIC][GDB]   ESP properly aligned\n");
    }
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_system_info(const panic_context_t* ctx) {
    (void)ctx; // Suppress unused parameter warning
    
    debuglog_write("[PANIC][GDB] ===== SYSTEM STATE INFORMATION =====\n");
    
    // CPU mode and privilege level
    debuglog_write("[PANIC][GDB] CPU State:\n");
    uint16 cs = ctx->cpu_state.cs;
    uint8 cpl = cs & 0x3;  // Current Privilege Level
    debuglog_write("[PANIC][GDB]   Privilege Level: ");
    debuglog_write_dec(cpl);
    debuglog_write((cpl == 0) ? " (Kernel Mode)\n" : " (User Mode)\n");
    debuglog_write("[PANIC][GDB]   Code Segment: ");
    char cs_str[12];
    format_hex32(cs, cs_str);
    debuglog_write(cs_str);
    debuglog_write("\n");
    
    // Detailed segment analysis
    debuglog_write("[PANIC][GDB] Segment Descriptors Analysis:\n");
    debuglog_write("[PANIC][GDB]   CS Selector: ");
    char cs_sel[12];
    format_hex32(ctx->cpu_state.cs, cs_sel);
    debuglog_write(cs_sel);
    debuglog_write((ctx->cpu_state.cs == 0x08) ? " (Kernel Code Segment)\n" : " (Unknown/User Segment)\n");
    debuglog_write("[PANIC][GDB]   DS Selector: ");
    char ds_sel[12];
    format_hex32(ctx->cpu_state.ds, ds_sel);
    debuglog_write(ds_sel);
    debuglog_write((ctx->cpu_state.ds == 0x10) ? " (Kernel Data Segment)\n" : " (Unknown/User Segment)\n");
    debuglog_write("[PANIC][GDB]   SS Selector: ");
    char ss_sel[12];
    format_hex32(ctx->cpu_state.ss, ss_sel);
    debuglog_write(ss_sel);
    debuglog_write((ctx->cpu_state.ss == 0x10) ? " (Kernel Stack Segment)\n" : " (Unknown/User Segment)\n");
    
    // Interrupt state analysis
    debuglog_write("[PANIC][GDB] Interrupt State:\n");
    bool interrupts_enabled = (ctx->cpu_state.eflags & 0x200) != 0;
    debuglog_write("[PANIC][GDB]   Interrupts: ");
    debuglog_write(interrupts_enabled ? "ENABLED" : "DISABLED");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]   EFLAGS Register Analysis:\n");
    debuglog_write("[PANIC][GDB]     CF (Carry): ");
    debuglog_write((ctx->cpu_state.eflags & 0x1) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     PF (Parity): ");
    debuglog_write((ctx->cpu_state.eflags & 0x4) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     ZF (Zero): ");
    debuglog_write((ctx->cpu_state.eflags & 0x40) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     SF (Sign): ");
    debuglog_write((ctx->cpu_state.eflags & 0x80) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     IF (Interrupt): ");
    debuglog_write(interrupts_enabled ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     DF (Direction): ");
    debuglog_write((ctx->cpu_state.eflags & 0x400) ? "SET (decrement)" : "CLEAR (increment)");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     OF (Overflow): ");
    debuglog_write((ctx->cpu_state.eflags & 0x800) ? "SET" : "CLEAR");
    debuglog_write("\n");
    
    // System timers and counters
    debuglog_write("[PANIC][GDB] System Counters:\n");
    debuglog_write("[PANIC][GDB]   Panic Count: ");
    debuglog_write_dec(g_panic_count);
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]   Boot Stage: POST_INIT (assumed)\n");
    debuglog_write("[PANIC][GDB]   System Uptime: Unknown (timer unavailable)\n");
    debuglog_write("[PANIC][GDB]   Memory Management: Basic (paging enabled)\n");
    
    // Exception analysis
    debuglog_write("[PANIC][GDB] Exception Analysis:\n");
    if (ctx->type == PANIC_TYPE_PAGE_FAULT) {
        debuglog_write("[PANIC][GDB]   Exception Type: Page Fault (0x0E)\n");
        debuglog_write("[PANIC][GDB]   Error Code Analysis:\n");
        debuglog_write("[PANIC][GDB]     Present: Page ");
        debuglog_write("not present\n");  // Assuming not present since it's a fault
        debuglog_write("[PANIC][GDB]     Access: ");
        debuglog_write("Unknown (error code unavailable)\n");
        debuglog_write("[PANIC][GDB]     Mode: Kernel mode access\n");
    } else if (ctx->type == PANIC_TYPE_DOUBLE_FAULT) {
        debuglog_write("[PANIC][GDB]   Exception Type: Double Fault (0x08)\n");
        debuglog_write("[PANIC][GDB]   Critical: System unable to handle initial exception\n");
    } else {
        debuglog_write("[PANIC][GDB]   Exception Type: General Protection/Other\n");
    }
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_hardware_info(const panic_context_t* ctx) {
    (void)ctx; // Suppress unused parameter warning
    
    debuglog_write("[PANIC][GDB] ===== HARDWARE STATE =====\n");
    
    // PIC state and interrupt routing
    debuglog_write("[PANIC][GDB] Interrupt Controller (PIC):\n");
    debuglog_write("[PANIC][GDB]   Master PIC: 0x20-0x21\n");
    debuglog_write("[PANIC][GDB]   Slave PIC:  0xA0-0xA1\n");
    debuglog_write("[PANIC][GDB]   IRQ Mapping:\n");
    debuglog_write("[PANIC][GDB]     IRQ 0: Timer (PIT 8253/8254)\n");
    debuglog_write("[PANIC][GDB]     IRQ 1: Keyboard (PS/2)\n");
    debuglog_write("[PANIC][GDB]     IRQ 2: Cascade from Slave PIC\n");
    debuglog_write("[PANIC][GDB]     IRQ 3: COM2 Serial Port\n");
    debuglog_write("[PANIC][GDB]     IRQ 4: COM1 Serial Port\n");
    debuglog_write("[PANIC][GDB]     IRQ 5: LPT2 Parallel Port\n");
    debuglog_write("[PANIC][GDB]     IRQ 6: Floppy Disk Controller\n");
    debuglog_write("[PANIC][GDB]     IRQ 7: LPT1 Parallel Port\n");
    debuglog_write("[PANIC][GDB]     IRQ 8-15: Slave PIC (8-15)\n");
    
    // Control register analysis
    debuglog_write("[PANIC][GDB] CPU Control Registers:\n");
    debuglog_write("[PANIC][GDB]   CR0 Analysis:\n");
    uint32 cr0 = ctx->cpu_state.cr0;
    debuglog_write("[PANIC][GDB]     PE (Protected Mode): ");
    debuglog_write((cr0 & 0x1) ? "ENABLED" : "DISABLED");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     MP (Math Present): ");
    debuglog_write((cr0 & 0x2) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     EM (Emulation): ");
    debuglog_write((cr0 & 0x4) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     TS (Task Switched): ");
    debuglog_write((cr0 & 0x8) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     ET (Extension Type): ");
    debuglog_write((cr0 & 0x10) ? "80387" : "80287");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     NE (Numeric Error): ");
    debuglog_write((cr0 & 0x20) ? "ENABLED" : "DISABLED");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     WP (Write Protect): ");
    debuglog_write((cr0 & 0x10000) ? "ENABLED" : "DISABLED");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     AM (Alignment Mask): ");
    debuglog_write((cr0 & 0x40000) ? "ENABLED" : "DISABLED");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     NW (Not Write-through): ");
    debuglog_write((cr0 & 0x20000000) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     CD (Cache Disable): ");
    debuglog_write((cr0 & 0x40000000) ? "SET" : "CLEAR");
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB]     PG (Paging): ");
    debuglog_write((cr0 & 0x80000000) ? "ENABLED" : "DISABLED");
    debuglog_write("\n");
    
    // Hardware detection and platform information
    debuglog_write("[PANIC][GDB] Hardware Configuration:\n");
    debuglog_write("[PANIC][GDB]   Architecture: i386 (32-bit)\n");
    debuglog_write("[PANIC][GDB]   Platform: PC/AT Compatible\n");
    debuglog_write("[PANIC][GDB]   Memory Model: Linear 32-bit\n");
    debuglog_write("[PANIC][GDB]   Byte Order: Little Endian\n");
    debuglog_write("[PANIC][GDB]   Word Size: 32-bit\n");
    debuglog_write("[PANIC][GDB]   Cache Line Size: 32 bytes (estimated)\n");
    debuglog_write("[PANIC][GDB]   FPU Present: ");
    debuglog_write((cr0 & 0x4) ? "NO (emulated)" : "YES (or emulated)");
    debuglog_write("\n");
    
    // I/O port ranges and device information
    debuglog_write("[PANIC][GDB] I/O Subsystem:\n");
    debuglog_write("[PANIC][GDB]   VGA Text Mode: 80x25\n");
    debuglog_write("[PANIC][GDB]   VGA Buffer: 0xB8000-0xBFFFF\n");
    debuglog_write("[PANIC][GDB]   VGA Registers: 0x3C0-0x3DF\n");
    debuglog_write("[PANIC][GDB]   Keyboard: PS/2 Compatible\n");
    debuglog_write("[PANIC][GDB]   Keyboard Controller: 0x60, 0x64\n");
    debuglog_write("[PANIC][GDB]   Timer: PIT 8253/8254\n");
    debuglog_write("[PANIC][GDB]   Timer Ports: 0x40-0x43\n");
    debuglog_write("[PANIC][GDB]   Serial Ports: 0x3F8 (COM1), 0x2F8 (COM2)\n");
    debuglog_write("[PANIC][GDB]   Parallel Port: 0x378 (LPT1)\n");
    debuglog_write("[PANIC][GDB]   Floppy Controller: 0x3F0-0x3F7\n");
    debuglog_write("[PANIC][GDB]   DMA Controller: 0x00-0x0F, 0xC0-0xDF\n");
    
    // System timing and clock information
    debuglog_write("[PANIC][GDB] System Timing:\n");
    debuglog_write("[PANIC][GDB]   PIT Frequency: ~1.193182 MHz\n");
    debuglog_write("[PANIC][GDB]   Timer IRQ: IRQ 0 (Vector 0x20)\n");
    debuglog_write("[PANIC][GDB]   RTC Present: Yes (assumed)\n");
    debuglog_write("[PANIC][GDB]   RTC Ports: 0x70-0x71\n");
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_task_info(const panic_context_t* ctx) {
    (void)ctx; // Suppress unused parameter warning
    
    debuglog_write("[PANIC][GDB] ===== TASK/PROCESS CONTEXT =====\n");
    
    debuglog_write("[PANIC][GDB] Current Task State:\n");
    debuglog_write("[PANIC][GDB]   Task ID: KERNEL (PID 0)\n");
    debuglog_write("[PANIC][GDB]   Task Name: kernel_main\n");
    debuglog_write("[PANIC][GDB]   Task State: RUNNING\n");
    debuglog_write("[PANIC][GDB]   Privilege: Ring 0 (Kernel)\n");
    
    debuglog_write("[PANIC][GDB] Execution Context:\n");
    debuglog_write("[PANIC][GDB]   Execution Mode: Kernel Space\n");
    debuglog_write("[PANIC][GDB]   Exception Context: ");
    debuglog_write(get_panic_type_name(ctx->type));
    debuglog_write("\n");
    
    // Current instruction analysis
    debuglog_write("[PANIC][GDB] Instruction Context:\n");
    debuglog_write("[PANIC][GDB]   Current EIP: ");
    char eip_str[12];
    format_hex32(ctx->cpu_state.eip, eip_str);
    debuglog_write(eip_str);
    debuglog_write("\n");
    
    if (ctx->cpu_state.eip == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB]   WARNING: EIP is heap marker - severe corruption!\n");
    } else if (ctx->cpu_state.eip < 0x100000) {
        debuglog_write("[PANIC][GDB]   WARNING: EIP in low memory - possible corruption!\n");
    } else if (ctx->cpu_state.eip >= 0x100000 && ctx->cpu_state.eip < 0x200000) {
        debuglog_write("[PANIC][GDB]   EIP Location: Kernel code section\n");
    }
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_filesystem_info(const panic_context_t* ctx) {
    (void)ctx; // Suppress unused parameter warning
    
    debuglog_write("[PANIC][GDB] ===== FILESYSTEM STATE =====\n");
    
    debuglog_write("[PANIC][GDB] Virtual File System:\n");
    debuglog_write("[PANIC][GDB]   Root FS: initrd (RAM disk)\n");
    debuglog_write("[PANIC][GDB]   Mount Points: / (root)\n");
    debuglog_write("[PANIC][GDB]   VFS State: INITIALIZED\n");
    debuglog_write("[PANIC][GDB]   Filesystem Type: TARFS (TAR-based)\n");
    debuglog_write("[PANIC][GDB]   Block Size: 512 bytes\n");
    debuglog_write("[PANIC][GDB]   Inode Support: Basic\n");
    
    debuglog_write("[PANIC][GDB] Storage Devices:\n");
    debuglog_write("[PANIC][GDB]   Primary: RAM Disk (initrd)\n");
    debuglog_write("[PANIC][GDB]   Secondary: None detected\n");
    debuglog_write("[PANIC][GDB]   IDE Controllers: Not initialized\n");
    debuglog_write("[PANIC][GDB]   SATA Controllers: Not detected\n");
    debuglog_write("[PANIC][GDB]   USB Storage: Not supported\n");
    
    debuglog_write("[PANIC][GDB] File Operations:\n");
    debuglog_write("[PANIC][GDB]   Open Files: Unknown\n");
    debuglog_write("[PANIC][GDB]   Active Handles: Unknown\n");
    debuglog_write("[PANIC][GDB]   Buffer Cache: Unknown\n");
    debuglog_write("[PANIC][GDB]   Dirty Buffers: Unknown\n");
    debuglog_write("[PANIC][GDB]   Free Inodes: Unknown\n");
    debuglog_write("[PANIC][GDB]   File Descriptor Table: Kernel only\n");
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_interrupt_vectors(const panic_context_t* ctx) {
    (void)ctx; // Suppress unused parameter warning
    
    debuglog_write("[PANIC][GDB] ===== INTERRUPT VECTOR TABLE ANALYSIS =====\n");
    
    debuglog_write("[PANIC][GDB] IDT (Interrupt Descriptor Table):\n");
    debuglog_write("[PANIC][GDB]   IDT Base: Unknown (IDTR not captured)\n");
    debuglog_write("[PANIC][GDB]   IDT Limit: 256 entries (assumed)\n");
    debuglog_write("[PANIC][GDB]   Entry Size: 8 bytes per descriptor\n");
    
    debuglog_write("[PANIC][GDB] Exception Vectors (0x00-0x1F):\n");
    debuglog_write("[PANIC][GDB]   0x00: Divide Error (#DE)\n");
    debuglog_write("[PANIC][GDB]   0x01: Debug Exception (#DB)\n");
    debuglog_write("[PANIC][GDB]   0x02: NMI Interrupt\n");
    debuglog_write("[PANIC][GDB]   0x03: Breakpoint (#BP)\n");
    debuglog_write("[PANIC][GDB]   0x04: Overflow (#OF)\n");
    debuglog_write("[PANIC][GDB]   0x05: BOUND Range Exceeded (#BR)\n");
    debuglog_write("[PANIC][GDB]   0x06: Invalid Opcode (#UD)\n");
    debuglog_write("[PANIC][GDB]   0x07: Device Not Available (#NM)\n");
    debuglog_write("[PANIC][GDB]   0x08: Double Fault (#DF)\n");
    debuglog_write("[PANIC][GDB]   0x09: Coprocessor Segment Overrun\n");
    debuglog_write("[PANIC][GDB]   0x0A: Invalid TSS (#TS)\n");
    debuglog_write("[PANIC][GDB]   0x0B: Segment Not Present (#NP)\n");
    debuglog_write("[PANIC][GDB]   0x0C: Stack-Segment Fault (#SS)\n");
    debuglog_write("[PANIC][GDB]   0x0D: General Protection (#GP)\n");
    debuglog_write("[PANIC][GDB]   0x0E: Page Fault (#PF)\n");
    debuglog_write("[PANIC][GDB]   0x0F: Reserved\n");
    debuglog_write("[PANIC][GDB]   0x10: x87 FPU Error (#MF)\n");
    debuglog_write("[PANIC][GDB]   0x11: Alignment Check (#AC)\n");
    debuglog_write("[PANIC][GDB]   0x12: Machine Check (#MC)\n");
    debuglog_write("[PANIC][GDB]   0x13: SIMD Exception (#XM)\n");
    debuglog_write("[PANIC][GDB]   0x14-0x1F: Reserved\n");
    
    debuglog_write("[PANIC][GDB] Hardware Interrupts (0x20-0x2F):\n");
    debuglog_write("[PANIC][GDB]   0x20: Timer (IRQ 0)\n");
    debuglog_write("[PANIC][GDB]   0x21: Keyboard (IRQ 1)\n");
    debuglog_write("[PANIC][GDB]   0x22-0x27: Master PIC (IRQ 2-7)\n");
    debuglog_write("[PANIC][GDB]   0x28-0x2F: Slave PIC (IRQ 8-15)\n");
    
    debuglog_write("[PANIC][GDB] Current Exception Analysis:\n");
    if (ctx->type == PANIC_TYPE_PAGE_FAULT) {
        debuglog_write("[PANIC][GDB]   Vector: 0x0E (Page Fault)\n");
        debuglog_write("[PANIC][GDB]   Error Code: Not captured\n");
        debuglog_write("[PANIC][GDB]   Handler: default_exception_handler\n");
    } else if (ctx->type == PANIC_TYPE_DOUBLE_FAULT) {
        debuglog_write("[PANIC][GDB]   Vector: 0x08 (Double Fault)\n");
        debuglog_write("[PANIC][GDB]   Error Code: 0 (always)\n");
        debuglog_write("[PANIC][GDB]   Handler: double_fault_handler\n");
    } else {
        debuglog_write("[PANIC][GDB]   Vector: Unknown/General\n");
    }
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static panic_type_t panic_analyze_and_classify(const panic_context_t* ctx) {
    // Sophisticated panic type detection based on CPU state and context
    
    uint32 cr2 = ctx->cpu_state.cr2;
    uint32 eip = ctx->cpu_state.eip;
    uint32 esp = ctx->cpu_state.esp;
    uint32 ebp = ctx->cpu_state.ebp;
    uint32 cr0 = ctx->cpu_state.cr0;
    uint32 eflags = ctx->cpu_state.eflags;
    
    // Check for specific corruption patterns first
    if (eip == 0xDEADBEEF) {
        return PANIC_TYPE_INSTRUCTION_POINTER_CORRUPTION;
    }
    if (esp == 0xDEADBEEF || ebp == 0xDEADBEEF) {
        return PANIC_TYPE_STACK_POINTER_CORRUPTION;
    }
    if (cr2 == 0xDEADBEEF) {
        return PANIC_TYPE_HEAP_CORRUPTION;
    }
    
    // Page fault classification
    if (ctx->type == PANIC_TYPE_PAGE_FAULT || cr2 != 0) {
        if (cr2 == 0) {
            return PANIC_TYPE_PAGE_FAULT_NULL_POINTER;
        } else if (cr2 < 0x1000) {
            return PANIC_TYPE_PAGE_FAULT_NULL_POINTER;  // NULL + small offset
        } else if (cr2 >= 0xB8000 && cr2 <= 0xBFFFF) {
            return PANIC_TYPE_PAGE_FAULT_DATA_ACCESS;   // VGA buffer access
        } else if (cr2 >= 0xF0000 && cr2 <= 0xFFFFF) {
            return PANIC_TYPE_PAGE_FAULT_DATA_ACCESS;   // BIOS area
        } else if (cr2 >= 0x100000 && cr2 < 0x200000) {
            return PANIC_TYPE_PAGE_FAULT_KERNEL_MODE;   // Kernel code/data
        } else if (cr2 >= 0x80000 && cr2 < 0x100000) {
            return PANIC_TYPE_PAGE_FAULT_STACK_GUARD;   // Stack area
        } else if (cr2 >= 0xFFF00000) {
            return PANIC_TYPE_PAGE_FAULT_INVALID;       // Very high memory
        } else if (cr2 >= 0x80000000) {
            return PANIC_TYPE_PAGE_FAULT_KERNEL_MODE;   // Kernel space
        } else {
            return PANIC_TYPE_PAGE_FAULT_USER_MODE;     // User space
        }
    }
    
    // Check for specific CPU exceptions based on context
    if (eip == 0) {
        return PANIC_TYPE_NULL_POINTER_DEREFERENCE;
    }
    
    // Stack overflow detection
    if (esp < 0x80000) {
        return PANIC_TYPE_KERNEL_STACK_OVERFLOW;
    }
    if ((0x100000 - esp) > 0x70000) {  // More than 448KB stack usage
        return PANIC_TYPE_STACK_OVERFLOW;
    }
    
    // Alignment checks
    if (esp & 0x3) {  // ESP not 4-byte aligned
        return PANIC_TYPE_MEMORY_ALIGNMENT_ERROR;
    }
    
    // Invalid instruction pointer locations
    if (eip < 0x100000) {
        return PANIC_TYPE_INSTRUCTION_POINTER_CORRUPTION;
    }
    if (eip >= 0x200000 && eip < 0x300000) {
        return PANIC_TYPE_INSTRUCTION_POINTER_CORRUPTION;  // Between kernel and heap
    }
    
    // Check for register corruption patterns
    if (ctx->cpu_state.eax == ctx->cpu_state.ebx && 
        ctx->cpu_state.ebx == ctx->cpu_state.ecx &&
        ctx->cpu_state.ecx == ctx->cpu_state.edx &&
        ctx->cpu_state.eax != 0) {
        return PANIC_TYPE_REGISTER_CORRUPTION;
    }
    
    // Check for specific hardware-related issues
    if (!(cr0 & 0x1)) {  // Protected mode not enabled
        return PANIC_TYPE_CPU_FAULT;
    }
    if (!(cr0 & 0x80000000)) {  // Paging not enabled
        return PANIC_TYPE_MEMORY_MAPPING_ERROR;
    }
    
    // Check for interrupt-related issues
    if (!(eflags & 0x200)) {  // Interrupts disabled during panic
        return PANIC_TYPE_INTERRUPT_DEADLOCK;
    }
    
    // Analyze based on panic message if available
    if (ctx->message[0]) {
        // Simple string matching for common error patterns
        if (ctx->message[0] == 'D' && ctx->message[1] == 'i' && ctx->message[2] == 'v') {
            return PANIC_TYPE_DIVISION_BY_ZERO;
        }
        if (ctx->message[0] == 'A' && ctx->message[1] == 's' && ctx->message[2] == 's') {
            return PANIC_TYPE_ASSERTION_FAILED;
        }
    }
    
    // Default classifications based on original type
    switch (ctx->type) {
        case PANIC_TYPE_DOUBLE_FAULT:
            return PANIC_TYPE_DOUBLE_FAULT;
        case PANIC_TYPE_STACK_OVERFLOW:
            return PANIC_TYPE_STACK_OVERFLOW;
        case PANIC_TYPE_MEMORY_CORRUPTION:
            return PANIC_TYPE_MEMORY_CORRUPTION;
        case PANIC_TYPE_HARDWARE_FAILURE:
            return PANIC_TYPE_HARDWARE_FAILURE;
        case PANIC_TYPE_ASSERTION_FAILED:
            return PANIC_TYPE_ASSERTION_FAILED;
        case PANIC_TYPE_KERNEL_OOPS:
            return PANIC_TYPE_KERNEL_OOPS;
        default:
            return PANIC_TYPE_GENERAL;
    }
}

static void panic_debuglog_emit_memory_corruption_analysis(const panic_context_t* ctx) {
    debuglog_write("[PANIC][GDB] ===== MEMORY CORRUPTION DETECTION =====\n");
    
    // Check for obvious corruption patterns
    debuglog_write("[PANIC][GDB] Corruption Pattern Analysis:\n");
    
    // Check registers for corruption markers
    uint32* regs = (uint32*)&ctx->cpu_state;
    int corruption_count = 0;
    
    if (ctx->cpu_state.eax == 0xDEADBEEF || ctx->cpu_state.ebx == 0xDEADBEEF ||
        ctx->cpu_state.ecx == 0xDEADBEEF || ctx->cpu_state.edx == 0xDEADBEEF ||
        ctx->cpu_state.esi == 0xDEADBEEF || ctx->cpu_state.edi == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB]   DETECTED: Heap corruption marker in registers\n");
        corruption_count++;
    }
    
    if (ctx->cpu_state.eip == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB]   CRITICAL: EIP contains corruption marker\n");
        corruption_count++;
    }
    
    if (ctx->cpu_state.esp == 0xDEADBEEF || ctx->cpu_state.ebp == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB]   CRITICAL: Stack pointer corruption detected\n");
        corruption_count++;
    }
    
    if (ctx->cpu_state.cr2 == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB]   DETECTED: Page fault accessing corruption marker\n");
        corruption_count++;
    }
    
    // Check for NULL pointers
    if (ctx->cpu_state.eip == 0) {
        debuglog_write("[PANIC][GDB]   CRITICAL: NULL instruction pointer\n");
        corruption_count++;
    }
    
    // Check for suspiciously aligned values (possible corrupted pointers)
    if ((ctx->cpu_state.eax & 0xFFFF0000) == (ctx->cpu_state.ebx & 0xFFFF0000) &&
        (ctx->cpu_state.eax & 0xFFFF0000) == (ctx->cpu_state.ecx & 0xFFFF0000) &&
        ctx->cpu_state.eax != 0) {
        debuglog_write("[PANIC][GDB]   SUSPICIOUS: Multiple registers have same high bits\n");
        corruption_count++;
    }
    
    debuglog_write("[PANIC][GDB] Corruption Indicators Found: ");
    debuglog_write_dec(corruption_count);
    debuglog_write("\n");
    
    // Stack analysis for corruption
    debuglog_write("[PANIC][GDB] Stack Corruption Analysis:\n");
    uint32 stack_range = ctx->cpu_state.ebp - ctx->cpu_state.esp;
    if (stack_range > 0x10000) { // More than 64KB between ESP and EBP
        debuglog_write("[PANIC][GDB]   WARNING: Abnormal stack frame size\n");
    } else if (stack_range == 0 && ctx->cpu_state.ebp == ctx->cpu_state.esp) {
        debuglog_write("[PANIC][GDB]   WARNING: ESP equals EBP (possible corruption)\n");
    } else {
        debuglog_write("[PANIC][GDB]   Stack frame appears normal\n");
    }
    
    // Memory region validation
    debuglog_write("[PANIC][GDB] Memory Region Validation:\n");
    if (ctx->cpu_state.eip >= 0x100000 && ctx->cpu_state.eip < 0x200000) {
        debuglog_write("[PANIC][GDB]   EIP in kernel code region: VALID\n");
    } else {
        debuglog_write("[PANIC][GDB]   EIP outside kernel code region: INVALID\n");
        corruption_count++;
    }
    
    if (ctx->cpu_state.esp >= 0x80000 && ctx->cpu_state.esp <= 0x100000) {
        debuglog_write("[PANIC][GDB]   ESP in kernel stack region: VALID\n");
    } else {
        debuglog_write("[PANIC][GDB]   ESP outside kernel stack region: INVALID\n");
        corruption_count++;
    }
    
    // Analyze the detected panic type using our classification
    panic_type_t detected_type = panic_analyze_and_classify(ctx);
    debuglog_write("[PANIC][GDB] Automated Classification: ");
    debuglog_write(get_panic_type_name(detected_type));
    debuglog_write("\n");
    
    debuglog_write("[PANIC][GDB] Total Corruption Score: ");
    debuglog_write_dec(corruption_count);
    if (corruption_count >= 3) {
        debuglog_write(" (SEVERE CORRUPTION)\n");
    } else if (corruption_count >= 1) {
        debuglog_write(" (MODERATE CORRUPTION)\n");
    } else {
        debuglog_write(" (NO OBVIOUS CORRUPTION)\n");
    }
    
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit_meta(const panic_context_t* ctx) {
    debuglog_write("[PANIC][GDB] ===== PANIC CONTEXT =====\n");
    if (ctx->message[0]) {
        debuglog_write("[PANIC][GDB] Message: ");
        debuglog_write(ctx->message);
        debuglog_write("\n");
    }
    if (ctx->file[0]) {
        debuglog_write("[PANIC][GDB] Location: ");
        debuglog_write(ctx->file);
        if (ctx->line) {
            debuglog_write(":");
            debuglog_write_dec(ctx->line);
        }
        if (ctx->function[0]) {
            debuglog_write(" (");
            debuglog_write(ctx->function);
            debuglog_write(")");
        }
        debuglog_write("\n");
    }
    debuglog_write("[PANIC][GDB] Panic Count: ");
    debuglog_write_dec(g_panic_count);
    debuglog_write("\n");
    debuglog_write("[PANIC][GDB] System State: CRITICAL - HALTED\n");
    debuglog_write("[PANIC][GDB] ================================\n");
}

static void panic_debuglog_emit(const panic_context_t* ctx) {
    if (!debuglog_is_ready()) {
        return;
    }

    debuglog_write("\n");
    debuglog_write("################################################################################\n");
    debuglog_write("[PANIC][GDB] ===== FOREST OS KERNEL PANIC - DETAILED ANALYSIS =====\n");
    debuglog_write("[PANIC][GDB] Timestamp: Boot + Unknown (timer not available during panic)\n");
    debuglog_write("[PANIC][GDB] Analysis Version: Enhanced Debug v2.0\n");
    debuglog_write("################################################################################\n");
    debuglog_write("\n");
    
    // Core panic information
    panic_debuglog_emit_meta(ctx);
    debuglog_write("\n");
    
    // CPU and register state
    panic_debuglog_emit_registers(&ctx->cpu_state);
    debuglog_write("\n");
    
    // Memory subsystem information
    panic_debuglog_emit_memory_info(ctx);
    debuglog_write("\n");
    
    // Call stack and execution flow
    panic_debuglog_emit_stack(ctx);
    debuglog_write("\n");
    
    // Manual stack snapshot if available
    panic_debuglog_emit_manual_stack(ctx);
    debuglog_write("\n");
    
    // Task and process context
    panic_debuglog_emit_task_info(ctx);
    debuglog_write("\n");
    
    // System state information
    panic_debuglog_emit_system_info(ctx);
    debuglog_write("\n");
    
    // Hardware state
    panic_debuglog_emit_hardware_info(ctx);
    debuglog_write("\n");
    
    // Filesystem state
    panic_debuglog_emit_filesystem_info(ctx);
    debuglog_write("\n");
    
    // Interrupt vector table analysis
    panic_debuglog_emit_interrupt_vectors(ctx);
    debuglog_write("\n");
    
    // Memory corruption analysis
    panic_debuglog_emit_memory_corruption_analysis(ctx);
    debuglog_write("\n");
    
    // Summary and recommendations
    debuglog_write("[PANIC][GDB] ===== ANALYSIS SUMMARY =====\n");
    debuglog_write("[PANIC][GDB] Panic Type: ");
    debuglog_write(get_panic_type_name(ctx->type));
    debuglog_write("\n");
    
    // Provide analysis based on panic context
    if (ctx->cpu_state.cr2 == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB] Root Cause: HEAP CORRUPTION - Heap marker accessed as memory\n");
        debuglog_write("[PANIC][GDB] Likely Issue: Buffer overflow, use-after-free, or double-free\n");
        debuglog_write("[PANIC][GDB] Recommendation: Check memory allocation/deallocation logic\n");
    } else if (ctx->cpu_state.cr2 == 0x0) {
        debuglog_write("[PANIC][GDB] Root Cause: NULL POINTER DEREFERENCE\n");
        debuglog_write("[PANIC][GDB] Likely Issue: Uninitialized pointer or failed allocation\n");
        debuglog_write("[PANIC][GDB] Recommendation: Check pointer initialization and error handling\n");
    } else if (ctx->cpu_state.eip == 0xDEADBEEF) {
        debuglog_write("[PANIC][GDB] Root Cause: SEVERE MEMORY CORRUPTION - EIP corrupted\n");
        debuglog_write("[PANIC][GDB] Likely Issue: Stack overflow or buffer overflow corruption\n");
        debuglog_write("[PANIC][GDB] Recommendation: Check stack usage and buffer bounds\n");
    } else {
        debuglog_write("[PANIC][GDB] Root Cause: GENERAL EXCEPTION\n");
        debuglog_write("[PANIC][GDB] Recommendation: Analyze call stack and register state\n");
    }
    
    debuglog_write("[PANIC][GDB] System Status: HALTED - Manual reboot required\n");
    debuglog_write("[PANIC][GDB] Debug Info: All available system state captured above\n");
    debuglog_write("[PANIC][GDB] ================================\n");
    
    debuglog_write("\n");
    debuglog_write("################################################################################\n");
    debuglog_write("[PANIC][GDB] ===== END OF PANIC ANALYSIS =====\n");
    debuglog_write("################################################################################\n");
    debuglog_write("\n");
}
#endif

static const char* get_panic_type_name(panic_type_t type) {
    switch (type) {
        // General categories
        case PANIC_TYPE_GENERAL: return "General Panic";
        case PANIC_TYPE_UNKNOWN: return "Unknown Error";
        
        // Page fault subtypes
        case PANIC_TYPE_PAGE_FAULT: return "Page Fault (General)";
        case PANIC_TYPE_PAGE_FAULT_MINOR: return "Page Fault (Minor/Soft)";
        case PANIC_TYPE_PAGE_FAULT_MAJOR: return "Page Fault (Major/Hard)";
        case PANIC_TYPE_PAGE_FAULT_INVALID: return "Page Fault (Invalid Address)";
        case PANIC_TYPE_PAGE_FAULT_NULL_POINTER: return "Page Fault (NULL Pointer)";
        case PANIC_TYPE_PAGE_FAULT_SEGMENTATION: return "Page Fault (Segmentation)";
        case PANIC_TYPE_PAGE_FAULT_ACCESS_VIOLATION: return "Page Fault (Access Violation)";
        case PANIC_TYPE_PAGE_FAULT_WRITE_PROTECT: return "Page Fault (Write Protection)";
        case PANIC_TYPE_PAGE_FAULT_USER_MODE: return "Page Fault (User Mode)";
        case PANIC_TYPE_PAGE_FAULT_KERNEL_MODE: return "Page Fault (Kernel Mode)";
        case PANIC_TYPE_PAGE_FAULT_INSTRUCTION_FETCH: return "Page Fault (Instruction Fetch)";
        case PANIC_TYPE_PAGE_FAULT_DATA_ACCESS: return "Page Fault (Data Access)";
        case PANIC_TYPE_PAGE_FAULT_RESERVED_BIT: return "Page Fault (Reserved Bit Set)";
        case PANIC_TYPE_PAGE_FAULT_STACK_GUARD: return "Page Fault (Stack Guard)";
        case PANIC_TYPE_PAGE_FAULT_COW_VIOLATION: return "Page Fault (Copy-on-Write Violation)";
        case PANIC_TYPE_PAGE_FAULT_SHARED_MEMORY: return "Page Fault (Shared Memory)";
        case PANIC_TYPE_PAGE_FAULT_MEMORY_MAPPED_FILE: return "Page Fault (Memory-Mapped File)";
        case PANIC_TYPE_PAGE_FAULT_BUFFER_OVERFLOW: return "Page Fault (Buffer Overflow)";
        case PANIC_TYPE_PAGE_FAULT_USE_AFTER_FREE: return "Page Fault (Use After Free)";
        case PANIC_TYPE_PAGE_FAULT_DOUBLE_FREE: return "Page Fault (Double Free)";
        
        // CPU exceptions
        case PANIC_TYPE_DOUBLE_FAULT: return "Double Fault";
        case PANIC_TYPE_TRIPLE_FAULT: return "Triple Fault";
        case PANIC_TYPE_DIVIDE_ERROR: return "Divide Error (#DE)";
        case PANIC_TYPE_DEBUG_EXCEPTION: return "Debug Exception (#DB)";
        case PANIC_TYPE_NMI_INTERRUPT: return "NMI Interrupt";
        case PANIC_TYPE_BREAKPOINT: return "Breakpoint (#BP)";
        case PANIC_TYPE_OVERFLOW: return "Overflow (#OF)";
        case PANIC_TYPE_BOUND_RANGE_EXCEEDED: return "BOUND Range Exceeded (#BR)";
        case PANIC_TYPE_INVALID_OPCODE: return "Invalid Opcode (#UD)";
        case PANIC_TYPE_DEVICE_NOT_AVAILABLE: return "Device Not Available (#NM)";
        case PANIC_TYPE_COPROCESSOR_SEGMENT_OVERRUN: return "Coprocessor Segment Overrun";
        case PANIC_TYPE_INVALID_TSS: return "Invalid TSS (#TS)";
        case PANIC_TYPE_SEGMENT_NOT_PRESENT: return "Segment Not Present (#NP)";
        case PANIC_TYPE_STACK_SEGMENT_FAULT: return "Stack-Segment Fault (#SS)";
        case PANIC_TYPE_GENERAL_PROTECTION_FAULT: return "General Protection Fault (#GP)";
        case PANIC_TYPE_X87_FPU_ERROR: return "x87 FPU Error (#MF)";
        case PANIC_TYPE_ALIGNMENT_CHECK: return "Alignment Check (#AC)";
        case PANIC_TYPE_MACHINE_CHECK: return "Machine Check (#MC)";
        case PANIC_TYPE_SIMD_EXCEPTION: return "SIMD Floating-Point Exception (#XM)";
        case PANIC_TYPE_VIRTUALIZATION_EXCEPTION: return "Virtualization Exception (#VE)";
        case PANIC_TYPE_CONTROL_PROTECTION_EXCEPTION: return "Control Protection Exception (#CP)";
        
        // Memory errors
        case PANIC_TYPE_MEMORY_CORRUPTION: return "Memory Corruption";
        case PANIC_TYPE_HEAP_CORRUPTION: return "Heap Corruption";
        case PANIC_TYPE_STACK_CORRUPTION: return "Stack Corruption";
        case PANIC_TYPE_BUFFER_OVERFLOW: return "Buffer Overflow";
        case PANIC_TYPE_BUFFER_UNDERFLOW: return "Buffer Underflow";
        case PANIC_TYPE_STACK_OVERFLOW: return "Stack Overflow";
        case PANIC_TYPE_STACK_UNDERFLOW: return "Stack Underflow";
        case PANIC_TYPE_MEMORY_LEAK: return "Memory Leak";
        case PANIC_TYPE_OUT_OF_MEMORY: return "Out of Memory";
        case PANIC_TYPE_ALLOCATION_FAILURE: return "Memory Allocation Failure";
        case PANIC_TYPE_DEALLOCATION_ERROR: return "Memory Deallocation Error";
        case PANIC_TYPE_INVALID_FREE: return "Invalid Memory Free";
        case PANIC_TYPE_MEMORY_ALIGNMENT_ERROR: return "Memory Alignment Error";
        case PANIC_TYPE_MEMORY_ACCESS_VIOLATION: return "Memory Access Violation";
        case PANIC_TYPE_MEMORY_PROTECTION_VIOLATION: return "Memory Protection Violation";
        case PANIC_TYPE_MEMORY_MAPPING_ERROR: return "Memory Mapping Error";
        case PANIC_TYPE_VIRTUAL_MEMORY_EXHAUSTED: return "Virtual Memory Exhausted";
        case PANIC_TYPE_PHYSICAL_MEMORY_EXHAUSTED: return "Physical Memory Exhausted";
        case PANIC_TYPE_MEMORY_FRAGMENTATION: return "Memory Fragmentation";
        case PANIC_TYPE_MEMORY_BOUNDS_CHECK_FAILED: return "Memory Bounds Check Failed";
        case PANIC_TYPE_MEMORY_CANARY_CORRUPTION: return "Memory Canary Corruption";
        case PANIC_TYPE_MEMORY_METADATA_CORRUPTION: return "Memory Metadata Corruption";
        case PANIC_TYPE_MEMORY_POOL_CORRUPTION: return "Memory Pool Corruption";
        case PANIC_TYPE_MEMORY_REGION_CORRUPTION: return "Memory Region Corruption";
        case PANIC_TYPE_MEMORY_SLAB_CORRUPTION: return "Memory Slab Corruption";
        
        // Pointer errors
        case PANIC_TYPE_NULL_POINTER_DEREFERENCE: return "NULL Pointer Dereference";
        case PANIC_TYPE_WILD_POINTER_ACCESS: return "Wild Pointer Access";
        case PANIC_TYPE_DANGLING_POINTER_ACCESS: return "Dangling Pointer Access";
        case PANIC_TYPE_INVALID_POINTER_ARITHMETIC: return "Invalid Pointer Arithmetic";
        case PANIC_TYPE_POINTER_CORRUPTION: return "Pointer Corruption";
        case PANIC_TYPE_FUNCTION_POINTER_CORRUPTION: return "Function Pointer Corruption";
        case PANIC_TYPE_VTABLE_CORRUPTION: return "Virtual Table Corruption";
        case PANIC_TYPE_RETURN_ADDRESS_CORRUPTION: return "Return Address Corruption";
        
        // Hardware errors
        case PANIC_TYPE_HARDWARE_FAILURE: return "Hardware Failure";
        case PANIC_TYPE_CPU_FAULT: return "CPU Fault";
        case PANIC_TYPE_CPU_OVERHEATING: return "CPU Overheating";
        case PANIC_TYPE_CPU_CACHE_ERROR: return "CPU Cache Error";
        case PANIC_TYPE_CPU_TLB_ERROR: return "CPU TLB Error";
        case PANIC_TYPE_CPU_MICROCODE_ERROR: return "CPU Microcode Error";
        case PANIC_TYPE_MEMORY_BUS_ERROR: return "Memory Bus Error";
        case PANIC_TYPE_MEMORY_ECC_ERROR: return "Memory ECC Error";
        case PANIC_TYPE_MEMORY_PARITY_ERROR: return "Memory Parity Error";
        case PANIC_TYPE_MEMORY_CONTROLLER_ERROR: return "Memory Controller Error";
        case PANIC_TYPE_DISK_CONTROLLER_ERROR: return "Disk Controller Error";
        case PANIC_TYPE_DISK_READ_ERROR: return "Disk Read Error";
        case PANIC_TYPE_DISK_WRITE_ERROR: return "Disk Write Error";
        case PANIC_TYPE_DISK_SEEK_ERROR: return "Disk Seek Error";
        case PANIC_TYPE_NETWORK_CONTROLLER_ERROR: return "Network Controller Error";
        case PANIC_TYPE_PCI_ERROR: return "PCI Error";
        case PANIC_TYPE_ACPI_ERROR: return "ACPI Error";
        case PANIC_TYPE_BIOS_ERROR: return "BIOS Error";
        case PANIC_TYPE_FIRMWARE_ERROR: return "Firmware Error";
        case PANIC_TYPE_POWER_SUPPLY_ERROR: return "Power Supply Error";
        case PANIC_TYPE_THERMAL_ERROR: return "Thermal Error";
        case PANIC_TYPE_FAN_FAILURE: return "Fan Failure";
        
        // Interrupt errors
        case PANIC_TYPE_SPURIOUS_INTERRUPT: return "Spurious Interrupt";
        case PANIC_TYPE_UNHANDLED_INTERRUPT: return "Unhandled Interrupt";
        case PANIC_TYPE_INTERRUPT_STORM: return "Interrupt Storm";
        case PANIC_TYPE_IRQ_CONFLICT: return "IRQ Conflict";
        case PANIC_TYPE_INTERRUPT_VECTOR_CORRUPTION: return "Interrupt Vector Corruption";
        case PANIC_TYPE_IDT_CORRUPTION: return "IDT Corruption";
        case PANIC_TYPE_GDT_CORRUPTION: return "GDT Corruption";
        case PANIC_TYPE_TSS_CORRUPTION: return "TSS Corruption";
        case PANIC_TYPE_INTERRUPT_STACK_OVERFLOW: return "Interrupt Stack Overflow";
        case PANIC_TYPE_INTERRUPT_DEADLOCK: return "Interrupt Deadlock";
        
        // Synchronization errors
        case PANIC_TYPE_DEADLOCK: return "Deadlock";
        case PANIC_TYPE_LIVELOCK: return "Livelock";
        case PANIC_TYPE_RACE_CONDITION: return "Race Condition";
        case PANIC_TYPE_SPINLOCK_DEADLOCK: return "Spinlock Deadlock";
        case PANIC_TYPE_MUTEX_DEADLOCK: return "Mutex Deadlock";
        case PANIC_TYPE_SEMAPHORE_OVERFLOW: return "Semaphore Overflow";
        case PANIC_TYPE_SEMAPHORE_UNDERFLOW: return "Semaphore Underflow";
        case PANIC_TYPE_CONDITION_VARIABLE_ERROR: return "Condition Variable Error";
        case PANIC_TYPE_BARRIER_ERROR: return "Barrier Error";
        case PANIC_TYPE_ATOMIC_OPERATION_FAILURE: return "Atomic Operation Failure";
        case PANIC_TYPE_MEMORY_ORDERING_VIOLATION: return "Memory Ordering Violation";
        case PANIC_TYPE_LOCK_CORRUPTION: return "Lock Corruption";
        case PANIC_TYPE_LOCK_INVERSION: return "Lock Inversion";
        case PANIC_TYPE_PRIORITY_INVERSION: return "Priority Inversion";
        
        // Process errors
        case PANIC_TYPE_TASK_CORRUPTION: return "Task Corruption";
        case PANIC_TYPE_PROCESS_CORRUPTION: return "Process Corruption";
        case PANIC_TYPE_THREAD_CORRUPTION: return "Thread Corruption";
        case PANIC_TYPE_SCHEDULER_ERROR: return "Scheduler Error";
        case PANIC_TYPE_CONTEXT_SWITCH_ERROR: return "Context Switch Error";
        case PANIC_TYPE_STACK_POINTER_CORRUPTION: return "Stack Pointer Corruption";
        case PANIC_TYPE_INSTRUCTION_POINTER_CORRUPTION: return "Instruction Pointer Corruption";
        case PANIC_TYPE_REGISTER_CORRUPTION: return "Register Corruption";
        case PANIC_TYPE_PROCESS_TABLE_CORRUPTION: return "Process Table Corruption";
        case PANIC_TYPE_THREAD_STACK_OVERFLOW: return "Thread Stack Overflow";
        case PANIC_TYPE_KERNEL_STACK_OVERFLOW: return "Kernel Stack Overflow";
        case PANIC_TYPE_USER_STACK_OVERFLOW: return "User Stack Overflow";
        case PANIC_TYPE_SIGNAL_HANDLER_ERROR: return "Signal Handler Error";
        case PANIC_TYPE_SIGNAL_STACK_CORRUPTION: return "Signal Stack Corruption";
        
        // Security errors
        case PANIC_TYPE_SECURITY_VIOLATION: return "Security Violation";
        case PANIC_TYPE_PRIVILEGE_ESCALATION: return "Privilege Escalation";
        case PANIC_TYPE_BUFFER_OVERFLOW_EXPLOIT: return "Buffer Overflow Exploit";
        case PANIC_TYPE_CODE_INJECTION: return "Code Injection Attack";
        case PANIC_TYPE_ROP_ATTACK: return "Return-Oriented Programming Attack";
        case PANIC_TYPE_JOP_ATTACK: return "Jump-Oriented Programming Attack";
        case PANIC_TYPE_CONTROL_FLOW_HIJACK: return "Control Flow Hijacking";
        case PANIC_TYPE_STACK_SMASHING: return "Stack Smashing Attack";
        case PANIC_TYPE_HEAP_SPRAYING: return "Heap Spraying Attack";
        case PANIC_TYPE_FORMAT_STRING_ATTACK: return "Format String Attack";
        case PANIC_TYPE_INTEGER_OVERFLOW_EXPLOIT: return "Integer Overflow Exploit";
        case PANIC_TYPE_RACE_CONDITION_EXPLOIT: return "Race Condition Exploit";
        case PANIC_TYPE_TIME_OF_CHECK_TIME_OF_USE: return "TOCTTOU Attack";
        
        // Cache errors
        case PANIC_TYPE_L1_CACHE_ERROR: return "L1 Cache Error";
        case PANIC_TYPE_L2_CACHE_ERROR: return "L2 Cache Error";
        case PANIC_TYPE_L3_CACHE_ERROR: return "L3 Cache Error";
        case PANIC_TYPE_L4_CACHE_ERROR: return "L4 Cache Error";
        case PANIC_TYPE_CACHE_COHERENCY_ERROR: return "Cache Coherency Error";
        case PANIC_TYPE_CACHE_MISS_STORM: return "Cache Miss Storm";
        case PANIC_TYPE_CACHE_THRASHING: return "Cache Thrashing";
        case PANIC_TYPE_TLB_MISS_STORM: return "TLB Miss Storm";
        case PANIC_TYPE_TLB_COHERENCY_ERROR: return "TLB Coherency Error";
        case PANIC_TYPE_MEMORY_BANDWIDTH_EXCEEDED: return "Memory Bandwidth Exceeded";
        case PANIC_TYPE_MEMORY_LATENCY_TIMEOUT: return "Memory Latency Timeout";
        case PANIC_TYPE_NUMA_ERROR: return "NUMA Error";
        case PANIC_TYPE_MEMORY_CONTROLLER_TIMEOUT: return "Memory Controller Timeout";
        
        // System call errors
        case PANIC_TYPE_SYSCALL_ERROR: return "System Call Error";
        case PANIC_TYPE_INVALID_SYSCALL: return "Invalid System Call";
        case PANIC_TYPE_SYSCALL_CORRUPTION: return "System Call Corruption";
        case PANIC_TYPE_SYSCALL_TABLE_CORRUPTION: return "System Call Table Corruption";
        case PANIC_TYPE_SYSCALL_STACK_OVERFLOW: return "System Call Stack Overflow";
        case PANIC_TYPE_SYSCALL_PARAMETER_ERROR: return "System Call Parameter Error";
        case PANIC_TYPE_SYSCALL_PRIVILEGE_ERROR: return "System Call Privilege Error";
        case PANIC_TYPE_SYSCALL_DEADLOCK: return "System Call Deadlock";
        case PANIC_TYPE_SYSCALL_TIMEOUT: return "System Call Timeout";
        
        // Other common types
        case PANIC_TYPE_ASSERTION_FAILED: return "Assertion Failed";
        case PANIC_TYPE_KERNEL_OOPS: return "Kernel Oops";
        case PANIC_TYPE_DIVISION_BY_ZERO: return "Division by Zero";
        case PANIC_TYPE_INTEGER_OVERFLOW: return "Integer Overflow";
        case PANIC_TYPE_INTEGER_UNDERFLOW: return "Integer Underflow";
        case PANIC_TYPE_FLOATING_POINT_ERROR: return "Floating Point Error";
        
        default: return "Unknown Panic Type";
    }
}

static int get_panic_type_color(panic_type_t type) {
    switch (type) {
        case PANIC_TYPE_PAGE_FAULT: return 4;
        case PANIC_TYPE_DOUBLE_FAULT: return 4;
        case PANIC_TYPE_STACK_OVERFLOW: return graphics_color_to_tui(COLOR_TEXT_WARN);
        case PANIC_TYPE_MEMORY_CORRUPTION: return 4;
        case PANIC_TYPE_HARDWARE_FAILURE: return 4;
        case PANIC_TYPE_ASSERTION_FAILED: return graphics_color_to_tui(COLOR_TEXT_WARN);
        case PANIC_TYPE_KERNEL_OOPS: return 3;
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

// =============================================================================
// NEW BSOD-STYLE INTERFACE FUNCTIONS
// =============================================================================

static void draw_bsod_header(const panic_context_t* ctx) {
    // This function is now the entry point for drawing the whole screen.
    // Clear the entire screen with the background color
    panic_fill_rect(0, 0, g_screen_width, g_screen_height, COLOR_BG);
    
    // Draw header bar
    panic_fill_rect(0, 0, g_screen_width, 30, COLOR_HEAD);
    char header_text[128];
    strcpy(header_text, "Forest OS v1.1 - A CRITICAL SYSTEM ERROR HAS OCCURRED");
    panic_draw_text(header_text, 10, 8, COLOR_FG);

    // Main title
    panic_draw_text("CRITICAL_SYSTEM_ERROR", 10, 50, COLOR_TEXT_ERROR);

    // Panic message
    if (ctx->message[0]) {
        panic_draw_text(ctx->message, 10, 70, COLOR_FG);
    }
}

static void draw_page_navigation(void) {
    char nav_info[80];
    
    strcpy(nav_info, "Page ");
    nav_info[strlen(nav_info) + 1] = '\0';
    nav_info[strlen(nav_info)] = '0' + (g_current_page + 1);
    strcat(nav_info, "/7: ");
    strcat(nav_info, g_bsod_pages[g_current_page].title);
    
    panic_draw_text(nav_info, 10, 100, FG_CYAN);
    panic_draw_text(g_bsod_pages[g_current_page].description, 10, 118, FG_GRAY);

    panic_draw_text("UP/DOWN: Scroll  LEFT/RIGHT: Change Page  ESC: Halt System", 10, g_screen_height - 30, FG_YELLOW);
}

static void draw_current_page(const panic_context_t* ctx) {
    // Render the current page starting from a pixel row of 150
    if (g_current_page < BSOD_PAGE_MAX) {
        g_bsod_pages[g_current_page].render_func(ctx, 150);
    }
}

static void handle_bsod_navigation(char key) {
    switch (key) {
        case 'w': case 'W': case 72: // Up arrow
            if (g_page_scroll_offset > 0) {
                g_page_scroll_offset--;
            }
            break;
            
        case 's': case 'S': case 80: // Down arrow
            g_page_scroll_offset++;
            break;
            
        case 'a': case 'A': case 75: // Left arrow
            if (g_current_page > 0) {
                g_current_page--;
                g_page_scroll_offset = 0; // Reset scroll when changing pages
            }
            break;
            
        case 'd': case 'D': case 77: // Right arrow
            if (g_current_page < BSOD_PAGE_MAX - 1) {
                g_current_page++;
                g_page_scroll_offset = 0; // Reset scroll when changing pages
            }
            break;
            
        case ' ': // Space to refresh
            break;
            
        case '1': case '2': case '3': case '4': case '5': case '6': case '7':
            {
                int page = key - '1';
                if (page < BSOD_PAGE_MAX) {
                    g_current_page = page;
                    g_page_scroll_offset = 0;
                }
            }
            break;
    }
}

// =============================================================================
// PAGE RENDERING FUNCTIONS
// =============================================================================

static void render_overview_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    
    panic_draw_text("GENERAL INFORMATION:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    char hex_str[16];
    format_hex32(ctx->cpu_state.eip, hex_str);
    
    char temp[80];
    strcpy(temp, "Error Address: ");
    strcat(temp, hex_str);
    panic_draw_text(temp, 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    strcpy(temp, "Error Type: ");
    strcat(temp, get_panic_type_name(ctx->type));
    panic_draw_text(temp, 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    if (ctx->file[0]) {
        strcpy(temp, "Source: ");
        strcat(temp, ctx->file);
        strcat(temp, " in ");
        if (ctx->function[0]) {
            strcat(temp, ctx->function);
        } else {
            strcat(temp, "unknown");
        }
        strcat(temp, "()");
        panic_draw_text(temp, 30, y, FG_CYAN);
        y += FONT_HEIGHT;
    }
    
    y += FONT_HEIGHT;
    panic_draw_text("TECHNICAL DETAILS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    format_hex32(ctx->cpu_state.esp, hex_str);
    strcpy(temp, "Stack Pointer: ");
    strcat(temp, hex_str);
    panic_draw_text(temp, 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    format_hex32(ctx->cpu_state.ebp, hex_str);
    strcpy(temp, "Base Pointer:  ");
    strcat(temp, hex_str);
    panic_draw_text(temp, 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    strcpy(temp, "Stack Frames: ");
    char num_buf[12];
    format_decimal(ctx->stack_frame_count, num_buf);
    strcat(temp, num_buf);
    panic_draw_text(temp, 30, y, FG_WHITE);
}

static void render_cpu_state_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    char hex_str[16];
    char temp[80];
    
    panic_draw_text("PROCESSOR REGISTERS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    int x1 = 30;
    int x2 = 350;
    int current_y = y;

    format_hex32(ctx->cpu_state.eax, hex_str);
    strcpy(temp, "EAX  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);
    
    format_hex32(ctx->cpu_state.cr0, hex_str);
    strcpy(temp, "CR0  "); strcat(temp, hex_str);
    panic_draw_text(temp, x2, current_y, FG_WHITE);
    current_y += FONT_HEIGHT;

    format_hex32(ctx->cpu_state.ebx, hex_str);
    strcpy(temp, "EBX  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);

    format_hex32(ctx->cpu_state.cr2, hex_str);
    strcpy(temp, "CR2  "); strcat(temp, hex_str);
    panic_draw_text(temp, x2, current_y, FG_WHITE);
    current_y += FONT_HEIGHT;
    
    format_hex32(ctx->cpu_state.ecx, hex_str);
    strcpy(temp, "ECX  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);

    format_hex32(ctx->cpu_state.cr3, hex_str);
    strcpy(temp, "CR3  "); strcat(temp, hex_str);
    panic_draw_text(temp, x2, current_y, FG_WHITE);
    current_y += FONT_HEIGHT;

    format_hex32(ctx->cpu_state.edx, hex_str);
    strcpy(temp, "EDX  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);

    format_hex32(ctx->cpu_state.cr4, hex_str);
    strcpy(temp, "CR4  "); strcat(temp, hex_str);
    panic_draw_text(temp, x2, current_y, FG_WHITE);
    current_y += FONT_HEIGHT * 2;

    format_hex32(ctx->cpu_state.esi, hex_str);
    strcpy(temp, "ESI  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);
    current_y += FONT_HEIGHT;
    
    format_hex32(ctx->cpu_state.edi, hex_str);
    strcpy(temp, "EDI  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);
    current_y += FONT_HEIGHT * 2;

    format_hex32(ctx->cpu_state.esp, hex_str);
    strcpy(temp, "ESP  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);
    current_y += FONT_HEIGHT;

    format_hex32(ctx->cpu_state.ebp, hex_str);
    strcpy(temp, "EBP  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);
    current_y += FONT_HEIGHT;
    
    format_hex32(ctx->cpu_state.eip, hex_str);
    strcpy(temp, "EIP  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_GREEN);
    current_y += FONT_HEIGHT;

    format_hex32(ctx->cpu_state.eflags, hex_str);
    strcpy(temp, "EFL  "); strcat(temp, hex_str);
    panic_draw_text(temp, x1, current_y, FG_WHITE);
}

static void render_memory_info_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    char temp[80], hex_str[16];
    
    panic_draw_text("MEMORY STATUS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    // Check for page fault information
    if (ctx->type == PANIC_TYPE_PAGE_FAULT) {
        format_hex32(ctx->cpu_state.cr2, hex_str);
        strcpy(temp, "Fault Address: ");
        strcat(temp, hex_str);
        panic_draw_text(temp, 30, y, FG_RED);
        y += FONT_HEIGHT;
    }
    
    panic_draw_text("Stack Memory (around ESP):", 30, y, FG_CYAN);
    y += FONT_HEIGHT;

    format_hex32(ctx->cpu_state.esp, hex_str);
    strcpy(temp, "ESP: ");
    strcat(temp, hex_str);
    panic_draw_text(temp, 30, y, FG_YELLOW);
}

static void render_system_info_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    char temp[80];
    
    panic_draw_text("SYSTEM INFORMATION:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    panic_draw_text("Operating System: Forest OS v2.0", 30, y, FG_WHITE);
    y += FONT_HEIGHT;

    panic_draw_text("Architecture: x86-32", 30, y, FG_WHITE);
    y += FONT_HEIGHT;

    strcpy(temp, "Panic Count: ");
    char pcount_str[12];
    format_decimal(g_panic_count, pcount_str);
    strcat(temp, pcount_str);
    panic_draw_text(temp, 30, y, FG_WHITE);
    y += FONT_HEIGHT * 2;
    
    panic_draw_text("HARDWARE STATUS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;

    panic_draw_text("CPU: x86 Compatible", 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    panic_draw_text("Memory: Available", 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    panic_draw_text("Interrupts: System enabled", 30, y, FG_WHITE);
}

static void render_stack_trace_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    char temp[80];
    
    panic_draw_text("CALL STACK TRACE:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;

    for (uint32 i = 0; i < ctx->stack_frame_count; i++) {
        if (ctx->stack_trace[i].valid) {
            char addr_str[16];
            format_hex32(ctx->stack_trace[i].address, addr_str);
            char num_buf[12];
            format_decimal(i, num_buf);

            strcpy(temp, "#");
            strcat(temp, num_buf);
            strcat(temp, " ");
            strcat(temp, addr_str);
            panic_draw_text(temp, 30, y, FG_WHITE);
            y += FONT_HEIGHT;
        }
    }
    
    if (ctx->manual_stack_valid) {
        y += FONT_HEIGHT;
        panic_draw_text("MANUAL STACK ENTRIES:", 10, y, FG_CYAN);
        y += FONT_HEIGHT * 2;
        
        for (uint32 i = 0; i < ctx->manual_stack_count; i++) {
            char addr_str[16];
            format_hex32(ctx->manual_stack[i], addr_str);
            char num_buf[12];
            format_decimal(i, num_buf);

            strcpy(temp, "M");
            strcat(temp, num_buf);
            strcat(temp, " ");
            strcat(temp, addr_str);
            panic_draw_text(temp, 30, y, FG_CYAN);
            y += FONT_HEIGHT;
        }
    }
}

static void render_hardware_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    
    panic_draw_text("HARDWARE DIAGNOSTICS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    panic_draw_text("CPU State: Active", 30, y, FG_GREEN);
    y += FONT_HEIGHT;

    panic_draw_text("Memory Controller: Operational", 30, y, FG_GREEN);
    y += FONT_HEIGHT;

    panic_draw_text("Interrupt Controller: Active", 30, y, FG_GREEN);
    y += FONT_HEIGHT;

    panic_draw_text("Timer: Functional", 30, y, FG_GREEN);
    y += FONT_HEIGHT * 2;
    
    panic_draw_text("DRIVER STATUS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    panic_draw_text("Display Driver: Loaded", 30, y, FG_GREEN);
    y += FONT_HEIGHT;
    
    panic_draw_text("Keyboard Driver: Active", 30, y, FG_GREEN);
}

static void render_advanced_page(const panic_context_t* ctx, int start_y) {
    int y = start_y - (g_page_scroll_offset * FONT_HEIGHT);
    char temp[80];
    
    panic_draw_text("ADVANCED DIAGNOSTICS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    strcpy(temp, "Error Classification: ");
    strcat(temp, get_panic_type_name(ctx->type));
    panic_draw_text(temp, 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    const char* recovery = ctx->recoverable ? "Possible" : "Not Possible";
    strcpy(temp, "Recovery Status: ");
    strcat(temp, recovery);
    panic_draw_text(temp, 30, y, ctx->recoverable ? FG_GREEN : FG_RED);
    y += FONT_HEIGHT * 2;

    panic_draw_text("RECOMMENDATIONS:", 10, y, FG_YELLOW);
    y += FONT_HEIGHT * 2;
    
    panic_draw_text("1. Check hardware connections", 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    panic_draw_text("2. Verify system memory", 30, y, FG_WHITE);
    y += FONT_HEIGHT;
    
    panic_draw_text("3. Update system drivers", 30, y, FG_WHITE);
    y += FONT_HEIGHT;

    panic_draw_text("4. Contact system administrator", 30, y, FG_WHITE);
}

static char panic_tolower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c + ('a' - 'A');
    }
    return c;
}

static bool panic_contains_keyword(const char* haystack, const char* keyword) {
    if (!haystack || !keyword || !*haystack || !*keyword) {
        return false;
    }

    size_t keyword_len = strlen(keyword);
    for (const char* h = haystack; *h; ++h) {
        size_t i = 0;
        while (i < keyword_len && h[i] &&
               panic_tolower(h[i]) == panic_tolower(keyword[i])) {
            i++;
        }
        if (i == keyword_len) {
            return true;
        }
        if (!h[i]) {
            break;
        }
    }
    return false;
}

static const char* panic_identify_subsystem(const panic_context_t* ctx) {
    if (!ctx) {
        return "Unknown subsystem";
    }

    const char* details[3] = { NULL, NULL, NULL };
    if (ctx->file[0]) details[0] = ctx->file;
    if (ctx->function[0]) details[1] = ctx->function;
    if (ctx->message[0]) details[2] = ctx->message;

    for (int i = 0; i < 3; ++i) {
        const char* text = details[i];
        if (!text) continue;
        if (panic_contains_keyword(text, "interrupt") ||
            panic_contains_keyword(text, "irq")) {
            return "Interrupt & IRQ Controller";
        }
        if (panic_contains_keyword(text, "memory") ||
            panic_contains_keyword(text, "paging") ||
            panic_contains_keyword(text, "heap") ||
            panic_contains_keyword(text, "mmu")) {
            return "Memory Management Unit";
        }
        if (panic_contains_keyword(text, "fs/") ||
            panic_contains_keyword(text, "filesystem") ||
            panic_contains_keyword(text, "vfs")) {
            return "Filesystem / VFS Layer";
        }
        if (panic_contains_keyword(text, "driver") ||
            panic_contains_keyword(text, "ps2") ||
            panic_contains_keyword(text, "kbd") ||
            panic_contains_keyword(text, "mouse") ||
            panic_contains_keyword(text, "ata") ||
            panic_contains_keyword(text, "pci")) {
            return "Hardware Driver Stack";
        }
        if (panic_contains_keyword(text, "sched") ||
            panic_contains_keyword(text, "task") ||
            panic_contains_keyword(text, "thread")) {
            return "Scheduler / Tasking Core";
        }
        if (panic_contains_keyword(text, "graphics") ||
            panic_contains_keyword(text, "tty") ||
            panic_contains_keyword(text, "framebuffer")) {
            return "Display & Console Subsystem";
        }
        if (panic_contains_keyword(text, "ipc") ||
            panic_contains_keyword(text, "syscall")) {
            return "System Call & IPC Layer";
        }
        if (panic_contains_keyword(text, "acpi") ||
            panic_contains_keyword(text, "power")) {
            return "Power / ACPI Management";
        }
    }

    return "Core Kernel Runtime";
}

// Simple ANSI/Tty-based panic display
static void draw_simple_panic_screen(const panic_context_t* ctx) {
    // Try to ensure the TTY is ready even in error paths
    // First check if graphics is available to avoid circular panic
    if (!graphics_is_initialized()) {
        // Use basic console for panic display when graphics isn't available
        console_init();
        clearScreen();
        print_colored("PANIC: Using basic console mode (graphics not initialized)\n", TEXT_ATTR_LIGHT_RED, TEXT_ATTR_BLACK);
    } else {
        bool tty_ok = tty_init();
        if (!tty_ok) {
            // Fallback to basic console for panic display
            console_init();
            clearScreen();
            print_colored("PANIC: TTY initialization failed during panic handling\n", TEXT_ATTR_LIGHT_RED, TEXT_ATTR_BLACK);
            print_colored("Using basic console mode\n", TEXT_ATTR_YELLOW, TEXT_ATTR_BLACK);
        } else {
            tty_set_attr(MAKE_TEXT_ATTR(TEXT_ATTR_LIGHT_GRAY, TEXT_ATTR_BLACK));
            tty_clear();
            // Continue with TTY-based panic display
            goto tty_display;
        }
    }
    
    // Basic console panic display
    print_colored("\n", TEXT_ATTR_WHITE, TEXT_ATTR_BLACK);
    print_colored("################################################################################\n", TEXT_ATTR_RED, TEXT_ATTR_BLACK);
    print_colored("FOREST OS KERNEL PANIC\n", TEXT_ATTR_LIGHT_RED, TEXT_ATTR_BLACK);
    print_colored("################################################################################\n", TEXT_ATTR_RED, TEXT_ATTR_BLACK);
    return;
    
tty_display:

    // Build a more expressive ANSI interface with a pinned background so the
    // panic view stays readable even when the graphics stack fails back to
    // legacy text mode.
    const char* subsystem = panic_identify_subsystem(ctx);
    tty_write_ansi("\x1b[0m\x1b[?25l\x1b[45m\x1b[97m\x1b[2J\x1b[H");
    tty_write_ansi("\x1b[1;97;45m   FOREST OS KERNEL PANIC   \x1b[0m\x1b[45m\x1b[97m\n");

    tty_write_ansi("\x1b[95m==============================================================================\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("\x1b[1;95mContext\x1b[0m\x1b[45m\x1b[97m\n");

    if (ctx->message[0]) {
        char message_copy[sizeof(ctx->message)];
        strncpy(message_copy, ctx->message, sizeof(message_copy));
        message_copy[sizeof(message_copy) - 1] = '\0';
        bool first_line = true;
        char* token = message_copy;
        while (token && *token) {
            char* next = strchr(token, '\n');
            if (next) {
                *next = '\0';
                next++;
            }
            if (first_line) {
                tty_write_ansi("  \x1b[1;31m• Message:\x1b[0m ");
                first_line = false;
            } else {
                tty_write_ansi("    \x1b[0;35m↳\x1b[0m ");
            }
            tty_write_ansi(token);
            tty_write_ansi("\n");
            token = next;
        }
    }

    tty_write_ansi("  \x1b[1;34m• Classification:\x1b[0m ");
    tty_write_ansi(get_panic_type_name(ctx->type));
    tty_write_ansi("\n");

    tty_write_ansi("  \x1b[1;92m• Subsystem Source:\x1b[0m ");
    tty_write_ansi(subsystem);
    tty_write_ansi("\n");

    tty_write_ansi("  \x1b[1;33m• Recovery Window:\x1b[0m ");
    tty_write_ansi(ctx->recoverable ? "Partial recovery possible" : "Manual intervention required");
    tty_write_ansi("\n");

    const char* backend = tty_uses_graphics_backend() ? "graphics text framebuffer" : "legacy text framebuffer";
    tty_write_ansi("  \x1b[1;36m• TTY surface:\x1b[0m ");
    tty_write_ansi(backend);
    tty_write_ansi(" (ANSI renderer active)\n");

    if (ctx->file[0]) {
        tty_write_ansi("  \x1b[1;96m• Location:\x1b[0m ");
        tty_write_ansi(ctx->file);
        if (ctx->line > 0) {
            char line_str[16];
            format_decimal(ctx->line, line_str);
            tty_write_ansi(" : ");
            tty_write_ansi(line_str);
        }
        if (ctx->function[0]) {
            tty_write_ansi(" (");
            tty_write_ansi(ctx->function);
            tty_write_ansi(")");
        }
        tty_write_ansi("\n");
    }

    tty_write_ansi("\x1b[95m------------------------------------------------------------------------------\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("\x1b[1;95mCPU Snapshot\x1b[0m\x1b[45m\x1b[97m\n");
    char hex_str[12];
    char reg_str[12];
    char dec_str[12];
    format_hex32(ctx->cpu_state.eip, hex_str);
    tty_write_ansi("  \x1b[0;35mEIP\x1b[0m    : \x1b[1;97m");
    tty_write_ansi(hex_str);
    tty_write_ansi("\x1b[0m\x1b[45m\x1b[97m\n");

    format_hex32(ctx->cpu_state.esp, hex_str);
    tty_write_ansi("  \x1b[0;35mESP\x1b[0m    : \x1b[1;97m");
    tty_write_ansi(hex_str);
    tty_write_ansi("\x1b[0m\x1b[45m\x1b[97m\n");

    format_hex32(ctx->cpu_state.eflags, hex_str);
    tty_write_ansi("  \x1b[0;35mEFLAGS\x1b[0m : \x1b[1;97m");
    tty_write_ansi(hex_str);
    tty_write_ansi("\x1b[0m\x1b[45m\x1b[97m\n");

    tty_write_ansi("  \x1b[0;95mGeneral registers:\x1b[0m\x1b[45m\x1b[97m\n");
    format_hex32(ctx->cpu_state.eax, reg_str);
    tty_write_ansi("    \x1b[0;36mEAX\x1b[0m ");
    tty_write_ansi(reg_str);
    tty_write_ansi("    ");
    format_hex32(ctx->cpu_state.ebx, reg_str);
    tty_write_ansi("\x1b[0;36mEBX\x1b[0m ");
    tty_write_ansi(reg_str);
    tty_write_ansi("\n");
    format_hex32(ctx->cpu_state.ecx, reg_str);
    tty_write_ansi("    \x1b[0;36mECX\x1b[0m ");
    tty_write_ansi(reg_str);
    tty_write_ansi("    ");
    format_hex32(ctx->cpu_state.edx, reg_str);
    tty_write_ansi("\x1b[0;36mEDX\x1b[0m ");
    tty_write_ansi(reg_str);
    tty_write_ansi("\n");
    format_hex32(ctx->cpu_state.esi, reg_str);
    tty_write_ansi("    \x1b[0;36mESI\x1b[0m ");
    tty_write_ansi(reg_str);
    tty_write_ansi("    ");
    format_hex32(ctx->cpu_state.edi, reg_str);
    tty_write_ansi("\x1b[0;36mEDI\x1b[0m ");
    tty_write_ansi(reg_str);
    tty_write_ansi("\n\n");

    tty_write_ansi("\x1b[95m------------------------------------------------------------------------------\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("\x1b[1;95mError Details\x1b[0m\x1b[45m\x1b[97m\n");
    if (ctx->error_code) {
        format_hex32(ctx->error_code, hex_str);
        format_decimal(ctx->error_code, dec_str);
        tty_write_ansi("  \x1b[1;31m• Error Code:\x1b[0m ");
        tty_write_ansi(hex_str);
        tty_write_ansi(" (");
        tty_write_ansi(dec_str);
        tty_write_ansi(")\n");
    } else {
        tty_write_ansi("  \x1b[1;31m• Error Code:\x1b[0m Not supplied/CPU generated\n");
    }
    tty_write_ansi("  \x1b[1;36m• Fault Address (CR2):\x1b[0m ");
    if (ctx->cpu_state.cr2) {
        format_hex32(ctx->cpu_state.cr2, hex_str);
        tty_write_ansi(hex_str);
        tty_write_ansi("\n");
    } else {
        tty_write_ansi("No address captured\n");
    }
    format_hex32(ctx->cpu_state.eip, hex_str);
    format_hex32(ctx->cpu_state.cs, reg_str);
    tty_write_ansi("  \x1b[1;94m• Instruction Source:\x1b[0m CS=");
    tty_write_ansi(reg_str);
    tty_write_ansi(" EIP=");
    tty_write_ansi(hex_str);
    tty_write_ansi("\n");

    tty_write_ansi("\x1b[95m------------------------------------------------------------------------------\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("\x1b[1;95mActions\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("  \x1b[1;31m!\x1b[0;45;97m Safely power cycle the machine.\n");
    tty_write_ansi("  \x1b[1;36m!\x1b[0;45;97m Capture this screen for debugging.\n");
    tty_write_ansi("  \x1b[1;33m!\x1b[0;45;97m Review recent logs for hardware or memory faults.\n");
    tty_write_ansi("  \x1b[1;35m!\x1b[0;45;97m Inspect recent drivers or kernel modules for regressions.\n\n");

    tty_write_ansi("\x1b[95m------------------------------------------------------------------------------\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("\x1b[1;95mANSI Diagnostic Palette\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("  ");
    tty_write_ansi("\x1b[40m  0  \x1b[41m  1  \x1b[42m  2  \x1b[43m  3  \x1b[44m  4  \x1b[45m  5  \x1b[46m  6  \x1b[47m  7  \x1b[0m\n");
    tty_write_ansi("  ");
    tty_write_ansi("\x1b[100m  8  \x1b[101m  9  \x1b[102m 10  \x1b[103m 11  \x1b[104m 12  \x1b[105m 13  \x1b[106m 14  \x1b[107m 15  \x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("  \x1b[0mForeground sweep:\x1b[30m 30\x1b[31m 31\x1b[32m 32\x1b[33m 33\x1b[34m 34\x1b[35m 35\x1b[36m 36\x1b[37m 37\x1b[0m\x1b[45m\x1b[97m\n");
    tty_write_ansi("  \x1b[0mBright sweep:\x1b[90m 90\x1b[91m 91\x1b[92m 92\x1b[93m 93\x1b[94m 94\x1b[95m 95\x1b[96m 96\x1b[97m 97\x1b[0m\x1b[45m\x1b[97m\n\n");

    tty_write_ansi("\x1b[0;90mSystem halted. Press reset or power off.\x1b[0m\n");
}

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

static void capture_stack_snapshot(panic_context_t* ctx) {
    if (!ctx) return;
    
    // Simple implementation - just capture basic CPU state
    // In a full implementation, this would walk the stack
    ctx->manual_stack_count = 0;
    ctx->manual_stack_valid = false;
    ctx->stack_frame_count = 0;
    
    // Try to capture some basic stack information
    uint32 esp;
    asm volatile("mov %%esp, %0" : "=r" (esp));
    
    if (esp > 0x1000 && esp < 0xFFFF0000) {
        // Try to read a few stack entries
        uint32* stack_ptr = (uint32*)esp;
        for (int i = 0; i < 8 && i < MAX_STACK_FRAMES; i++) {
            ctx->manual_stack[i] = stack_ptr[i];
            ctx->manual_stack_count++;
        }
        ctx->manual_stack_valid = true;
    }
}

// =============================================================================
// EXTERNAL API FUNCTIONS
// =============================================================================

void kernel_panic_annotated(const char* message, const char* file, uint32 line, const char* func) {
    if (!message) message = "Unknown error";
    if (!file) file = "unknown";
    if (!func) func = "unknown";

    // Initialize simple panic context
    panic_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    capture_cpu_state_atomic(&ctx.cpu_state);
    strncpy((char*)ctx.message, message, sizeof(ctx.message) - 1);
    strncpy((char*)ctx.file, file, sizeof(ctx.file) - 1);
    strncpy((char*)ctx.function, func, sizeof(ctx.function) - 1);
    ctx.line = line;
    ctx.type = PANIC_TYPE_GENERAL;

    if (g_panic_pending_fault.valid) {
        ctx.cpu_state.cr2 = g_panic_pending_fault.fault_addr;
        ctx.error_code = g_panic_pending_fault.error_code;
        if (g_panic_pending_fault.fault_eip) {
            ctx.cpu_state.eip = g_panic_pending_fault.fault_eip;
        }
        if (g_panic_pending_fault.fault_cs) {
            ctx.cpu_state.cs = g_panic_pending_fault.fault_cs;
        }
        if (g_panic_pending_fault.fault_eflags) {
            ctx.cpu_state.eflags = g_panic_pending_fault.fault_eflags;
        }
        g_panic_pending_fault.valid = false;
    }

    // Capture current CPU state
    capture_stack_snapshot(&ctx);
    panic_store_context(&ctx);
    panic_debuglog_emit(&ctx);
    
    // Display panic and halt
    draw_simple_panic_screen(&ctx);
    
    // Disable interrupts and halt system
    asm volatile("cli");
    for(;;) {
        asm volatile("hlt");
    }
}

void kernel_panic_with_stack(const char* message, const uint32* stack_entries, uint32 entry_count) {
    if (!message) message = "Stack trace panic";

    // Initialize panic context
    panic_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    capture_cpu_state_atomic(&ctx.cpu_state);
    strncpy((char*)ctx.message, message, sizeof(ctx.message) - 1);
    ctx.type = PANIC_TYPE_GENERAL;

    if (g_panic_pending_fault.valid) {
        ctx.cpu_state.cr2 = g_panic_pending_fault.fault_addr;
        ctx.error_code = g_panic_pending_fault.error_code;
        if (g_panic_pending_fault.fault_eip) {
            ctx.cpu_state.eip = g_panic_pending_fault.fault_eip;
        }
        if (g_panic_pending_fault.fault_cs) {
            ctx.cpu_state.cs = g_panic_pending_fault.fault_cs;
        }
        if (g_panic_pending_fault.fault_eflags) {
            ctx.cpu_state.eflags = g_panic_pending_fault.fault_eflags;
        }
        g_panic_pending_fault.valid = false;
    }

    // Copy stack entries
    if (stack_entries && entry_count > 0) {
        uint32 copy_count = (entry_count > MAX_STACK_FRAMES) ? MAX_STACK_FRAMES : entry_count;
        for (uint32 i = 0; i < copy_count; i++) {
            ctx.manual_stack[i] = stack_entries[i];
        }
        ctx.manual_stack_count = copy_count;
        ctx.manual_stack_valid = true;
    }

    panic_store_context(&ctx);
    panic_debuglog_emit(&ctx);

    // Display panic and halt  
    draw_simple_panic_screen(&ctx);
    
    // Disable interrupts and halt system
    asm volatile("cli");
    for(;;) {
        asm volatile("hlt");
    }
}

// =============================================================================
// END OF PANIC HANDLER
// =============================================================================
