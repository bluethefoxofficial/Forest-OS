/*
 * Forest OS PanicUI - Framebuffer-Based Kernel Panic Screen
 *
 * A clean, direct framebuffer panic display using graphics_draw_text
 * for text rendering. Does not use TTY to avoid buffer conflicts.
 */

#include "include/panicui.h"
#include "include/tty.h"
#include "include/system.h"
#include "include/memory.h"
#include "include/string.h"
#include "include/debuglog.h"
#include "include/util.h"
#include "include/hardware.h"
#include "include/ps2_mouse.h"
#include "include/keyboard_layout.h"
#include "include/ps2_keyboard.h"
#include "include/libc/stdio.h"
#include "include/graphics/graphics_manager.h"
#include "include/graphics/font_renderer.h"

// =============================================================================
// PANIC UI CONFIGURATION
// =============================================================================

#define PANIC_VERSION "2.0"

// Graphics colors for framebuffer drawing
static const graphics_color_t PANIC_COLOR_BG       = {0, 0, 120, 255};      // Blue background
static const graphics_color_t PANIC_COLOR_HEADER   = {0, 0, 170, 255};      // Lighter blue header
static const graphics_color_t PANIC_COLOR_WHITE    = {255, 255, 255, 255};  // White
static const graphics_color_t PANIC_COLOR_RED      = {255, 80, 80, 255};    // Red for errors
static const graphics_color_t PANIC_COLOR_YELLOW   = {255, 255, 0, 255};    // Yellow
static const graphics_color_t PANIC_COLOR_CYAN     = {0, 255, 255, 255};    // Cyan
static const graphics_color_t PANIC_COLOR_GRAY     = {180, 180, 180, 255};  // Light gray

// Character dimensions (8x8 font assumed)
#define CHAR_WIDTH  8
#define CHAR_HEIGHT 8

// =============================================================================
// PANIC UI STATE
// =============================================================================

typedef enum {
    PANIC_PAGE_OVERVIEW = 0,
    PANIC_PAGE_REGISTERS,
    PANIC_PAGE_MEMORY,
    PANIC_PAGE_STACK,
    PANIC_PAGE_SYSTEM,
    PANIC_PAGE_MAX
} panic_page_t;

static struct {
    bool initialized;
    bool active;

    // Screen dimensions
    uint32_t screen_width;
    uint32_t screen_height;

    // Current page
    panic_page_t current_page;
    int32_t scroll_offset;

    // Panic information
    char message[512];
    char file[256];
    uint32_t line;
    uint32_t fault_addr;
    uint32_t error_code;

    // CPU state
    struct {
        uint32_t eax, ebx, ecx, edx;
        uint32_t esi, edi, ebp, esp;
        uint32_t eip, eflags;
        uint32_t cr0, cr2, cr3, cr4;
        uint16_t cs, ds, es, fs, gs, ss;
    } regs;

    // Stack trace
    uint32_t stack_trace[16];
    uint32_t stack_count;

    // Font
    font_t* font;

    // Mouse
    int32_t mouse_x;
    int32_t mouse_y;
    bool mouse_left;
    bool mouse_right;
    bool mouse_enabled;
} g_panic = {0};

// Page titles
static const char* g_page_titles[PANIC_PAGE_MAX] = {
    "OVERVIEW",
    "CPU REGISTERS",
    "MEMORY",
    "STACK TRACE",
    "SYSTEM INFO"
};

// =============================================================================
// INPUT BUFFER (EVENT + DIRECT POLL)
// =============================================================================

#define PANIC_KEY_QUEUE_CAP 32
static volatile uint8_t g_key_head = 0;
static volatile uint8_t g_key_tail = 0;
static key_code_t g_key_queue[PANIC_KEY_QUEUE_CAP];
static bool g_seen_extended = false;

static void panic_enqueue_key(key_code_t code) {
    uint8_t next = (uint8_t)((g_key_head + 1) % PANIC_KEY_QUEUE_CAP);
    g_key_queue[g_key_head] = code;
    g_key_head = next;
    if (g_key_head == g_key_tail) {
        g_key_tail = (uint8_t)((g_key_tail + 1) % PANIC_KEY_QUEUE_CAP);
    }
}

