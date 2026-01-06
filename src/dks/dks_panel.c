/*
 * DKS Panel Implementation
 * Configurable taskbar/dock with system tray and clock
 */

#include "../include/dks/dks_panel.h"
#include "../include/dks/dks_draw.h"
#include "../include/dks/dks_menu.h"
#include "../include/dks/dks_core.h"
#include <string.h>

// External functions
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);
extern uint32_t timer_get_ticks(void);

// RTC functions for clock
extern uint8_t rtc_get_hours(void);
extern uint8_t rtc_get_minutes(void);
extern uint8_t rtc_get_seconds(void);

// Global panel state
static dks_panel_t panel;
static bool panel_initialized = false;

// System tray icons
#define MAX_SYSTRAY_ICONS 16
static systray_icon_t systray_icons[MAX_SYSTRAY_ICONS];
static uint32_t systray_icon_count = 0;

// Next panel item ID
static uint32_t next_panel_item_id = 1;

// Start menu state
static dks_start_menu_t start_menu_state;

void dks_panel_init(void) {
    if (panel_initialized) return;

    memset(&panel, 0, sizeof(dks_panel_t));
    memset(&start_menu_state, 0, sizeof(dks_start_menu_t));
    memset(systray_icons, 0, sizeof(systray_icons));

    // Default configuration
    panel.config.position = PANEL_POSITION_BOTTOM;
    panel.config.mode = PANEL_MODE_TASKBAR;
    panel.config.size = 36;
    panel.config.auto_hide = false;
    panel.config.show_start_button = true;
    panel.config.show_window_list = true;
    panel.config.show_systray = true;
    panel.config.show_clock = true;
    panel.config.show_seconds = false;
    panel.config.icon_size = 24;
    panel.config.icon_spacing = 4;
    panel.config.transparent = false;
    panel.config.opacity = 255;

    panel.hidden = false;
    panel.dirty = true;

    panel_initialized = true;

    // Recalculate bounds
    uint32_t screen_w = dks_get_screen_width();
    uint32_t screen_h = dks_get_screen_height();

    switch (panel.config.position) {
        case PANEL_POSITION_BOTTOM:
            panel.bounds.x = 0;
            panel.bounds.y = screen_h - panel.config.size;
            panel.bounds.width = screen_w;
            panel.bounds.height = panel.config.size;
            break;
        case PANEL_POSITION_TOP:
            panel.bounds.x = 0;
            panel.bounds.y = 0;
            panel.bounds.width = screen_w;
            panel.bounds.height = panel.config.size;
            break;
        case PANEL_POSITION_LEFT:
            panel.bounds.x = 0;
            panel.bounds.y = 0;
            panel.bounds.width = panel.config.size;
            panel.bounds.height = screen_h;
            break;
        case PANEL_POSITION_RIGHT:
            panel.bounds.x = screen_w - panel.config.size;
            panel.bounds.y = 0;
            panel.bounds.width = panel.config.size;
            panel.bounds.height = screen_h;
            break;
    }
}

void dks_panel_shutdown(void) {
    // Free panel items
    dks_panel_clear_items();
    panel_initialized = false;
}

// Configuration

void dks_panel_set_config(const dks_panel_config_t* config) {
    if (!config) return;
    memcpy(&panel.config, config, sizeof(dks_panel_config_t));
    panel.dirty = true;
    dks_panel_init();  // Recalculate bounds
}

dks_panel_config_t* dks_panel_get_config(void) {
    return &panel.config;
}

void dks_panel_set_position(panel_position_t position) {
    panel.config.position = position;
    panel.dirty = true;
    dks_panel_init();
}

void dks_panel_set_mode(panel_mode_t mode) {
    panel.config.mode = mode;
    panel.dirty = true;
}

void dks_panel_set_size(uint32_t size) {
    panel.config.size = size;
    panel.dirty = true;
    dks_panel_init();
}

void dks_panel_set_auto_hide(bool auto_hide) {
    panel.config.auto_hide = auto_hide;
    if (!auto_hide) {
        panel.hidden = false;
    }
}

// Panel items

static panel_item_t* create_panel_item(panel_item_type_t type) {
    panel_item_t* item = (panel_item_t*)kmalloc(sizeof(panel_item_t));
    if (!item) return NULL;

    memset(item, 0, sizeof(panel_item_t));
    item->type = type;
    item->id = next_panel_item_id++;
    item->visible = true;
    item->enabled = true;

    return item;
}

