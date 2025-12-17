#include "include/screen.h"
#include "include/libc/string.h"
#include "include/task.h"
#include "include/interrupt.h"  // Use new safe interrupt functions

static uint32 cursorX = 0, cursorY = 0;
uint16 screen_width = 80, screen_height = 25;
const uint8 sd = 2;
static int color = 0x0F;
static volatile uint8* vidmem = (volatile uint8*)0xb8000;

typedef struct {
    volatile uint32 owner;
    volatile bool locked;
    volatile bool initialized;
    volatile uint32 recursion;
} console_lock_t;

static console_lock_t console_lock = {0, false, false, 0};

// Use safe interrupt functions from interrupt.h
// No longer need local implementations

static volatile bool console_irq_state = false;

static inline void console_lock_init(void) {
    if (!console_lock.initialized) {
        console_lock.owner = 0;
        console_lock.locked = false;
        console_lock.initialized = true;
        console_lock.recursion = 0;
    }
}

static inline uint32 console_lock_owner_id(void) {
    // During early boot, current_task may not be available
    // Use stack pointer as a unique identifier
    uint32 sp;
    __asm__ volatile("mov %%esp, %0" : "=r"(sp));
    return sp & 0xFFFFF000; // Align to page boundary for stability
}

void console_init(void) {
    console_lock_init();
    
    // Initialize screen dimensions and cursor
    cursorX = 0;
    cursorY = 0;
    
    // Set default color (white text on black background)
    color = 0x0F;
    
    // Ensure we have valid video memory pointer
    vidmem = (volatile uint8*)0xb8000;
    
    // Clear screen to establish clean state
    clearScreen();
}

static inline void console_lock_acquire(void) {
    // Ensure lock is initialized
    if (!console_lock.initialized) {
        console_lock_init();
    }
    
    // Save interrupt state and disable interrupts
    bool irq_state = irq_save_and_disable_safe();
    uint32 owner_id = console_lock_owner_id();
    
    // Support recursive locking from the same context
    if (console_lock.locked && console_lock.owner == owner_id) {
        console_lock.recursion++;
        return;
    }
    
    // Spin until we acquire the lock (with timeout for safety)
    uint32 timeout = 0;
    while (__sync_lock_test_and_set(&console_lock.locked, true)) {
        // Brief pause to avoid overwhelming the bus
        __asm__ volatile("pause" ::: "memory");
        
        // Safety timeout to prevent infinite loops during early boot
        if (++timeout > 100000) {
            // Force unlock in case of deadlock during early boot
            __sync_lock_release(&console_lock.locked);
            break;
        }
    }
    
    // Record the owner for debugging
    console_lock.owner = owner_id;
    console_lock.recursion = 1;
    console_irq_state = irq_state;
}

static inline void console_lock_release(void) {
    // Safety check: ensure lock system is initialized
    if (!console_lock.initialized) {
        return;
    }
    
    if (!console_lock.locked) {
        return;
    }

    uint32 owner_id = console_lock_owner_id();
    
    // Only the owner can release the lock (with safety fallback)
    if (console_lock.owner != owner_id) {
        // During early boot, allow forced unlock for safety
        if (console_lock.recursion == 0) {
            __sync_lock_release(&console_lock.locked);
        }
        return;
    }
    
    if (console_lock.recursion > 1) {
        console_lock.recursion--;
        return;
    }
    
    // Clear owner and release lock
    console_lock.owner = 0;
    console_lock.recursion = 0;
    
    // Store IRQ state before releasing lock
    bool saved_irq_state = console_irq_state;
    
    __sync_lock_release(&console_lock.locked);
    
    // Restore previous interrupt state
    irq_restore_safe(saved_irq_state);
}

static inline bool validate_screen_bounds(uint32 x, uint32 y) {
    return (x < screen_width) && (y < screen_height);
}