static bool panic_dequeue_key(key_code_t* code) {
    if (!code || g_key_head == g_key_tail) {
        return false;
    }
    *code = g_key_queue[g_key_tail];
    g_key_tail = (uint8_t)((g_key_tail + 1) % PANIC_KEY_QUEUE_CAP);
    return true;
}

static void panicui_keyboard_callback(const keyboard_event_t* event) {
    if (!event || event->state != KEY_STATE_PRESSED) {
        return;
    }
    panic_enqueue_key(event->key_code);
}

// Fallback: poll controller directly (in case IRQ path is dead)
static bool panic_poll_direct_key(key_code_t* out_code) {
    uint8_t status = inportb(0x64);

    // If AUX (mouse) data is pending, flush it via the mouse poller and skip this turn
    if (status & 0x20) {
        ps2_mouse_poll();
        return false;
    }

    if ((status & 0x01) == 0) {
        return false;  // No data
    }

    uint8_t scancode = inportb(0x60);
    if (scancode == 0xE0) {
        g_seen_extended = true;
        return false;
    }
    if (scancode == 0xE1) {
        g_seen_extended = false;
        return false;
    }

    bool release = (scancode & 0x80U) != 0;
    scancode &= 0x7F;

    if (release) {
        g_seen_extended = false;
        return false;
    }

    key_code_t code = keyboard_scancode_set1_lookup(scancode, g_seen_extended);
    g_seen_extended = false;
    if (code == KEY_UNKNOWN) {
        return false;
    }

    if (out_code) {
        *out_code = code;
    }
    return true;
}

// =============================================================================
// DIRECT GRAPHICS DRAWING HELPERS
// =============================================================================

static void panic_fill_rect(int32_t x, int32_t y, uint32_t w, uint32_t h, graphics_color_t color) {
    graphics_rect_t rect = {x, y, w, h};
    graphics_draw_rect(&rect, color, true);
}

// Simple crosshair cursor for panic UI
static void panic_draw_cursor(void) {
    if (!g_panic.mouse_enabled) {
        return;
    }

    int32_t x = g_panic.mouse_x;
    int32_t y = g_panic.mouse_y;

    // Horizontal line
    graphics_draw_line(x - 6, y, x + 6, y, PANIC_COLOR_WHITE);
    // Vertical line
    graphics_draw_line(x, y - 6, x, y + 6, PANIC_COLOR_WHITE);
    // Center dot
    graphics_draw_pixel(x, y, PANIC_COLOR_RED);
}

// Draw text at pixel position
static void panic_draw_text(int32_t x, int32_t y, const char* text, graphics_color_t color) {
    graphics_draw_text(x, y, text, g_panic.font, color);
}

// =============================================================================
// PAGE RENDERING
// =============================================================================

static void panic_render_header(void) {
    // Header background
    panic_fill_rect(0, 0, g_panic.screen_width, 48, PANIC_COLOR_HEADER);
    graphics_draw_line(0, 47, g_panic.screen_width - 1, 47, PANIC_COLOR_WHITE);

    // Title
    panic_draw_text(16, 8, "FOREST OS - KERNEL PANIC", PANIC_COLOR_WHITE);

    // Page info
    char page_info[64];
    sprintf(page_info, "Page [%d/%d]: %s",
            g_panic.current_page + 1, PANIC_PAGE_MAX,
            g_page_titles[g_panic.current_page]);
    panic_draw_text(16, 28, page_info, PANIC_COLOR_CYAN);
}

static void panic_render_footer(void) {
    uint32_t footer_y = g_panic.screen_height - 32;

    // Footer background
    panic_fill_rect(0, footer_y, g_panic.screen_width, 32, PANIC_COLOR_HEADER);
    graphics_draw_line(0, footer_y, g_panic.screen_width - 1, footer_y, PANIC_COLOR_WHITE);

    // Navigation help
    panic_draw_text(16, footer_y + 12,
        "[LEFT/RIGHT] Page  [UP/DOWN] Scroll  [R] Reboot  [H] Halt", PANIC_COLOR_YELLOW);
}