static void add_panel_item(panel_item_t* item) {
    if (!item) return;

    item->next = NULL;
    item->prev = NULL;

    if (!panel.items) {
        panel.items = item;
    } else {
        panel_item_t* last = panel.items;
        while (last->next) last = last->next;
        last->next = item;
        item->prev = last;
    }
    panel.item_count++;
    panel.dirty = true;
}

panel_item_t* dks_panel_add_launcher(const char* app_id, const char* label, bmp_image_t* icon, void (*callback)(panel_item_t*, void*), void* data) {
    panel_item_t* item = create_panel_item(PANEL_ITEM_APP_LAUNCHER);
    if (!item) return NULL;

    if (label) strncpy(item->label, label, sizeof(item->label) - 1);
    if (app_id) strncpy(item->tooltip, app_id, sizeof(item->tooltip) - 1);
    item->icon = icon;
    item->on_click = callback;
    item->callback_data = data;

    add_panel_item(item);
    return item;
}

panel_item_t* dks_panel_add_separator(void) {
    panel_item_t* item = create_panel_item(PANEL_ITEM_SEPARATOR);
    if (item) {
        add_panel_item(item);
    }
    return item;
}

void dks_panel_remove_item(panel_item_t* item) {
    if (!item) return;

    if (item->prev) {
        item->prev->next = item->next;
    } else {
        panel.items = item->next;
    }

    if (item->next) {
        item->next->prev = item->prev;
    }

    kfree(item);
    panel.item_count--;
    panel.dirty = true;
}

void dks_panel_clear_items(void) {
    panel_item_t* item = panel.items;
    while (item) {
        panel_item_t* next = item->next;
        kfree(item);
        item = next;
    }
    panel.items = NULL;
    panel.item_count = 0;
    panel.dirty = true;
}

// Window list management

void dks_panel_update_window_list(void) {
    if (panel.config.mode != PANEL_MODE_TASKBAR || !panel.config.show_window_list) {
        return;
    }

    // Remove existing window buttons
    panel_item_t* item = panel.items;
    while (item) {
        panel_item_t* next = item->next;
        if (item->type == PANEL_ITEM_WINDOW_BUTTON) {
            dks_panel_remove_item(item);
        }
        item = next;
    }

    // Add buttons for all windows
    // This would iterate through dks_core's window list
    // For now, placeholder
    panel.dirty = true;
}

void dks_panel_add_window(dks_window_t* window) {
    if (!window || panel.config.mode != PANEL_MODE_TASKBAR) return;

    panel_item_t* item = create_panel_item(PANEL_ITEM_WINDOW_BUTTON);
    if (!item) return;

    strncpy(item->label, window->title, sizeof(item->label) - 1);
    item->icon = window->app_icon;
    item->window = window;

    add_panel_item(item);
}

void dks_panel_remove_window(dks_window_t* window) {
    if (!window) return;

    panel_item_t* item = panel.items;
    while (item) {
        if (item->type == PANEL_ITEM_WINDOW_BUTTON && item->window == window) {
            dks_panel_remove_item(item);
            return;
        }
        item = item->next;
    }
}

void dks_panel_update_window(dks_window_t* window) {
    if (!window) return;

    panel_item_t* item = panel.items;
    while (item) {
        if (item->type == PANEL_ITEM_WINDOW_BUTTON && item->window == window) {
            strncpy(item->label, window->title, sizeof(item->label) - 1);
            item->active = window->focused;
            panel.dirty = true;
            return;
        }
        item = item->next;
    }
}

// System tray

void dks_systray_add_icon(uint32_t id, bmp_image_t* icon, const char* tooltip, void (*on_click)(void*), void* data) {
    if (systray_icon_count >= MAX_SYSTRAY_ICONS) return;

    systray_icons[systray_icon_count].id = id;
    systray_icons[systray_icon_count].icon = icon;
    if (tooltip) strncpy(systray_icons[systray_icon_count].tooltip, tooltip, sizeof(systray_icons[0].tooltip) - 1);
    systray_icons[systray_icon_count].on_click = on_click;
    systray_icons[systray_icon_count].callback_data = data;
    systray_icon_count++;
    panel.dirty = true;
}