static inline bool validate_screen_offset(uint32 offset) {
    uint32 max_offset = (uint32)screen_width * screen_height * sd;
    return offset < max_offset;
}

static void safe_volatile_memmove(volatile uint8 *dest, const volatile uint8 *src, uint32 count) {
    if (dest == src || count == 0) return;
    
    if (dest < src) {
        for (uint32 i = 0; i < count; i++) {
            dest[i] = src[i];
        }
    } else {
        for (uint32 i = count; i > 0; i--) {
            dest[i - 1] = src[i - 1];
        }
    }
}

static inline void safe_tui_set_char(int x, int y, char c, int fg_color, int bg_color) {
    if (x < 0 || y < 0 || !validate_screen_bounds((uint32)x, (uint32)y)) {
        return;
    }
    
    uint32 pos = ((uint32)y * screen_width + (uint32)x) * sd;
    if (validate_screen_offset(pos + 1)) {
        vidmem[pos] = c;
        vidmem[pos + 1] = ((bg_color & 0xF) << 4) | (fg_color & 0xF);
    }
}

typedef struct {
    uint16 width;
    uint16 height;
    uint8 max_scanline; // VGA character height minus one
} text_mode_profile_t;

static const text_mode_profile_t modes[] = {
    [TEXT_MODE_80x25] = {80, 25, 0x0F},
    [TEXT_MODE_80x50] = {80, 50, 0x07},
};

static void apply_scanline_height(uint8 max_scanline) {
    bool irq_state = irq_save_and_disable_safe();
    outportb(0x3D4, 0x09);
    outportb(0x3D5, max_scanline);
    irq_restore_safe(irq_state);
}

bool screen_set_mode(text_mode_t mode) {
    if (mode >= TEXT_MODE_COUNT) {
        return false;
    }

    console_lock_acquire();
    
    text_mode_profile_t profile = modes[mode];
    apply_scanline_height(profile.max_scanline);

    screen_width = profile.width;
    screen_height = profile.height;
    cursorX = 0;
    cursorY = 0;
    clearScreen();
    
    console_lock_release();
    return true;
}

void clearLine(uint16 from, uint16 to) {
    console_lock_acquire();
    
    if (from >= screen_height) {
        console_lock_release();
        return;
    }
    if (to > screen_height) {
        to = screen_height;
    }
    
    uint32 start_cell = (uint32)from * screen_width;
    uint32 end_cell = (uint32)to * screen_width;
    uint32 max_cells = (uint32)screen_width * screen_height;
    
    for (uint32 cell = start_cell; cell < end_cell && cell < max_cells; cell++) {
        uint32 pos = cell * sd;
        
        if (!validate_screen_offset(pos + 1)) {
            break;
        }
        
        vidmem[pos] = ' ';
        vidmem[pos + 1] = (uint8)color;
    }
    
    console_lock_release();
}

// Mouse support implementation
static tui_mouse_state_t mouse_state = {0, 0, false, false, false};
static bool mouse_cursor_visible = false;
static int mouse_cursor_x = 40, mouse_cursor_y = 12;  // Screen center
static char mouse_saved_char = ' ';
static int mouse_saved_fg = 0x0F;
static int mouse_saved_bg = 0x00;
static tui_mouse_handler_t registered_mouse_handler = NULL;

void tui_show_mouse_cursor(bool visible) {
    console_lock_acquire();
    
    // Restore character at previous cursor position
    if (mouse_cursor_visible && validate_screen_bounds(mouse_cursor_x, mouse_cursor_y)) {
        safe_tui_set_char(mouse_cursor_x, mouse_cursor_y, mouse_saved_char, mouse_saved_fg, mouse_saved_bg);
    }
    
    mouse_cursor_visible = visible;
    
    // Draw cursor at current position if visible
    if (visible && validate_screen_bounds(mouse_cursor_x, mouse_cursor_y)) {
        // Save character under cursor
        uint32 pos = ((uint32)mouse_cursor_y * screen_width + (uint32)mouse_cursor_x) * sd;
        if (validate_screen_offset(pos + 1)) {
            mouse_saved_char = vidmem[pos];
            mouse_saved_fg = vidmem[pos + 1] & 0x0F;
            mouse_saved_bg = (vidmem[pos + 1] >> 4) & 0x0F;
        }
        
        // Draw mouse cursor (white block on current background)
        safe_tui_set_char(mouse_cursor_x, mouse_cursor_y, 0xDB, 0x0F, mouse_saved_bg);
    }
    
    console_lock_release();
}