static void panic_render_overview(void) {
    int32_t y = 64;
    int32_t x = 16;

    // Error title
    panic_draw_text(x, y, "*** STOP: KERNEL PANIC ***", PANIC_COLOR_RED);
    y += 24;

    // Error message
    panic_draw_text(x, y, g_panic.message, PANIC_COLOR_WHITE);
    y += 16;

    // Location
    char loc[128];
    sprintf(loc, "Location: %s:%u", g_panic.file, g_panic.line);
    panic_draw_text(x, y, loc, PANIC_COLOR_CYAN);
    y += 16;

    // Fault info
    char fault[128];
    sprintf(fault, "Fault Address: 0x%08X  Error Code: 0x%08X",
            g_panic.fault_addr, g_panic.error_code);
    panic_draw_text(x, y, fault, PANIC_COLOR_YELLOW);
    y += 32;

    // Quick CPU info
    panic_draw_text(x, y, "Quick Info:", PANIC_COLOR_CYAN);
    y += 16;

    char reg_info[128];
    sprintf(reg_info, "EIP: 0x%08X  ESP: 0x%08X  EBP: 0x%08X",
            g_panic.regs.eip, g_panic.regs.esp, g_panic.regs.ebp);
    panic_draw_text(x + 16, y, reg_info, PANIC_COLOR_WHITE);
    y += 12;

    sprintf(reg_info, "CR2: 0x%08X  CR3: 0x%08X  EFLAGS: 0x%08X",
            g_panic.regs.cr2, g_panic.regs.cr3, g_panic.regs.eflags);
    panic_draw_text(x + 16, y, reg_info, PANIC_COLOR_WHITE);
    y += 32;

    // Recommendations
    panic_draw_text(x, y, "Recommendations:", PANIC_COLOR_CYAN);
    y += 16;
    panic_draw_text(x + 16, y, "1. Check recent code changes for bugs", PANIC_COLOR_GRAY);
    y += 12;
    panic_draw_text(x + 16, y, "2. Review the stack trace (page 4)", PANIC_COLOR_GRAY);
    y += 12;
    panic_draw_text(x + 16, y, "3. Verify memory operations are valid", PANIC_COLOR_GRAY);
    y += 12;
    panic_draw_text(x + 16, y, "4. Check for null pointer dereferences", PANIC_COLOR_GRAY);
}

static void panic_render_registers(void) {
    int32_t y = 64;
    int32_t x = 16;
    char buf[64];

    panic_draw_text(x, y, "CPU Register State:", PANIC_COLOR_CYAN);
    y += 24;

    // General purpose registers
    sprintf(buf, "EAX: 0x%08X    EBX: 0x%08X", g_panic.regs.eax, g_panic.regs.ebx);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 12;

    sprintf(buf, "ECX: 0x%08X    EDX: 0x%08X", g_panic.regs.ecx, g_panic.regs.edx);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 12;

    sprintf(buf, "ESI: 0x%08X    EDI: 0x%08X", g_panic.regs.esi, g_panic.regs.edi);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 12;

    sprintf(buf, "EBP: 0x%08X    ESP: 0x%08X", g_panic.regs.ebp, g_panic.regs.esp);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 24;

    // Instruction pointer and flags
    panic_draw_text(x, y, "Instruction Pointer & Flags:", PANIC_COLOR_CYAN);
    y += 16;

    sprintf(buf, "EIP: 0x%08X", g_panic.regs.eip);
    panic_draw_text(x, y, buf, PANIC_COLOR_RED);
    y += 12;

    sprintf(buf, "EFLAGS: 0x%08X", g_panic.regs.eflags);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 12;

    // Decode flags
    uint32_t flags = g_panic.regs.eflags;
    sprintf(buf, "CF=%d PF=%d ZF=%d SF=%d OF=%d IF=%d",
            (flags & 0x1) ? 1 : 0,
            (flags & 0x4) ? 1 : 0,
            (flags & 0x40) ? 1 : 0,
            (flags & 0x80) ? 1 : 0,
            (flags & 0x800) ? 1 : 0,
            (flags & 0x200) ? 1 : 0);
    panic_draw_text(x + 16, y, buf, PANIC_COLOR_GRAY);
    y += 24;

    // Control registers
    panic_draw_text(x, y, "Control Registers:", PANIC_COLOR_CYAN);
    y += 16;

    sprintf(buf, "CR0: 0x%08X    CR2: 0x%08X", g_panic.regs.cr0, g_panic.regs.cr2);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 12;

    sprintf(buf, "CR3: 0x%08X    CR4: 0x%08X", g_panic.regs.cr3, g_panic.regs.cr4);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 24;

    // Segment registers
    panic_draw_text(x, y, "Segment Registers:", PANIC_COLOR_CYAN);
    y += 16;

    sprintf(buf, "CS: 0x%04X  DS: 0x%04X  ES: 0x%04X  FS: 0x%04X  GS: 0x%04X  SS: 0x%04X",
            g_panic.regs.cs, g_panic.regs.ds, g_panic.regs.es,
            g_panic.regs.fs, g_panic.regs.gs, g_panic.regs.ss);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
}