void dks_systray_remove_icon(uint32_t id) {
    for (uint32_t i = 0; i < systray_icon_count; i++) {
        if (systray_icons[i].id == id) {
            for (uint32_t j = i; j < systray_icon_count - 1; j++) {
                systray_icons[j] = systray_icons[j + 1];
            }
            systray_icon_count--;
            panel.dirty = true;
            return;
        }
    }
}

void dks_systray_update_icon(uint32_t id, bmp_image_t* new_icon) {
    for (uint32_t i = 0; i < systray_icon_count; i++) {
        if (systray_icons[i].id == id) {
            systray_icons[i].icon = new_icon;
            panel.dirty = true;
            return;
        }
    }
}

void dks_systray_set_tooltip(uint32_t id, const char* tooltip) {
    for (uint32_t i = 0; i < systray_icon_count; i++) {
        if (systray_icons[i].id == id) {
            if (tooltip) {
                strncpy(systray_icons[i].tooltip, tooltip, sizeof(systray_icons[0].tooltip) - 1);
            }
            return;
        }
    }
}

// Clock

void dks_panel_update_clock(void) {
    // Clock is rendered directly, no state to update
    panel.dirty = true;
}

void dks_panel_get_time_string(char* buffer, uint32_t size, bool show_seconds) {
    if (!buffer || size == 0) return;

    uint8_t hours = 12;  // Default
    uint8_t minutes = 0;
    uint8_t seconds = 0;

    // Try to get from RTC (may not be available in all builds)
    #ifdef HAS_RTC
    hours = rtc_get_hours();
    minutes = rtc_get_minutes();
    seconds = rtc_get_seconds();
    #endif

    if (show_seconds) {
        // Format: HH:MM:SS
        if (size >= 9) {
            buffer[0] = '0' + (hours / 10);
            buffer[1] = '0' + (hours % 10);
            buffer[2] = ':';
            buffer[3] = '0' + (minutes / 10);
            buffer[4] = '0' + (minutes % 10);
            buffer[5] = ':';
            buffer[6] = '0' + (seconds / 10);
            buffer[7] = '0' + (seconds % 10);
            buffer[8] = '\0';
        }
    } else {
        // Format: HH:MM
        if (size >= 6) {
            buffer[0] = '0' + (hours / 10);
            buffer[1] = '0' + (hours % 10);
            buffer[2] = ':';
            buffer[3] = '0' + (minutes / 10);
            buffer[4] = '0' + (minutes % 10);
            buffer[5] = '\0';
        }
    }
}

// Input handling

bool dks_panel_handle_mouse_move(int32_t x, int32_t y) {
    if (!dks_panel_contains_point(x, y)) {
        // Clear hover states
        panel_item_t* item = panel.items;
        while (item) {
            if (item->hovered) {
                item->hovered = false;
                panel.dirty = true;
            }
            item = item->next;
        }
        panel.hovered_item = NULL;
        panel.mouse_in_panel = false;
        return false;
    }

    panel.mouse_in_panel = true;

    // Find hovered item
    panel_item_t* hit = dks_panel_hit_test(x, y);

    if (panel.hovered_item != hit) {
        if (panel.hovered_item) {
            panel.hovered_item->hovered = false;
        }
        if (hit) {
            hit->hovered = true;
        }
        panel.hovered_item = hit;
        panel.dirty = true;
    }

    return true;
}

bool dks_panel_handle_mouse_button(int32_t x, int32_t y, uint8_t button, bool pressed) {
    if (!dks_panel_contains_point(x, y)) return false;

    panel_item_t* hit = dks_panel_hit_test(x, y);

    if (pressed && button == MOUSE_BUTTON_LEFT) {
        if (hit) {
            hit->pressed = true;
            panel.dirty = true;

            // Handle click based on item type
            switch (hit->type) {
                case PANEL_ITEM_START_BUTTON:
                    dks_start_menu_toggle();
                    break;

                case PANEL_ITEM_APP_LAUNCHER:
                    if (hit->on_click) {
                        hit->on_click(hit, hit->callback_data);
                    }
                    break;

                case PANEL_ITEM_WINDOW_BUTTON:
                    if (hit->window) {
                        if (hit->window->state == DKS_WINDOW_STATE_MINIMIZED) {
                            dks_window_restore(hit->window);
                        }
                        dks_window_focus(hit->window);
                        dks_window_bring_to_front(hit->window);
                    }
                    break;

                default:
                    break;
            }
        } else {
            // Check if clicking in systray area
            // Check if clicking on clock (could show calendar)
        }
    } else if (!pressed && button == MOUSE_BUTTON_LEFT) {
        // Release
        panel_item_t* item = panel.items;
        while (item) {
            if (item->pressed) {
                item->pressed = false;
                panel.dirty = true;
            }
            item = item->next;
        }
    }

    return true;
}