void tui_set_mouse_position(int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)screen_width) x = screen_width - 1;
    if (y >= (int)screen_height) y = screen_height - 1;
    
    mouse_cursor_x = x;
    mouse_cursor_y = y;
    mouse_state.x = x;
    mouse_state.y = y;
    
    // Update cursor display if visible
    if (mouse_cursor_visible) {
        tui_show_mouse_cursor(false);
        tui_show_mouse_cursor(true);
    }
}

void tui_update_mouse_cursor(int x, int y) {
    console_lock_acquire();
    
    // Restore character at previous position
    if (mouse_cursor_visible && validate_screen_bounds(mouse_cursor_x, mouse_cursor_y)) {
        safe_tui_set_char(mouse_cursor_x, mouse_cursor_y, mouse_saved_char, mouse_saved_fg, mouse_saved_bg);
    }
    
    // Update position
    tui_set_mouse_position(x, y);
    
    // Draw cursor at new position if visible
    if (mouse_cursor_visible && validate_screen_bounds(mouse_cursor_x, mouse_cursor_y)) {
        // Save character under new cursor position
        uint32 pos = ((uint32)mouse_cursor_y * screen_width + (uint32)mouse_cursor_x) * sd;
        if (validate_screen_offset(pos + 1)) {
            mouse_saved_char = vidmem[pos];
            mouse_saved_fg = vidmem[pos + 1] & 0x0F;
            mouse_saved_bg = (vidmem[pos + 1] >> 4) & 0x0F;
        }
        
        // Draw mouse cursor
        safe_tui_set_char(mouse_cursor_x, mouse_cursor_y, 0xDB, 0x0F, mouse_saved_bg);
    }
    
    console_lock_release();
}

tui_mouse_state_t tui_get_mouse_state(void) {
    return mouse_state;
}

void tui_register_mouse_handler(tui_mouse_handler_t handler) {
    registered_mouse_handler = handler;
}

void tui_process_mouse_event(const tui_mouse_event_t* event) {
    if (!event) return;
    
    // Update internal mouse state
    mouse_state.x = event->x;
    mouse_state.y = event->y;
    mouse_state.left_button = event->left_button;
    mouse_state.right_button = event->right_button;
    mouse_state.middle_button = event->middle_button;
    
    // Update cursor position
    if (event->type == TUI_MOUSE_MOVE || event->type == TUI_MOUSE_CLICK) {
        tui_update_mouse_cursor(event->x, event->y);
    }
    
    // Call registered handler
    if (registered_mouse_handler) {
        registered_mouse_handler(event);
    }
}

bool tui_is_point_in_rect(int px, int py, int x, int y, int width, int height) {
    return px >= x && px < x + width && py >= y && py < y + height;
}

int tui_get_clicked_menu_item(int mouse_x, int mouse_y, int menu_x, int menu_y, int item_count) {
    if (mouse_x < menu_x || mouse_x >= menu_x + 40) {  // Assume 40 char menu width
        return -1;
    }
    
    for (int i = 0; i < item_count; i++) {
        if (mouse_y == menu_y + i) {
            return i;
        }
    }
    
    return -1;
}

// Essential print functions
void print(const char* message) {
    if (!message) return;
    
    int i = 0;
    while (message[i] != '\0') {
        printch(message[i]);
        i++;
    }
}