static void panic_render_memory(void) {
    int32_t y = 64;
    int32_t x = 16;

    panic_draw_text(x, y, "Memory Around Fault Address:", PANIC_COLOR_CYAN);
    y += 16;

    if (g_panic.fault_addr == 0) {
        panic_draw_text(x, y, "No fault address available.", PANIC_COLOR_YELLOW);
        return;
    }

    char buf[128];
    sprintf(buf, "Fault Address: 0x%08X", g_panic.fault_addr);
    panic_draw_text(x, y, buf, PANIC_COLOR_RED);
    y += 24;

    // Header
    panic_draw_text(x, y, "Address     00 01 02 03 04 05 06 07  ASCII", PANIC_COLOR_YELLOW);
    y += 12;

    // Show memory around fault address
    uint32_t base = (g_panic.fault_addr & ~0x7) - 0x20;

    for (int row = 0; row < 8 && y < (int32_t)(g_panic.screen_height - 64); row++) {
        uint32_t addr = base + row * 8;
        char hex_part[64];
        char ascii_part[16];

        sprintf(hex_part, "0x%08X  ", addr);

        for (int i = 0; i < 8; i++) {
            uint32_t byte_addr = addr + i;
            // Only read from kernel memory space safely
            if (byte_addr >= 0xC0000000) {
                uint8_t byte = *(volatile uint8_t*)byte_addr;
                char byte_str[4];
                sprintf(byte_str, "%02X ", byte);
                strcat(hex_part, byte_str);
                ascii_part[i] = (byte >= 32 && byte < 127) ? byte : '.';
            } else {
                strcat(hex_part, "?? ");
                ascii_part[i] = '?';
            }
        }
        ascii_part[8] = '\0';
        strcat(hex_part, " ");
        strcat(hex_part, ascii_part);

        // Highlight line with fault address
        graphics_color_t color = PANIC_COLOR_WHITE;
        if (addr <= g_panic.fault_addr && g_panic.fault_addr < addr + 8) {
            color = PANIC_COLOR_RED;
        }

        panic_draw_text(x, y, hex_part, color);
        y += 10;
    }
}

static void panic_render_stack(void) {
    int32_t y = 64;
    int32_t x = 16;

    panic_draw_text(x, y, "Stack Trace:", PANIC_COLOR_CYAN);
    y += 24;

    if (g_panic.stack_count == 0) {
        panic_draw_text(x, y, "No stack trace available.", PANIC_COLOR_YELLOW);
        return;
    }

    panic_draw_text(x, y, "#   Address      Description", PANIC_COLOR_YELLOW);
    y += 12;

    for (uint32_t i = 0; i < g_panic.stack_count && y < (int32_t)(g_panic.screen_height - 64); i++) {
        char buf[64];
        graphics_color_t color = (i == 0) ? PANIC_COLOR_RED : PANIC_COLOR_WHITE;

        if (i == 0) {
            sprintf(buf, "%2u  0x%08X  <fault location>", i, g_panic.stack_trace[i]);
        } else {
            sprintf(buf, "%2u  0x%08X  <caller>", i, g_panic.stack_trace[i]);
        }

        panic_draw_text(x, y, buf, color);
        y += 10;
    }

    y += 16;
    panic_draw_text(x, y, "Note: Symbol names require symbol table support", PANIC_COLOR_GRAY);
}