bool dks_panel_contains_point(int32_t x, int32_t y) {
    if (!panel_initialized) return false;
    if (panel.hidden) return false;

    return x >= panel.bounds.x && y >= panel.bounds.y &&
           x < panel.bounds.x + (int32_t)panel.bounds.width &&
           y < panel.bounds.y + (int32_t)panel.bounds.height;
}

panel_item_t* dks_panel_hit_test(int32_t x, int32_t y) {
    if (!dks_panel_contains_point(x, y)) return NULL;

    panel_item_t* item = panel.items;
    while (item) {
        if (item->visible && item->type != PANEL_ITEM_SEPARATOR &&
            x >= item->bounds.x && y >= item->bounds.y &&
            x < item->bounds.x + (int32_t)item->bounds.width &&
            y < item->bounds.y + (int32_t)item->bounds.height) {
            return item;
        }
        item = item->next;
    }
    return NULL;
}

// Rendering

void dks_panel_render(graphics_surface_t* surface, const dks_theme_t* theme) {
    if (!panel_initialized || !surface) return;
    if (panel.hidden) return;

    // Draw panel background
    if (panel.config.transparent && panel.config.opacity < 255) {
        graphics_color_t bg = theme->panel_background;
        bg.a = panel.config.opacity;
        dks_fill_rect(surface, &panel.bounds, bg);
    } else {
        dks_fill_rect(surface, &panel.bounds, theme->panel_background);
    }

    // Draw border
    if (panel.config.position == PANEL_POSITION_BOTTOM) {
        dks_draw_hline(surface, panel.bounds.x, panel.bounds.y, panel.bounds.width, theme->panel_border);
    } else if (panel.config.position == PANEL_POSITION_TOP) {
        dks_draw_hline(surface, panel.bounds.x, panel.bounds.y + panel.bounds.height - 1, panel.bounds.width, theme->panel_border);
    }

    // Calculate item positions and render
    int32_t x = panel.bounds.x + 4;
    int32_t y = panel.bounds.y + (panel.bounds.height - panel.config.icon_size) / 2;

    // Start button (if taskbar mode)
    if (panel.config.mode == PANEL_MODE_TASKBAR && panel.config.show_start_button) {
        graphics_rect_t start_btn = {x, panel.bounds.y + 4, 60, panel.bounds.height - 8};

        graphics_color_t btn_bg = start_menu_state.visible ? theme->panel_active : theme->panel_hover;
        if (!start_menu_state.visible) {
            btn_bg = theme->panel_background;
        }

        dks_fill_rounded_rect(surface, &start_btn, btn_bg, 4);
        dks_draw_text_centered(surface, &start_btn, "Start", theme->panel_text);

        // Store bounds for hit testing
        // (Would store in a dedicated start button item)

        x += 64;
    }

    // Separator after start
    if (panel.config.mode == PANEL_MODE_TASKBAR) {
        dks_draw_vline(surface, x, panel.bounds.y + 6, panel.bounds.height - 12, theme->panel_border);
        x += 8;
    }

    // Render items
    panel_item_t* item = panel.items;
    while (item) {
        if (!item->visible) {
            item = item->next;
            continue;
        }

        switch (item->type) {
            case PANEL_ITEM_SEPARATOR:
                dks_draw_vline(surface, x + 4, panel.bounds.y + 6, panel.bounds.height - 12, theme->panel_border);
                item->bounds.x = x;
                item->bounds.y = panel.bounds.y;
                item->bounds.width = 12;
                item->bounds.height = panel.bounds.height;
                x += 12;
                break;

            case PANEL_ITEM_APP_LAUNCHER: {
                uint32_t btn_size = panel.config.icon_size + 8;
                item->bounds.x = x;
                item->bounds.y = panel.bounds.y + (panel.bounds.height - btn_size) / 2;
                item->bounds.width = btn_size;
                item->bounds.height = btn_size;

                if (item->hovered || item->pressed) {
                    graphics_color_t bg = item->pressed ? theme->panel_active : theme->panel_hover;
                    dks_fill_rounded_rect(surface, &item->bounds, bg, 4);
                }

                if (item->icon) {
                    dks_draw_icon_centered(surface, &item->bounds, item->icon, panel.config.icon_size);
                }

                x += btn_size + panel.config.icon_spacing;
                break;
            }

            case PANEL_ITEM_WINDOW_BUTTON: {
                uint32_t btn_width = 120;
                uint32_t btn_height = panel.bounds.height - 8;
                item->bounds.x = x;
                item->bounds.y = panel.bounds.y + 4;
                item->bounds.width = btn_width;
                item->bounds.height = btn_height;

                graphics_color_t bg = theme->panel_background;
                if (item->active) {
                    bg = theme->panel_active;
                } else if (item->hovered) {
                    bg = theme->panel_hover;
                }

                dks_fill_rounded_rect(surface, &item->bounds, bg, 4);

                // Draw icon
                if (item->icon) {
                    dks_draw_icon(surface, item->icon, x + 4, item->bounds.y + (btn_height - 16) / 2, 16);
                }

                // Draw title (truncated)
                graphics_rect_t text_bounds = {x + 24, item->bounds.y, btn_width - 28, btn_height};
                dks_draw_text_clipped(surface, &text_bounds, item->label, theme->panel_text, ALIGN_START);

                x += btn_width + 2;
                break;
            }

            default:
                break;
        }

        item = item->next;
    }

    // Right side: System tray and clock
    int32_t right_x = panel.bounds.x + panel.bounds.width - 8;

    // Clock
    if (panel.config.show_clock) {
        char time_str[16];
        dks_panel_get_time_string(time_str, sizeof(time_str), panel.config.show_seconds);
        uint32_t clock_width = strlen(time_str) * 8 + 16;

        right_x -= clock_width;
        graphics_rect_t clock_bounds = {right_x, panel.bounds.y + 4, clock_width, panel.bounds.height - 8};
        dks_draw_text_centered(surface, &clock_bounds, time_str, theme->panel_text);
    }

    // System tray
    if (panel.config.show_systray && systray_icon_count > 0) {
        right_x -= 8;  // Spacing

        for (int32_t i = systray_icon_count - 1; i >= 0; i--) {
            right_x -= 20;
            if (systray_icons[i].icon) {
                dks_draw_icon(surface, systray_icons[i].icon, right_x, y, 16);
            }
        }
    }

    panel.dirty = false;
}