void printch(char c) {
    console_lock_acquire();
    
    if (c == '\n') {
        cursorY++;
        cursorX = 0;
    } else if (c == '\r') {
        cursorX = 0;
    } else if (c == '\t') {
        cursorX = (cursorX + 8) & ~(8 - 1); // Round up to next multiple of 8
    } else if (c == '\b') {
        if (cursorX > 0) {
            cursorX--;
        }
    } else {
        uint32 index = (cursorY * screen_width + cursorX) * 2;
        vidmem[index] = c;
        vidmem[index + 1] = color;
        cursorX++;
    }
    
    // Handle screen wrapping
    if (cursorX >= screen_width) {
        cursorX = 0;
        cursorY++;
    }
    
    // Handle scrolling
    if (cursorY >= screen_height) {
        scrollUp(1);
        cursorY = screen_height - 1;
    }
    
    updateCursor();
    console_lock_release();
}

void print_colored(const char* message, int foreground, int background) {
    uint8 old_color = color;
    color = (background << 4) | foreground;
    print(message);
    color = old_color;
}

void print_dec(uint32 n) {
    if (n == 0) {
        printch('0');
        return;
    }
    
    char buffer[12];
    int i = 0;
    
    while (n > 0) {
        buffer[i++] = '0' + (n % 10);
        n /= 10;
    }
    
    while (i > 0) {
        printch(buffer[--i]);
    }
}

void print_hex(uint32 n) {
    print("0x");
    
    for (int i = 7; i >= 0; i--) {
        uint8 digit = (n >> (i * 4)) & 0xF;
        if (digit < 10) {
            printch('0' + digit);
        } else {
            printch('A' + digit - 10);
        }
    }
}

void clearScreen(void) {
    console_lock_acquire();
    
    for (uint32 i = 0; i < screen_width * screen_height; i++) {
        vidmem[i * 2] = ' ';
        vidmem[i * 2 + 1] = 0x07; // Light grey on black
    }
    
    cursorX = 0;
    cursorY = 0;
    updateCursor();
    
    console_lock_release();
}

// TUI functions - basic implementations
void tui_draw_status_bar(int y, const char* left_text, const char* right_text, int fg_color, int bg_color) {
    console_lock_acquire();
    
    uint8 color_attr = (bg_color << 4) | fg_color;
    
    // Draw background
    for (int x = 0; x < screen_width; x++) {
        uint32 index = (y * screen_width + x) * 2;
        vidmem[index] = ' ';
        vidmem[index + 1] = color_attr;
    }
    
    // Draw left text
    if (left_text) {
        int len = strlen(left_text);
        for (int i = 0; i < len && i < screen_width; i++) {
            uint32 index = (y * screen_width + i) * 2;
            vidmem[index] = left_text[i];
            vidmem[index + 1] = color_attr;
        }
    }
    
    // Draw right text
    if (right_text) {
        int len = strlen(right_text);
        int start_x = screen_width - len;
        if (start_x > 0) {
            for (int i = 0; i < len && start_x + i < screen_width; i++) {
                uint32 index = (y * screen_width + start_x + i) * 2;
                vidmem[index] = right_text[i];
                vidmem[index + 1] = color_attr;
            }
        }
    }
    
    console_lock_release();
}

// Missing basic functions that were referenced but not implemented
void scrollUp(uint16 lineNumber) {
    console_lock_acquire();
    
    // Move all lines up by lineNumber positions
    uint32 source_start = lineNumber * screen_width * sd;
    uint32 dest_start = 0;
    uint32 copy_size = (screen_height - lineNumber) * screen_width * sd;
    
    if (lineNumber > 0 && lineNumber < screen_height) {
        safe_volatile_memmove(&vidmem[dest_start], &vidmem[source_start], copy_size);
        
        // Clear the bottom lines
        uint32 clear_start = (screen_height - lineNumber) * screen_width * sd;
        for (uint32 i = 0; i < lineNumber * screen_width * sd; i += sd) {
            vidmem[clear_start + i] = ' ';
            vidmem[clear_start + i + 1] = (uint8)color;
        }
    }
    
    console_lock_release();
}