static void panic_render_system(void) {
    int32_t y = 64;
    int32_t x = 16;
    char buf[128];

    panic_draw_text(x, y, "System Information:", PANIC_COLOR_CYAN);
    y += 24;

    sprintf(buf, "Forest OS - Panic Screen v%s", PANIC_VERSION);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 16;

    sprintf(buf, "Display: %ux%u pixels", g_panic.screen_width, g_panic.screen_height);
    panic_draw_text(x, y, buf, PANIC_COLOR_WHITE);
    y += 24;

    panic_draw_text(x, y, "Memory Layout:", PANIC_COLOR_CYAN);
    y += 16;
    panic_draw_text(x + 16, y, "Kernel:  0xC0100000 - 0xC0FFFFFF", PANIC_COLOR_GRAY);
    y += 10;
    panic_draw_text(x + 16, y, "Heap:    0xC1000000 - 0xCFFFFFFF", PANIC_COLOR_GRAY);
    y += 10;
    panic_draw_text(x + 16, y, "Stack:   0xCFF00000 - 0xCFFFFFFF", PANIC_COLOR_GRAY);
    y += 24;

    panic_draw_text(x, y, "CPU Information:", PANIC_COLOR_CYAN);
    y += 16;
    panic_draw_text(x + 16, y, "Architecture: x86 (32-bit protected mode)", PANIC_COLOR_GRAY);
    y += 10;

    uint32_t cr0 = g_panic.regs.cr0;
    sprintf(buf, "CR0: PE=%d PG=%d", (cr0 & 0x1) ? 1 : 0, (cr0 & 0x80000000) ? 1 : 0);
    panic_draw_text(x + 16, y, buf, PANIC_COLOR_GRAY);
    y += 24;

    panic_draw_text(x, y, "Recovery Options:", PANIC_COLOR_CYAN);
    y += 16;
    panic_draw_text(x + 16, y, "Press 'R' to reboot the system", PANIC_COLOR_YELLOW);
    y += 10;
    panic_draw_text(x + 16, y, "Press 'H' to halt the CPU", PANIC_COLOR_YELLOW);
}

// =============================================================================
// MAIN RENDERING
// =============================================================================

static void panic_render_frame(void) {
    // Clear screen with blue background
    graphics_clear_screen(PANIC_COLOR_BG);

    // Render header and footer
    panic_render_header();
    panic_render_footer();

    // Render current page
    switch (g_panic.current_page) {
        case PANIC_PAGE_OVERVIEW:
            panic_render_overview();
            break;
        case PANIC_PAGE_REGISTERS:
            panic_render_registers();
            break;
        case PANIC_PAGE_MEMORY:
            panic_render_memory();
            break;
        case PANIC_PAGE_STACK:
            panic_render_stack();
            break;
        case PANIC_PAGE_SYSTEM:
            panic_render_system();
            break;
        default:
            break;
    }

    // Draw mouse cursor last so it sits on top
    panic_draw_cursor();

    // Swap buffers
    graphics_swap_buffers();
}

// =============================================================================
// INPUT HANDLING
// =============================================================================

static void panic_mouse_callback(const ps2_mouse_event_t* event) {
    if (!event) {
        return;
    }

    int32_t x = event->x;
    int32_t y = event->y;

    // Clamp to screen bounds
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int32_t)g_panic.screen_width) x = (int32_t)g_panic.screen_width - 1;
    if (y >= (int32_t)g_panic.screen_height) y = (int32_t)g_panic.screen_height - 1;

    g_panic.mouse_x = x;
    g_panic.mouse_y = y;
    g_panic.mouse_left = event->left_button;
    g_panic.mouse_right = event->right_button;
    g_panic.mouse_enabled = true;
}

