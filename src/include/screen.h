#ifndef SCREEN_H
#define SCREEN_H
#include "system.h"
#include "string.h"
extern uint16 screen_width, screen_height;
extern const uint8 sd;

typedef enum {
    TEXT_MODE_80x25 = 0,
    TEXT_MODE_80x50,
    TEXT_MODE_COUNT
} text_mode_t;

// Console initialization
void console_init(void);

bool screen_set_mode(text_mode_t mode);

void clearLine(uint16 from, uint16 to);

void updateCursor();

void clearScreen();

void scrollUp(uint16 lineNumber);

void newLineCheck();

void printch(char c);

void print (const char* ch);
void printl (const char* ch);
void set_screen_color_from_color_code(int color_code);
void set_screen_color(int text_color,int bg_color);
void print_colored(const char* ch,int text_color,int bg_color);
void print_hex(uint32 value);
void print_dec(uint32 value);

// TUI Functions
void tui_set_char_at(int x, int y, char c, int fg_color, int bg_color);
void tui_draw_box(int x, int y, int width, int height, int fg_color, int bg_color, bool double_border);
void tui_draw_shadow(int x, int y, int width, int height);
void tui_draw_window(int x, int y, int width, int height, const char* title, int fg_color, int bg_color);
void tui_print_at(int x, int y, const char* text, int fg_color, int bg_color);
void tui_center_text(int x, int y, int width, const char* text, int fg_color, int bg_color);

// Enhanced TUI functions with shading and menu support
void tui_draw_filled_box(int x, int y, int width, int height, char fill_char, int fg_color, int bg_color);
void tui_draw_shaded_box(int x, int y, int width, int height, int shade_level, int fg_color, int bg_color);
void tui_draw_progress_bar(int x, int y, int width, int progress, int max_progress, int fg_color, int bg_color);
void tui_draw_menu_item(int x, int y, int width, const char* text, bool selected, int fg_color, int bg_color);
void tui_draw_border(int x, int y, int width, int height, int border_style, int fg_color, int bg_color);

// Advanced TUI debugging and visualization functions
void tui_draw_graph(int x, int y, int width, int height, uint32* data, int data_count, const char* title, int fg_color, int bg_color);
void tui_draw_status_bar(int y, const char* left_text, const char* right_text, int fg_color, int bg_color);
void tui_draw_hex_viewer(int x, int y, int width, int height, void* data, uint32 base_addr, int fg_color, int bg_color);
void tui_draw_scrollbar(int x, int y, int height, int visible, int total, int position, int fg_color, int bg_color);
void tui_draw_memory_map(int x, int y, int width, int height, int fg_color, int bg_color);

// Full-screen TUI system
typedef struct {
    const char* title;
    const char* subtitle;
    const char* help_text;
    int bg_color;
    int fg_color;
    int accent_color;
} tui_fullscreen_theme_t;

void tui_fullscreen_clear(const tui_fullscreen_theme_t* theme);
void tui_fullscreen_header(const tui_fullscreen_theme_t* theme);
void tui_fullscreen_footer(const tui_fullscreen_theme_t* theme);
void tui_fullscreen_content_area(int* content_x, int* content_y, int* content_width, int* content_height);
void tui_print_table_row(int x, int y, int width, const char* label, const char* value, int label_color, int value_color, int bg_color);
void tui_print_section_header(int x, int y, int width, const char* title, int fg_color, int bg_color);

// Mouse support for TUI
typedef struct {
    int x, y;
    bool left_button;
    bool right_button;
    bool middle_button;
} tui_mouse_state_t;

typedef enum {
    TUI_MOUSE_CLICK,
    TUI_MOUSE_MOVE,
    TUI_MOUSE_RELEASE
} tui_mouse_event_type_t;

typedef struct {
    tui_mouse_event_type_t type;
    int x, y;
    bool left_button;
    bool right_button;
    bool middle_button;
} tui_mouse_event_t;

// Mouse cursor support
void tui_show_mouse_cursor(bool visible);
void tui_set_mouse_position(int x, int y);
void tui_update_mouse_cursor(int x, int y);
tui_mouse_state_t tui_get_mouse_state(void);

// Mouse event handling
typedef bool (*tui_mouse_handler_t)(const tui_mouse_event_t* event);
void tui_register_mouse_handler(tui_mouse_handler_t handler);
void tui_process_mouse_event(const tui_mouse_event_t* event);

// Interactive TUI elements with mouse support
bool tui_is_point_in_rect(int px, int py, int x, int y, int width, int height);
int tui_get_clicked_menu_item(int mouse_x, int mouse_y, int menu_x, int menu_y, int item_count);


#endif