void updateCursor() {
    uint16 cursorLocation = cursorY * screen_width + cursorX;
    
    // Send the high byte
    outportb(0x3D4, 14);
    outportb(0x3D5, cursorLocation >> 8);
    
    // Send the low byte
    outportb(0x3D4, 15);
    outportb(0x3D5, cursorLocation);
}

void newLineCheck() {
    if (cursorY >= screen_height - 1) {
        scrollUp(1);
        cursorY = screen_height - 1;
        cursorX = 0;
    }
}

void printl(const char* ch) {
    print(ch);
    print("\n");
}

void set_screen_color_from_color_code(int color_code) {
    color = color_code;
}

void set_screen_color(int text_color, int bg_color) {
    color = (bg_color << 4) | (text_color & 0x0F);
}

// TUI Functions - proper implementations matching header signatures
void tui_set_char_at(int x, int y, char c, int fg_color, int bg_color) {
    safe_tui_set_char(x, y, c, fg_color, bg_color);
}

void tui_draw_box(int x, int y, int width, int height, int fg_color, int bg_color, bool double_border) {
    console_lock_acquire();
    
    char horizontal = double_border ? 0xCD : '-';  // ═ or -
    char vertical = double_border ? 0xBA : '|';    // ║ or |
    char top_left = double_border ? 0xC9 : '+';    // ╔ or +
    char top_right = double_border ? 0xBB : '+';   // ╗ or +
    char bottom_left = double_border ? 0xC8 : '+'; // ╚ or +
    char bottom_right = double_border ? 0xBC : '+'; // ╝ or +
    
    // Draw corners
    safe_tui_set_char(x, y, top_left, fg_color, bg_color);
    safe_tui_set_char(x + width - 1, y, top_right, fg_color, bg_color);
    safe_tui_set_char(x, y + height - 1, bottom_left, fg_color, bg_color);
    safe_tui_set_char(x + width - 1, y + height - 1, bottom_right, fg_color, bg_color);
    
    // Draw horizontal lines
    for (int i = 1; i < width - 1; i++) {
        safe_tui_set_char(x + i, y, horizontal, fg_color, bg_color);
        safe_tui_set_char(x + i, y + height - 1, horizontal, fg_color, bg_color);
    }
    
    // Draw vertical lines
    for (int i = 1; i < height - 1; i++) {
        safe_tui_set_char(x, y + i, vertical, fg_color, bg_color);
        safe_tui_set_char(x + width - 1, y + i, vertical, fg_color, bg_color);
    }
    
    console_lock_release();
}

void tui_draw_shadow(int x, int y, int width, int height) {
    console_lock_acquire();
    
    // Draw shadow on right side
    for (int i = 1; i <= height; i++) {
        if (x + width < (int)screen_width && y + i < (int)screen_height) {
            uint32 pos = ((y + i) * screen_width + (x + width)) * sd;
            if (validate_screen_offset(pos + 1)) {
                vidmem[pos + 1] = (vidmem[pos + 1] & 0x0F) | 0x80; // Make background darker
            }
        }
    }
    
    // Draw shadow on bottom
    for (int i = 1; i < width; i++) {
        if (x + i < (int)screen_width && y + height < (int)screen_height) {
            uint32 pos = ((y + height) * screen_width + (x + i)) * sd;
            if (validate_screen_offset(pos + 1)) {
                vidmem[pos + 1] = (vidmem[pos + 1] & 0x0F) | 0x80; // Make background darker
            }
        }
    }
    
    console_lock_release();
}