static void panic_handle_keycode(key_code_t k) {
    // Page selection with number row
    if (k >= KEY_1 && k <= KEY_5) {
        g_panic.current_page = (panic_page_t)(k - KEY_1);
        g_panic.scroll_offset = 0;
        return;
    }

    // Map a few keypad numbers to pages (avoids clashes with keypad arrows)
    if (k == KEY_KEYPAD_1) {
        g_panic.current_page = PANIC_PAGE_OVERVIEW;
        g_panic.scroll_offset = 0;
        return;
    }
    if (k == KEY_KEYPAD_3) {
        g_panic.current_page = PANIC_PAGE_MEMORY;
        g_panic.scroll_offset = 0;
        return;
    }
    if (k == KEY_KEYPAD_5) {
        g_panic.current_page = PANIC_PAGE_SYSTEM;
        g_panic.scroll_offset = 0;
        return;
    }

    switch (k) {
        case KEY_LEFT:
        case KEY_KEYPAD_4:
            if (g_panic.current_page > 0) {
                g_panic.current_page--;
                g_panic.scroll_offset = 0;
            }
            break;

        case KEY_RIGHT:
        case KEY_KEYPAD_6:
            if (g_panic.current_page < PANIC_PAGE_MAX - 1) {
                g_panic.current_page++;
                g_panic.scroll_offset = 0;
            }
            break;

        case KEY_UP:
        case KEY_KEYPAD_8:
            g_panic.scroll_offset--;
            if (g_panic.scroll_offset < -64) g_panic.scroll_offset = -64;
            break;

        case KEY_DOWN:
        case KEY_KEYPAD_2:
            g_panic.scroll_offset++;
            if (g_panic.scroll_offset > 64) g_panic.scroll_offset = 64;
            break;

        case KEY_R: {
            uint8_t status = 0x02;
            while (status & 0x02) {
                status = inportb(0x64);
            }
            outportb(0x64, 0xFE);
            break;
        }

        case KEY_H:
        case KEY_ESC:
            g_panic.active = false;
            break;

        default:
            break;
    }
}

static void panic_handle_input(void) {
    key_code_t code;

    // Drain queued events from the PS/2 driver
    while (panic_dequeue_key(&code)) {
        panic_handle_keycode(code);
    }

    // Also poll the controller directly in case IRQs are off
    for (int i = 0; i < 4; i++) {
        if (!panic_poll_direct_key(&code)) {
            break;
        }
        panic_handle_keycode(code);
    }
}

// =============================================================================
// COLLECT SYSTEM STATE
// =============================================================================

static void panic_collect_registers(void) {
#ifndef __x86_64__
    __asm__ volatile("mov %%cr0, %0" : "=r"(g_panic.regs.cr0));
    __asm__ volatile("mov %%cr2, %0" : "=r"(g_panic.regs.cr2));
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_panic.regs.cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(g_panic.regs.cr4));

    __asm__ volatile("mov %%cs, %0" : "=r"(g_panic.regs.cs));
    __asm__ volatile("mov %%ds, %0" : "=r"(g_panic.regs.ds));
    __asm__ volatile("mov %%es, %0" : "=r"(g_panic.regs.es));
    __asm__ volatile("mov %%fs, %0" : "=r"(g_panic.regs.fs));
    __asm__ volatile("mov %%gs, %0" : "=r"(g_panic.regs.gs));
    __asm__ volatile("mov %%ss, %0" : "=r"(g_panic.regs.ss));

    __asm__ volatile("mov %%esp, %0" : "=r"(g_panic.regs.esp));
    __asm__ volatile("mov %%ebp, %0" : "=r"(g_panic.regs.ebp));

    __asm__ volatile("pushf; pop %0" : "=r"(g_panic.regs.eflags));
#endif
}

static void panic_collect_stack_trace(void) {
    uint32_t* ebp;
    __asm__ volatile("mov %%ebp, %0" : "=r"(ebp));

    g_panic.stack_count = 0;

    if (g_panic.regs.eip != 0) {
        g_panic.stack_trace[g_panic.stack_count++] = g_panic.regs.eip;
    }

    for (int i = 0; i < 15 && ebp != NULL && g_panic.stack_count < 16; i++) {
        if ((uint32_t)ebp < 0xC0000000 || (uint32_t)ebp > 0xFFFFFFFF) {
            break;
        }

        uint32_t ret_addr = *(ebp + 1);
        if (ret_addr < 0xC0000000 || ret_addr > 0xFFFFFFFF) {
            break;
        }

        g_panic.stack_trace[g_panic.stack_count++] = ret_addr;
        ebp = (uint32_t*)*ebp;
    }
}

// =============================================================================
// PUBLIC API
// =============================================================================