void dks_panel_invalidate(void) {
    panel.dirty = true;
}

// Get panel geometry

void dks_panel_get_bounds(graphics_rect_t* bounds) {
    if (bounds) {
        *bounds = panel.bounds;
    }
}

uint32_t dks_panel_get_reserved_space(void) {
    if (!panel_initialized || panel.hidden) return 0;
    return panel.config.size;
}

// Start menu

void dks_start_menu_show(void) {
    if (start_menu_state.visible) return;

    dks_menu_t* menu = dks_get_start_menu();
    if (!menu) return;

    // Position above start button
    int32_t menu_x = panel.bounds.x + 4;
    int32_t menu_y;

    if (panel.config.position == PANEL_POSITION_BOTTOM) {
        const dks_theme_t* theme = dks_theme_get_current();
        dks_menu_calc_size(menu, theme);
        menu_y = panel.bounds.y - menu->height;
    } else {
        menu_y = panel.bounds.y + panel.bounds.height;
    }

    dks_menu_show(menu, menu_x, menu_y);
    start_menu_state.visible = true;
    panel.dirty = true;
}

void dks_start_menu_hide(void) {
    if (!start_menu_state.visible) return;

    dks_menu_t* menu = dks_get_start_menu();
    if (menu) {
        dks_menu_hide(menu);
    }
    start_menu_state.visible = false;
    panel.dirty = true;
}

void dks_start_menu_toggle(void) {
    if (start_menu_state.visible) {
        dks_start_menu_hide();
    } else {
        dks_start_menu_show();
    }
}

bool dks_start_menu_is_visible(void) {
    return start_menu_state.visible;
}