void tui_draw_window(int x, int y, int width, int height, const char* title, int fg_color, int bg_color) {
    console_lock_acquire();
    
    // Draw the box border
    tui_draw_box(x, y, width, height, fg_color, bg_color, false);
    
    // Clear inside area
    for (int i = 1; i < height - 1; i++) {
        for (int j = 1; j < width - 1; j++) {
            safe_tui_set_char(x + j, y + i, ' ', fg_color, bg_color);
        }
    }
    
    // Draw title if provided
    if (title) {
        int len = strlen(title);
        if (len > width - 4) len = width - 4; // Leave space for borders and padding
        int title_x = x + (width - len) / 2;
        
        for (int i = 0; i < len; i++) {
            safe_tui_set_char(title_x + i, y, title[i], fg_color, bg_color);
        }
    }
    
    console_lock_release();
}

void tui_print_at(int x, int y, const char* text, int fg_color, int bg_color) {
    console_lock_acquire();
    
    if (text && y >= 0 && y < (int)screen_height) {
        int len = strlen(text);
        for (int i = 0; i < len && x + i >= 0 && x + i < (int)screen_width; i++) {
            safe_tui_set_char(x + i, y, text[i], fg_color, bg_color);
        }
    }
    
    console_lock_release();
}

void tui_center_text(int x, int y, int width, const char* text, int fg_color, int bg_color) {
    if (!text) return;
    
    int len = strlen(text);
    int start_x = x + (width - len) / 2;
    tui_print_at(start_x, y, text, fg_color, bg_color);
}

// Enhanced TUI functions with proper implementations
void tui_draw_filled_box(int x, int y, int width, int height, char fill_char, int fg_color, int bg_color) {
    console_lock_acquire();
    
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            safe_tui_set_char(x + j, y + i, fill_char, fg_color, bg_color);
        }
    }
    
    console_lock_release();
}

void tui_draw_shaded_box(int x, int y, int width, int height, int shade_level, int fg_color, int bg_color) {
    console_lock_acquire();
    
    char shade_chars[] = {' ', 0xB0, 0xB1, 0xB2, 0xDB}; // Light to dark shading
    char fill_char = shade_chars[shade_level % 5];
    
    tui_draw_filled_box(x, y, width, height, fill_char, fg_color, bg_color);
    
    console_lock_release();
}

void tui_draw_progress_bar(int x, int y, int width, int progress, int max_progress, int fg_color, int bg_color) {
    console_lock_acquire();
    
    if (max_progress <= 0) max_progress = 1;
    if (progress < 0) progress = 0;
    if (progress > max_progress) progress = max_progress;
    
    int filled_width = (progress * width) / max_progress;
    
    // Draw filled portion
    for (int i = 0; i < filled_width; i++) {
        safe_tui_set_char(x + i, y, 0xDB, fg_color, bg_color); // Full block
    }
    
    // Draw empty portion
    for (int i = filled_width; i < width; i++) {
        safe_tui_set_char(x + i, y, 0xB0, fg_color, bg_color); // Light shade
    }
    
    console_lock_release();
}

void tui_draw_menu_item(int x, int y, int width, const char* text, bool selected, int fg_color, int bg_color) {
    console_lock_acquire();
    
    int item_fg = selected ? bg_color : fg_color;
    int item_bg = selected ? fg_color : bg_color;
    
    // Clear the line
    tui_draw_filled_box(x, y, width, 1, ' ', item_fg, item_bg);
    
    // Print the text
    if (text) {
        int len = strlen(text);
        if (len > width - 2) len = width - 2;
        
        tui_print_at(x + 1, y, text, item_fg, item_bg);
        
        // Add selection indicator
        if (selected) {
            safe_tui_set_char(x, y, '>', item_fg, item_bg);
        }
    }
    
    console_lock_release();
}

void tui_draw_border(int x, int y, int width, int height, int border_style, int fg_color, int bg_color) {
    tui_draw_box(x, y, width, height, fg_color, bg_color, border_style == 1);
}