graphics_result_t panicui_init(void) {
    debuglog(DEBUG_INFO, "[PanicUI] Initializing framebuffer panic screen...\n");

    memset(&g_panic, 0, sizeof(g_panic));

    if (!graphics_is_initialized()) {
        debuglog(DEBUG_ERROR, "[PanicUI] Graphics not initialized\n");
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }

    video_mode_t mode;
    if (graphics_get_current_mode(&mode) != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "[PanicUI] Failed to get video mode\n");
        return GRAPHICS_ERROR_GENERIC;
    }

    g_panic.screen_width = mode.width;
    g_panic.screen_height = mode.height;

    g_panic.mouse_x = (int32_t)(g_panic.screen_width / 2);
    g_panic.mouse_y = (int32_t)(g_panic.screen_height / 2);
    g_panic.mouse_left = false;
    g_panic.mouse_right = false;
    g_panic.mouse_enabled = false;

    // Get system font
    if (!font_renderer_is_initialized()) {
        font_renderer_init();
    }

    if (font_get_system_font(&g_panic.font) != GRAPHICS_SUCCESS || !g_panic.font) {
        g_panic.font = NULL;
        debuglog(DEBUG_WARN, "[PanicUI] No system font available, using default\n");
    }

    // Disable double buffering for panic - write directly to screen
    graphics_enable_double_buffering(false);

    g_panic.initialized = true;
    g_panic.current_page = PANIC_PAGE_OVERVIEW;

    debuglog(DEBUG_INFO, "[PanicUI] Initialized (%ux%u)\n",
        g_panic.screen_width, g_panic.screen_height);

    return GRAPHICS_SUCCESS;
}

void panicui_shutdown(void) {
    memset(&g_panic, 0, sizeof(g_panic));
}

bool panicui_is_graphics_available(void) {
    return g_panic.initialized && graphics_is_initialized();
}

void panicui_show_panic(const char* message, const char* file, uint32_t line,
                       uint32_t fault_addr, uint32_t error_code) {
    if (!g_panic.initialized) {
        if (panicui_init() != GRAPHICS_SUCCESS) {
            return;
        }
    }

    // Store panic info
    if (message) {
        strncpy(g_panic.message, message, sizeof(g_panic.message) - 1);
    } else {
        strcpy(g_panic.message, "Unknown error");
    }

    if (file) {
        strncpy(g_panic.file, file, sizeof(g_panic.file) - 1);
    } else {
        strcpy(g_panic.file, "<unknown>");
    }

    g_panic.line = line;
    g_panic.fault_addr = fault_addr;
    g_panic.error_code = error_code;
    g_panic.regs.eip = fault_addr;

    // Collect state
    panic_collect_registers();
    panic_collect_stack_trace();

    // Hook keyboard events so we can receive input even if the normal handler is active
    g_key_head = g_key_tail = 0;
    ps2_keyboard_register_event_callback(panicui_keyboard_callback);
    ps2_mouse_register_event_callback(panic_mouse_callback);
    g_panic.mouse_enabled = true;

    g_panic.active = true;
    g_panic.current_page = PANIC_PAGE_OVERVIEW;
    g_panic.scroll_offset = 0;

    debuglog(DEBUG_INFO, "[PanicUI] Showing panic: %s\n", message);

    panicui_main_loop();
}

void panicui_main_loop(void) {
    while (g_panic.active) {
        // Keep mouse input flowing even if IRQ delivery is blocked
        ps2_mouse_poll();
        panic_handle_input();
        panic_render_frame();

        // Small delay
        for (volatile int i = 0; i < 100000; i++);
    }

    // Halt
    __asm__ volatile("cli; hlt");
}

void panicui_render_frame(void) {
    panic_render_frame();
}

// =============================================================================
// COMPATIBILITY STUBS
// =============================================================================

panicui_context_t* panicui_get_context(void) { return NULL; }

void panicui_switch_to_panel(panicui_panel_type_t panel) {
    if ((int)panel < (int)PANIC_PAGE_MAX) {
        g_panic.current_page = (panic_page_t)panel;
    }
}

void panicui_handle_mouse_event(const ps2_mouse_event_t* event) { (void)event; }
void panicui_handle_key_event(uint32_t keycode) { (void)keycode; }
void panicui_update_panel_content(panicui_panel_type_t panel) { (void)panel; }
void panicui_scroll_panel(panicui_panel_type_t panel, int32_t dx, int32_t dy) { (void)panel; (void)dx; (void)dy; }
void panicui_draw_window_frame(void) {}
void panicui_draw_titlebar(void) {}
void panicui_draw_tabs(void) {}
void panicui_draw_panel(panicui_panel_type_t panel) { (void)panel; }
void panicui_draw_statusbar(void) {}
void panicui_draw_cursor(void) {}
void panicui_draw_rect_with_border(graphics_rect_t r, graphics_color_t b, graphics_color_t bo, uint32_t bw) { (void)r; (void)b; (void)bo; (void)bw; }
void panicui_draw_text_with_shadow(int32_t x, int32_t y, const char* t, font_t* f, graphics_color_t c) { (void)x; (void)y; (void)t; (void)f; (void)c; }
void panicui_draw_button(graphics_rect_t b, const char* t, bool p, bool h) { (void)b; (void)t; (void)p; (void)h; }
graphics_rect_t panicui_get_text_bounds(const char* t, font_t* f) { (void)t; (void)f; return (graphics_rect_t){0,0,0,0}; }
bool panicui_point_in_rect(int32_t x, int32_t y, graphics_rect_t r) { (void)x; (void)y; (void)r; return false; }
panicui_widget_t* panicui_get_widget_at_point(int32_t x, int32_t y) { (void)x; (void)y; return NULL; }
void panicui_collect_register_info(void) { panic_collect_registers(); }
void panicui_collect_memory_info(uint32_t a) { (void)a; }
void panicui_collect_stack_trace(void) { panic_collect_stack_trace(); }
void panicui_collect_system_info(void) {}
void panicui_generate_recovery_suggestions(void) {}
graphics_color_t panicui_blend_colors(graphics_color_t a, graphics_color_t b, uint8_t al) { (void)b; (void)al; return a; }
graphics_color_t panicui_darken_color(graphics_color_t c, uint8_t a) { (void)a; return c; }
graphics_color_t panicui_lighten_color(graphics_color_t c, uint8_t a) { (void)a; return c; }
void panicui_draw_overview_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_registers_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_memory_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_stack_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_system_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_colors_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_recovery_panel(void* c, graphics_rect_t a) { (void)c; (void)a; }
void panicui_draw_hsv_square(graphics_rect_t b, float h, float* s, float* v) { (void)b; (void)h; (void)s; (void)v; }
void panicui_draw_hue_bar(graphics_rect_t b, float* h) { (void)b; (void)h; }
void panicui_draw_ansi_color_grid(graphics_rect_t b) { (void)b; }
void panicui_draw_color_preview(graphics_rect_t b, graphics_color_t c) { (void)b; (void)c; }
graphics_color_t panicui_hsv_to_rgb(float h, float s, float v) { (void)h; (void)s; (void)v; return (graphics_color_t){0,0,0,255}; }
void panicui_rgb_to_hsv(graphics_color_t r, float* h, float* s, float* v) { (void)r; (void)h; (void)s; (void)v; }
void panicui_generate_ansi_palette(graphics_color_t* p) { (void)p; }
void panicui_draw_glow_effect(graphics_rect_t b, graphics_color_t c, uint32_t r) { (void)b; (void)c; (void)r; }
void panicui_draw_gradient_rect(graphics_rect_t b, graphics_color_t s, graphics_color_t e, bool v) { (void)b; (void)s; (void)e; (void)v; }
void panicui_draw_animated_background(void) {}
void panicui_draw_particle_system(void) {}
void panicui_init_effects(void) {}
void panicui_add_sparkle_effect(int32_t x, int32_t y) { (void)x; (void)y; }
void panicui_draw_sparkles(void) {}
void panicui_draw_scanlines(void) {}
void panicui_draw_vignette(void) {}
void panicui_render_enhanced_frame(void) {}
void panicui_handle_color_panel_click(int32_t x, int32_t y) { (void)x; (void)y; }
void panicui_init_colors_panel(void) {}
void panicui_show_help_overlay(void) {}
void panicui_draw_help_overlay(void) {}
void panicui_handle_input(void) { panic_handle_input(); }