void tui_draw_graph(int x, int y, int width, int height, uint32* data, int data_count, const char* title, int fg_color, int bg_color) {
    console_lock_acquire();
    
    // Draw frame
    tui_draw_window(x, y, width, height, title, fg_color, bg_color);
    
    if (!data || data_count <= 0) {
        console_lock_release();
        return;
    }
    
    // Find max value for scaling
    uint32 max_val = 1;
    for (int i = 0; i < data_count; i++) {
        if (data[i] > max_val) max_val = data[i];
    }
    
    int graph_width = width - 4;  // Leave space for borders
    int graph_height = height - 3; // Leave space for title and borders
    
    // Draw graph bars
    for (int i = 0; i < data_count && i < graph_width; i++) {
        int bar_height = (data[i] * graph_height) / max_val;
        
        for (int j = 0; j < bar_height; j++) {
            int draw_y = y + height - 2 - j; // Bottom up
            safe_tui_set_char(x + 2 + i, draw_y, 0xDB, fg_color, bg_color); // Full block
        }
    }
    
    console_lock_release();
}

// Advanced TUI debugging and visualization functions
void tui_draw_hex_viewer(int x, int y, int width, int height, void* data, uint32 base_addr, int fg_color, int bg_color) {
    console_lock_acquire();
    
    tui_draw_window(x, y, width, height, "HEX VIEWER", fg_color, bg_color);
    
    if (!data) {
        tui_print_at(x + 2, y + 2, "No data to display", fg_color, bg_color);
        console_lock_release();
        return;
    }
    
    uint8* bytes = (uint8*)data;
    int lines_to_show = height - 3;
    int bytes_per_line = (width - 12) / 3; // Space for address and formatting
    
    for (int line = 0; line < lines_to_show; line++) {
        int row = y + 2 + line;
        
        // Print address
        char addr_str[12];
        uint32 addr = base_addr + (line * bytes_per_line);
        
        // Convert address to hex string manually
        addr_str[0] = '0';
        addr_str[1] = 'x';
        for (int i = 0; i < 8; i++) {
            uint8 digit = (addr >> ((7-i) * 4)) & 0xF;
            addr_str[2 + i] = (digit < 10) ? ('0' + digit) : ('A' + digit - 10);
        }
        addr_str[10] = ':';
        addr_str[11] = '\0';
        
        tui_print_at(x + 1, row, addr_str, fg_color, bg_color);
        
        // Print hex bytes
        for (int i = 0; i < bytes_per_line && (line * bytes_per_line + i) < 64; i++) { // Limit to reasonable amount
            uint8 byte = bytes[line * bytes_per_line + i];
            char hex_str[4];
            
            hex_str[0] = ' ';
            uint8 high = (byte >> 4) & 0xF;
            uint8 low = byte & 0xF;
            hex_str[1] = (high < 10) ? ('0' + high) : ('A' + high - 10);
            hex_str[2] = (low < 10) ? ('0' + low) : ('A' + low - 10);
            hex_str[3] = '\0';
            
            tui_print_at(x + 12 + (i * 3), row, hex_str, fg_color, bg_color);
        }
    }
    
    console_lock_release();
}

void tui_draw_scrollbar(int x, int y, int height, int visible, int total, int position, int fg_color, int bg_color) {
    console_lock_acquire();
    
    if (total <= visible) {
        console_lock_release();
        return; // No scrollbar needed
    }
    
    // Draw scrollbar track
    for (int i = 0; i < height; i++) {
        safe_tui_set_char(x, y + i, 0xB1, fg_color, bg_color); // Medium shade
    }
    
    // Calculate thumb position and size
    int thumb_size = (visible * height) / total;
    if (thumb_size < 1) thumb_size = 1;
    
    int thumb_pos = (position * (height - thumb_size)) / (total - visible);
    
    // Draw thumb
    for (int i = 0; i < thumb_size; i++) {
        safe_tui_set_char(x, y + thumb_pos + i, 0xDB, fg_color, bg_color); // Full block
    }
    
    console_lock_release();
}

void tui_draw_memory_map(int x, int y, int width, int height, int fg_color, int bg_color) {
    console_lock_acquire();
    
    tui_draw_window(x, y, width, height, "MEMORY MAP", fg_color, bg_color);
    
    // Simple memory map display
    tui_print_at(x + 2, y + 2, "0x00000000 - 0x000FFFFF: Lower Memory", fg_color, bg_color);
    tui_print_at(x + 2, y + 3, "0x00100000 - 0x003FFFFF: Kernel Space", fg_color, bg_color);
    tui_print_at(x + 2, y + 4, "0x00400000 - 0x7FFFFFFF: User Space", fg_color, bg_color);
    tui_print_at(x + 2, y + 5, "0x80000000 - 0xFFFFFFFF: Hardware/MMIO", fg_color, bg_color);
    
    console_lock_release();
}

// Full-screen TUI system
void tui_fullscreen_clear(const tui_fullscreen_theme_t* theme) {
    console_lock_acquire();
    
    int bg = theme ? theme->bg_color : 0x01;
    int fg = theme ? theme->fg_color : 0x0F;
    
    // Clear entire screen with theme colors
    for (uint32 i = 0; i < screen_width * screen_height; i++) {
        vidmem[i * 2] = ' ';
        vidmem[i * 2 + 1] = (bg << 4) | fg;
    }
    
    console_lock_release();
}

void tui_fullscreen_header(const tui_fullscreen_theme_t* theme) {
    if (!theme) return;
    
    int fg = theme->accent_color;
    int bg = theme->bg_color;
    
    tui_draw_status_bar(0, theme->title, theme->subtitle, fg, bg);
    
    if (theme->help_text) {
        tui_draw_status_bar(1, theme->help_text, "", fg, bg);
    }
}

void tui_fullscreen_footer(const tui_fullscreen_theme_t* theme) {
    if (!theme) return;
    
    int fg = theme->accent_color;
    int bg = theme->bg_color;
    
    tui_draw_status_bar(screen_height - 1, "ESC=Exit", "Use Arrow Keys", fg, bg);
}

void tui_fullscreen_content_area(int* content_x, int* content_y, int* content_width, int* content_height) {
    if (content_x) *content_x = 1;
    if (content_y) *content_y = 3;  // Leave space for header
    if (content_width) *content_width = screen_width - 2;
    if (content_height) *content_height = screen_height - 5; // Header + footer
}

void tui_print_table_row(int x, int y, int width, const char* label, const char* value, int label_color, int value_color, int bg_color) {
    console_lock_acquire();
    
    // Clear the row
    tui_draw_filled_box(x, y, width, 1, ' ', label_color, bg_color);
    
    if (label) {
        tui_print_at(x, y, label, label_color, bg_color);
    }
    
    if (value) {
        int label_len = label ? strlen(label) : 0;
        int separator_pos = x + label_len + 1;
        tui_print_at(separator_pos, y, ": ", label_color, bg_color);
        tui_print_at(separator_pos + 2, y, value, value_color, bg_color);
    }
    
    console_lock_release();
}

void tui_print_section_header(int x, int y, int width, const char* title, int fg_color, int bg_color) {
    console_lock_acquire();
    
    // Draw separator line
    tui_draw_filled_box(x, y, width, 1, 0xC4, fg_color, bg_color); // ─ character
    
    if (title) {
        int len = strlen(title);
        int title_x = x + (width - len - 4) / 2; // Center with padding
        
        // Clear space for title
        tui_draw_filled_box(title_x, y, len + 4, 1, ' ', fg_color, bg_color);
        tui_print_at(title_x + 2, y, title, fg_color, bg_color);
    }
    
    console_lock_release();
}
