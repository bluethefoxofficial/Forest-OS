/*
 * DKS Menu System
 * Context menus, start menu, and menu bars
 */

#include "../include/dks/dks_menu.h"
#include "../include/dks/dks_draw.h"
#include "../include/dks/dks_core.h"
#include <string.h>

// External memory allocation
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

// Active menus stack (for nested submenus)
#define MAX_ACTIVE_MENUS 8
static dks_menu_t* active_menus[MAX_ACTIVE_MENUS];
static uint32_t active_menu_count = 0;

// Built-in context menus
static dks_menu_t* desktop_context_menu = NULL;
static dks_menu_t* window_context_menu = NULL;
static dks_menu_t* text_context_menu = NULL;
static dks_menu_t* start_menu = NULL;

// Next menu item ID
static uint32_t next_menu_item_id = 1;

// Menu creation and destruction

dks_menu_t* dks_menu_create(void) {
    dks_menu_t* menu = (dks_menu_t*)kmalloc(sizeof(dks_menu_t));
    if (!menu) return NULL;

    memset(menu, 0, sizeof(dks_menu_t));
    menu->hover_index = -1;
    menu->opacity = 1.0f;

    return menu;
}

void dks_menu_destroy(dks_menu_t* menu) {
    if (!menu) return;

    // Free all items
    dks_menu_clear(menu);

    // Remove from active menus if present
    for (uint32_t i = 0; i < active_menu_count; i++) {
        if (active_menus[i] == menu) {
            for (uint32_t j = i; j < active_menu_count - 1; j++) {
                active_menus[j] = active_menus[j + 1];
            }
            active_menu_count--;
            break;
        }
    }

    kfree(menu);
}

// Add items

static dks_menu_item_t* create_menu_item(void) {
    dks_menu_item_t* item = (dks_menu_item_t*)kmalloc(sizeof(dks_menu_item_t));
    if (!item) return NULL;

    memset(item, 0, sizeof(dks_menu_item_t));
    item->id = next_menu_item_id++;
    item->enabled = true;

    return item;
}

static void add_item_to_menu(dks_menu_t* menu, dks_menu_item_t* item) {
    if (!menu || !item) return;

    item->prev = NULL;
    item->next = NULL;

    if (!menu->items) {
        menu->items = item;
    } else {
        dks_menu_item_t* last = menu->items;
        while (last->next) last = last->next;
        last->next = item;
        item->prev = last;
    }
    menu->item_count++;
}

dks_menu_item_t* dks_menu_add_item(dks_menu_t* menu, const char* label, menu_item_callback_t callback, void* data) {
    dks_menu_item_t* item = create_menu_item();
    if (!item) return NULL;

    item->type = MENU_ITEM_NORMAL;
    if (label) strncpy(item->label, label, DKS_MENU_MAX_LABEL - 1);
    item->on_click = callback;
    item->callback_data = data;

    add_item_to_menu(menu, item);
    return item;
}

dks_menu_item_t* dks_menu_add_item_with_icon(dks_menu_t* menu, const char* label, bmp_image_t* icon, menu_item_callback_t callback, void* data) {
    dks_menu_item_t* item = dks_menu_add_item(menu, label, callback, data);
    if (item) {
        item->icon = icon;
    }
    return item;
}

dks_menu_item_t* dks_menu_add_item_with_shortcut(dks_menu_t* menu, const char* label, const char* shortcut, menu_item_callback_t callback, void* data) {
    dks_menu_item_t* item = dks_menu_add_item(menu, label, callback, data);
    if (item && shortcut) {
        strncpy(item->shortcut, shortcut, DKS_MENU_MAX_SHORTCUT - 1);
    }
    return item;
}

dks_menu_item_t* dks_menu_add_checkbox(dks_menu_t* menu, const char* label, bool checked, menu_item_callback_t callback, void* data) {
    dks_menu_item_t* item = dks_menu_add_item(menu, label, callback, data);
    if (item) {
        item->type = MENU_ITEM_CHECKBOX;
        item->checked = checked;
    }
    return item;
}

dks_menu_item_t* dks_menu_add_radio(dks_menu_t* menu, const char* label, uint32_t group, bool selected, menu_item_callback_t callback, void* data) {
    dks_menu_item_t* item = dks_menu_add_item(menu, label, callback, data);
    if (item) {
        item->type = MENU_ITEM_RADIO;
        item->radio_group = group;
        item->checked = selected;
    }
    return item;
}

dks_menu_item_t* dks_menu_add_separator(dks_menu_t* menu) {
    dks_menu_item_t* item = create_menu_item();
    if (!item) return NULL;

    item->type = MENU_ITEM_SEPARATOR;
    item->enabled = false;

    add_item_to_menu(menu, item);
    return item;
}

dks_menu_item_t* dks_menu_add_submenu(dks_menu_t* menu, const char* label, dks_menu_t* submenu) {
    dks_menu_item_t* item = dks_menu_add_item(menu, label, NULL, NULL);
    if (item) {
        item->type = MENU_ITEM_SUBMENU;
        item->submenu = submenu;
        if (submenu) {
            submenu->parent = menu;
            submenu->parent_item = item;
        }
    }
    return item;
}

// Remove items

void dks_menu_remove_item(dks_menu_t* menu, dks_menu_item_t* item) {
    if (!menu || !item) return;

    if (item->prev) {
        item->prev->next = item->next;
    } else {
        menu->items = item->next;
    }

    if (item->next) {
        item->next->prev = item->prev;
    }

    // Destroy submenu if present
    if (item->submenu) {
        dks_menu_destroy(item->submenu);
    }

    kfree(item);
    menu->item_count--;
}

void dks_menu_clear(dks_menu_t* menu) {
    if (!menu) return;

    dks_menu_item_t* item = menu->items;
    while (item) {
        dks_menu_item_t* next = item->next;
        if (item->submenu) {
            dks_menu_destroy(item->submenu);
        }
        kfree(item);
        item = next;
    }

    menu->items = NULL;
    menu->item_count = 0;
    menu->hover_index = -1;
    menu->hovered_item = NULL;
}

// Item operations

void dks_menu_item_set_enabled(dks_menu_item_t* item, bool enabled) {
    if (item) item->enabled = enabled;
}

void dks_menu_item_set_checked(dks_menu_item_t* item, bool checked) {
    if (item) item->checked = checked;
}

void dks_menu_item_set_label(dks_menu_item_t* item, const char* label) {
    if (item && label) {
        strncpy(item->label, label, DKS_MENU_MAX_LABEL - 1);
    }
}

void dks_menu_item_set_icon(dks_menu_item_t* item, bmp_image_t* icon) {
    if (item) item->icon = icon;
}

// Show/hide menu

void dks_menu_show(dks_menu_t* menu, int32_t x, int32_t y) {
    if (!menu) return;

    // Calculate menu size
    const dks_theme_t* theme = dks_theme_get_current();
    dks_menu_calc_size(menu, theme);

    // Position menu, ensuring it stays on screen
    uint32_t screen_w = dks_get_screen_width();
    uint32_t screen_h = dks_get_screen_height();

    if (x + (int32_t)menu->width > (int32_t)screen_w) {
        x = screen_w - menu->width;
    }
    if (y + (int32_t)menu->height > (int32_t)screen_h) {
        y = screen_h - menu->height;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    menu->x = x;
    menu->y = y;
    menu->visible = true;
    menu->hover_index = -1;
    menu->hovered_item = NULL;

    // Add to active menus
    if (active_menu_count < MAX_ACTIVE_MENUS) {
        active_menus[active_menu_count++] = menu;
    }
}

void dks_menu_show_aligned(dks_menu_t* menu, const graphics_rect_t* anchor, alignment_t h_align, alignment_t v_align) {
    if (!menu || !anchor) return;

    const dks_theme_t* theme = dks_theme_get_current();
    dks_menu_calc_size(menu, theme);

    int32_t x, y;

    // Horizontal alignment
    switch (h_align) {
        case ALIGN_START:
            x = anchor->x;
            break;
        case ALIGN_CENTER:
            x = anchor->x + (anchor->width - menu->width) / 2;
            break;
        case ALIGN_END:
            x = anchor->x + anchor->width - menu->width;
            break;
        default:
            x = anchor->x;
            break;
    }

    // Vertical alignment
    switch (v_align) {
        case ALIGN_START:
            y = anchor->y - menu->height;
            break;
        case ALIGN_CENTER:
            y = anchor->y + (anchor->height - menu->height) / 2;
            break;
        case ALIGN_END:
            y = anchor->y + anchor->height;
            break;
        default:
            y = anchor->y + anchor->height;
            break;
    }

    dks_menu_show(menu, x, y);
}

void dks_menu_hide(dks_menu_t* menu) {
    if (!menu) return;

    // Hide any open submenu first
    if (menu->open_submenu) {
        dks_menu_hide(menu->open_submenu);
        menu->open_submenu = NULL;
    }

    menu->visible = false;
    menu->hover_index = -1;
    menu->hovered_item = NULL;

    // Remove from active menus
    for (uint32_t i = 0; i < active_menu_count; i++) {
        if (active_menus[i] == menu) {
            for (uint32_t j = i; j < active_menu_count - 1; j++) {
                active_menus[j] = active_menus[j + 1];
            }
            active_menu_count--;
            break;
        }
    }
}

void dks_menu_hide_all(void) {
    while (active_menu_count > 0) {
        dks_menu_hide(active_menus[0]);
    }
}

// Menu state

bool dks_menu_is_visible(dks_menu_t* menu) {
    return menu ? menu->visible : false;
}

bool dks_menu_any_visible(void) {
    return active_menu_count > 0;
}

dks_menu_t* dks_menu_get_active(void) {
    return active_menu_count > 0 ? active_menus[active_menu_count - 1] : NULL;
}

// Input handling

bool dks_menu_handle_mouse_move(int32_t x, int32_t y) {
    // Check menus in reverse order (top-most first)
    for (int32_t i = active_menu_count - 1; i >= 0; i--) {
        dks_menu_t* menu = active_menus[i];
        if (!menu->visible) continue;

        dks_menu_item_t* hit = dks_menu_hit_test(menu, x, y);

        // Clear previous hover
        if (menu->hovered_item && menu->hovered_item != hit) {
            menu->hovered_item->hovered = false;
        }

        if (hit) {
            hit->hovered = true;
            menu->hovered_item = hit;

            // Find hover index
            int32_t idx = 0;
            dks_menu_item_t* item = menu->items;
            while (item && item != hit) {
                idx++;
                item = item->next;
            }
            menu->hover_index = idx;

            // Open submenu on hover
            if (hit->type == MENU_ITEM_SUBMENU && hit->submenu && hit->enabled) {
                if (menu->open_submenu != hit->submenu) {
                    if (menu->open_submenu) {
                        dks_menu_hide(menu->open_submenu);
                    }
                    int32_t sub_x = menu->x + menu->width - 2;
                    int32_t sub_y = hit->bounds.y;
                    dks_menu_show(hit->submenu, sub_x, sub_y);
                    menu->open_submenu = hit->submenu;
                }
            }

            return true;
        } else if (dks_menu_contains_point(menu, x, y)) {
            // Inside menu but not on item (e.g., border)
            return true;
        }
    }

    return false;
}

bool dks_menu_handle_mouse_button(int32_t x, int32_t y, uint8_t button, bool pressed) {
    if (!pressed || button != MOUSE_BUTTON_LEFT) return false;

    for (int32_t i = active_menu_count - 1; i >= 0; i--) {
        dks_menu_t* menu = active_menus[i];
        if (!menu->visible) continue;

        dks_menu_item_t* hit = dks_menu_hit_test(menu, x, y);
        if (hit && hit->enabled) {
            switch (hit->type) {
                case MENU_ITEM_NORMAL:
                    if (hit->on_click) {
                        hit->on_click(hit, hit->callback_data);
                    }
                    dks_menu_hide_all();
                    return true;

                case MENU_ITEM_CHECKBOX:
                    hit->checked = !hit->checked;
                    if (hit->on_click) {
                        hit->on_click(hit, hit->callback_data);
                    }
                    // Don't close menu for checkboxes
                    return true;

                case MENU_ITEM_RADIO:
                    // Uncheck other items in same group
                    {
                        dks_menu_item_t* item = menu->items;
                        while (item) {
                            if (item->type == MENU_ITEM_RADIO && item->radio_group == hit->radio_group) {
                                item->checked = false;
                            }
                            item = item->next;
                        }
                    }
                    hit->checked = true;
                    if (hit->on_click) {
                        hit->on_click(hit, hit->callback_data);
                    }
                    return true;

                case MENU_ITEM_SUBMENU:
                    // Submenu handling done in mouse_move
                    return true;

                default:
                    break;
            }
        }

        if (dks_menu_contains_point(menu, x, y)) {
            return true;  // Clicked inside menu but not on item
        }
    }

    return false;
}

bool dks_menu_handle_key(uint32_t keycode, uint32_t modifiers) {
    dks_menu_t* menu = dks_menu_get_active();
    if (!menu) return false;

    (void)modifiers;

    switch (keycode) {
        case 0x48: // Up arrow
            if (menu->hover_index > 0) {
                menu->hover_index--;
                // Skip separators
                int32_t idx = 0;
                dks_menu_item_t* item = menu->items;
                while (item && idx < menu->hover_index) {
                    idx++;
                    item = item->next;
                }
                while (item && item->type == MENU_ITEM_SEPARATOR && menu->hover_index > 0) {
                    menu->hover_index--;
                    item = item->prev;
                }
                if (menu->hovered_item) menu->hovered_item->hovered = false;
                if (item) {
                    item->hovered = true;
                    menu->hovered_item = item;
                }
            }
            return true;

        case 0x50: // Down arrow
            if (menu->hover_index < (int32_t)menu->item_count - 1) {
                menu->hover_index++;
                int32_t idx = 0;
                dks_menu_item_t* item = menu->items;
                while (item && idx < menu->hover_index) {
                    idx++;
                    item = item->next;
                }
                while (item && item->type == MENU_ITEM_SEPARATOR &&
                       menu->hover_index < (int32_t)menu->item_count - 1) {
                    menu->hover_index++;
                    item = item->next;
                }
                if (menu->hovered_item) menu->hovered_item->hovered = false;
                if (item) {
                    item->hovered = true;
                    menu->hovered_item = item;
                }
            }
            return true;

        case 0x1C: // Enter
            if (menu->hovered_item && menu->hovered_item->enabled) {
                if (menu->hovered_item->type == MENU_ITEM_NORMAL) {
                    if (menu->hovered_item->on_click) {
                        menu->hovered_item->on_click(menu->hovered_item, menu->hovered_item->callback_data);
                    }
                    dks_menu_hide_all();
                }
            }
            return true;

        case 0x01: // Escape
            dks_menu_hide_all();
            return true;

        case 0x4D: // Right arrow - open submenu
            if (menu->hovered_item && menu->hovered_item->type == MENU_ITEM_SUBMENU &&
                menu->hovered_item->submenu) {
                dks_menu_show(menu->hovered_item->submenu,
                              menu->x + menu->width - 2,
                              menu->hovered_item->bounds.y);
                menu->open_submenu = menu->hovered_item->submenu;
            }
            return true;

        case 0x4B: // Left arrow - close submenu
            if (menu->parent) {
                dks_menu_hide(menu);
            }
            return true;
    }

    return false;
}

bool dks_menu_contains_point(dks_menu_t* menu, int32_t x, int32_t y) {
    if (!menu || !menu->visible) return false;
    return x >= menu->x && y >= menu->y &&
           x < menu->x + (int32_t)menu->width &&
           y < menu->y + (int32_t)menu->height;
}

dks_menu_item_t* dks_menu_hit_test(dks_menu_t* menu, int32_t x, int32_t y) {
    if (!dks_menu_contains_point(menu, x, y)) return NULL;

    dks_menu_item_t* item = menu->items;
    while (item) {
        if (item->type != MENU_ITEM_SEPARATOR &&
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

void dks_menu_render(dks_menu_t* menu, graphics_surface_t* surface, const dks_theme_t* theme) {
    if (!menu || !menu->visible || !surface) return;

    // Draw shadow
    graphics_rect_t shadow_rect = {menu->x, menu->y, menu->width, menu->height};
    dks_draw_box_shadow(surface, &shadow_rect, theme);

    // Draw background
    graphics_rect_t bg_rect = {menu->x, menu->y, menu->width, menu->height};
    dks_fill_rounded_rect(surface, &bg_rect, theme->menu_background, theme->corner_radius);
    dks_draw_rounded_rect(surface, &bg_rect, theme->menu_border, theme->corner_radius, false);

    // Draw items
    int32_t y = menu->y + 4;
    dks_menu_item_t* item = menu->items;
    while (item) {
        item->bounds.x = menu->x;
        item->bounds.y = y;
        item->bounds.width = menu->width;

        if (item->type == MENU_ITEM_SEPARATOR) {
            item->bounds.height = 8;
            int32_t sep_y = y + 4;
            dks_draw_hline(surface, menu->x + 8, sep_y, menu->width - 16, theme->menu_separator);
        } else {
            item->bounds.height = theme->menu_item_height;

            // Draw hover background
            if (item->hovered && item->enabled) {
                graphics_rect_t hover_rect = {
                    menu->x + 4, y, menu->width - 8, theme->menu_item_height
                };
                dks_fill_rounded_rect(surface, &hover_rect, theme->menu_hover, 4);
            }

            // Draw checkbox/radio
            int32_t text_x = menu->x + 8;
            if (item->type == MENU_ITEM_CHECKBOX || item->type == MENU_ITEM_RADIO) {
                if (item->checked) {
                    graphics_color_t check_color = item->enabled ? theme->primary_color : theme->menu_text_disabled;
                    if (item->type == MENU_ITEM_CHECKBOX) {
                        // Draw checkmark
                        int32_t cx = menu->x + 12;
                        int32_t cy = y + theme->menu_item_height / 2;
                        dks_draw_line(surface, cx - 3, cy, cx, cy + 3, check_color);
                        dks_draw_line(surface, cx, cy + 3, cx + 4, cy - 3, check_color);
                    } else {
                        // Draw radio dot
                        dks_draw_circle(surface, menu->x + 12, y + theme->menu_item_height / 2, 4, check_color, true);
                    }
                }
                text_x += 20;
            }

            // Draw icon if present
            if (item->icon) {
                dks_draw_icon(surface, item->icon, text_x, y + 2, 16);
                text_x += 20;
            }

            // Draw label
            graphics_color_t text_color = item->enabled ?
                (item->hovered ? theme->text_color : theme->menu_text) :
                theme->menu_text_disabled;
            dks_draw_text(surface, text_x, y + (theme->menu_item_height - 16) / 2, item->label, text_color);

            // Draw shortcut or submenu arrow
            if (item->type == MENU_ITEM_SUBMENU) {
                // Draw arrow
                int32_t arrow_x = menu->x + menu->width - 16;
                int32_t arrow_y = y + theme->menu_item_height / 2;
                dks_draw_line(surface, arrow_x, arrow_y - 4, arrow_x + 4, arrow_y, text_color);
                dks_draw_line(surface, arrow_x + 4, arrow_y, arrow_x, arrow_y + 4, text_color);
            } else if (item->shortcut[0]) {
                int32_t shortcut_x = menu->x + menu->width - 8 - strlen(item->shortcut) * 8;
                dks_draw_text(surface, shortcut_x, y + (theme->menu_item_height - 16) / 2,
                              item->shortcut, theme->text_secondary);
            }
        }

        y += item->bounds.height;
        item = item->next;
    }
}

void dks_menu_render_all(graphics_surface_t* surface, const dks_theme_t* theme) {
    for (uint32_t i = 0; i < active_menu_count; i++) {
        dks_menu_render(active_menus[i], surface, theme);
    }
}

void dks_menu_invalidate(dks_menu_t* menu) {
    (void)menu;
    // Menu invalidation could trigger redraw
}

void dks_menu_calc_size(dks_menu_t* menu, const dks_theme_t* theme) {
    if (!menu) return;

    uint32_t max_width = 150;  // Minimum width
    uint32_t height = 8;  // Top/bottom padding

    dks_menu_item_t* item = menu->items;
    while (item) {
        if (item->type == MENU_ITEM_SEPARATOR) {
            height += 8;
        } else {
            height += theme->menu_item_height;

            // Calculate width needed
            uint32_t item_width = 16;  // Left padding
            if (item->type == MENU_ITEM_CHECKBOX || item->type == MENU_ITEM_RADIO) {
                item_width += 20;
            }
            if (item->icon) {
                item_width += 20;
            }
            item_width += strlen(item->label) * 8;
            if (item->shortcut[0]) {
                item_width += 16 + strlen(item->shortcut) * 8;
            }
            if (item->type == MENU_ITEM_SUBMENU) {
                item_width += 20;
            }
            item_width += 16;  // Right padding

            if (item_width > max_width) max_width = item_width;
        }
        item = item->next;
    }

    menu->width = max_width;
    menu->height = height;
}

// Built-in context menus

static void desktop_menu_new_folder(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    // TODO: Create new folder
}

static void desktop_menu_refresh(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    dks_desktop_invalidate();
}

static void desktop_menu_settings(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    // TODO: Open settings
}

dks_menu_t* dks_get_desktop_context_menu(void) {
    if (!desktop_context_menu) {
        desktop_context_menu = dks_menu_create();
        dks_menu_add_item(desktop_context_menu, "New Folder", desktop_menu_new_folder, NULL);
        dks_menu_add_separator(desktop_context_menu);
        dks_menu_add_item(desktop_context_menu, "Refresh", desktop_menu_refresh, NULL);
        dks_menu_add_separator(desktop_context_menu);
        dks_menu_add_item(desktop_context_menu, "Settings", desktop_menu_settings, NULL);
    }
    return desktop_context_menu;
}

static void* window_context_target = NULL;

static void window_menu_minimize(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    if (window_context_target) {
        dks_window_minimize((dks_window_t*)window_context_target);
    }
}

static void window_menu_maximize(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    if (window_context_target) {
        dks_window_t* win = (dks_window_t*)window_context_target;
        if (win->state == DKS_WINDOW_STATE_MAXIMIZED) {
            dks_window_restore(win);
        } else {
            dks_window_maximize(win);
        }
    }
}

static void window_menu_close(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    if (window_context_target) {
        dks_window_close((dks_window_t*)window_context_target);
    }
}

dks_menu_t* dks_get_window_context_menu(void* window) {
    window_context_target = window;

    if (!window_context_menu) {
        window_context_menu = dks_menu_create();
        dks_menu_add_item(window_context_menu, "Minimize", window_menu_minimize, NULL);
        dks_menu_add_item(window_context_menu, "Maximize", window_menu_maximize, NULL);
        dks_menu_add_separator(window_context_menu);
        dks_menu_add_item(window_context_menu, "Close", window_menu_close, NULL);
    }
    return window_context_menu;
}

dks_menu_t* dks_get_file_context_menu(const char* filepath, bool is_directory) {
    // TODO: Implement file context menu
    (void)filepath; (void)is_directory;
    return NULL;
}

static void text_menu_copy(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    // TODO: Copy selected text
}

static void text_menu_paste(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    // TODO: Paste clipboard
}

static void text_menu_select_all(dks_menu_item_t* item, void* data) {
    (void)item; (void)data;
    // TODO: Select all text
}

dks_menu_t* dks_get_text_context_menu(bool has_selection, bool can_paste) {
    if (!text_context_menu) {
        text_context_menu = dks_menu_create();
        dks_menu_add_item_with_shortcut(text_context_menu, "Copy", "Ctrl+C", text_menu_copy, NULL);
        dks_menu_add_item_with_shortcut(text_context_menu, "Paste", "Ctrl+V", text_menu_paste, NULL);
        dks_menu_add_separator(text_context_menu);
        dks_menu_add_item_with_shortcut(text_context_menu, "Select All", "Ctrl+A", text_menu_select_all, NULL);
    }

    // Update enabled states
    dks_menu_item_t* item = text_context_menu->items;
    while (item) {
        if (strcmp(item->label, "Copy") == 0) {
            item->enabled = has_selection;
        } else if (strcmp(item->label, "Paste") == 0) {
            item->enabled = can_paste;
        }
        item = item->next;
    }

    return text_context_menu;
}

// Start menu

dks_menu_t* dks_get_start_menu(void) {
    if (!start_menu) {
        start_menu = dks_menu_create();
        start_menu->type = MENU_TYPE_START;
        dks_start_menu_rebuild();
    }
    return start_menu;
}

void dks_start_menu_add_app(const char* name, const char* app_id, bmp_image_t* icon) {
    if (!start_menu) dks_get_start_menu();
    dks_menu_add_item_with_icon(start_menu, name, icon, NULL, (void*)app_id);
}

void dks_start_menu_add_category(const char* name) {
    if (!start_menu) dks_get_start_menu();
    dks_menu_add_separator(start_menu);
    // Could add a header item type for categories
    (void)name;
}

void dks_start_menu_rebuild(void) {
    if (start_menu) {
        dks_menu_clear(start_menu);
    } else {
        start_menu = dks_menu_create();
        start_menu->type = MENU_TYPE_START;
    }

    // Add default items
    dks_menu_add_item(start_menu, "Files", NULL, NULL);
    dks_menu_add_item(start_menu, "Terminal", NULL, NULL);
    dks_menu_add_item(start_menu, "Settings", NULL, NULL);
    dks_menu_add_separator(start_menu);
    dks_menu_add_item(start_menu, "About", NULL, NULL);
    dks_menu_add_separator(start_menu);
    dks_menu_add_item(start_menu, "Shutdown", NULL, NULL);
}

// Menu bar

dks_menubar_t* dks_menubar_create(void) {
    dks_menubar_t* menubar = (dks_menubar_t*)kmalloc(sizeof(dks_menubar_t));
    if (!menubar) return NULL;

    memset(menubar, 0, sizeof(dks_menubar_t));
    menubar->active_index = -1;
    menubar->visible = true;

    return menubar;
}

void dks_menubar_destroy(dks_menubar_t* menubar) {
    if (!menubar) return;

    for (uint32_t i = 0; i < menubar->count; i++) {
        if (menubar->labels[i]) kfree(menubar->labels[i]);
        // Menus are owned elsewhere, don't destroy them
    }
    if (menubar->menus) kfree(menubar->menus);
    if (menubar->labels) kfree(menubar->labels);
    kfree(menubar);
}

void dks_menubar_add_menu(dks_menubar_t* menubar, const char* label, dks_menu_t* menu) {
    if (!menubar || !label || !menu) return;

    uint32_t new_count = menubar->count + 1;
    dks_menu_t** new_menus = (dks_menu_t**)kmalloc(new_count * sizeof(dks_menu_t*));
    char** new_labels = (char**)kmalloc(new_count * sizeof(char*));

    if (!new_menus || !new_labels) {
        if (new_menus) kfree(new_menus);
        if (new_labels) kfree(new_labels);
        return;
    }

    for (uint32_t i = 0; i < menubar->count; i++) {
        new_menus[i] = menubar->menus[i];
        new_labels[i] = menubar->labels[i];
    }

    new_menus[menubar->count] = menu;
    new_labels[menubar->count] = (char*)kmalloc(strlen(label) + 1);
    if (new_labels[menubar->count]) {
        strcpy(new_labels[menubar->count], label);
    }

    if (menubar->menus) kfree(menubar->menus);
    if (menubar->labels) kfree(menubar->labels);

    menubar->menus = new_menus;
    menubar->labels = new_labels;
    menubar->count = new_count;

    menu->type = MENU_TYPE_MENUBAR;
}

void dks_menubar_render(dks_menubar_t* menubar, graphics_surface_t* surface, int32_t x, int32_t y, uint32_t width, const dks_theme_t* theme) {
    if (!menubar || !menubar->visible) return;

    menubar->bounds.x = x;
    menubar->bounds.y = y;
    menubar->bounds.width = width;
    menubar->bounds.height = 24;

    // Draw background
    graphics_rect_t bg = {x, y, width, 24};
    dks_fill_rect(surface, &bg, theme->surface_color);

    // Draw items
    int32_t item_x = x + 4;
    for (uint32_t i = 0; i < menubar->count; i++) {
        uint32_t item_width = strlen(menubar->labels[i]) * 8 + 16;

        bool active = ((int32_t)i == menubar->active_index);

        if (active) {
            graphics_rect_t item_bg = {item_x, y, item_width, 24};
            dks_fill_rect(surface, &item_bg, theme->panel_hover);
        }

        dks_draw_text(surface, item_x + 8, y + 4, menubar->labels[i], theme->text_color);
        item_x += item_width;
    }

    // Draw separator line
    dks_draw_hline(surface, x, y + 23, width, theme->border_color);
}

bool dks_menubar_handle_mouse(dks_menubar_t* menubar, int32_t x, int32_t y, uint8_t button, bool pressed) {
    if (!menubar || !menubar->visible) return false;

    // Check if in menubar bounds
    if (y < menubar->bounds.y || y >= menubar->bounds.y + (int32_t)menubar->bounds.height) {
        return false;
    }

    // Find which item is hit
    int32_t item_x = menubar->bounds.x + 4;
    for (uint32_t i = 0; i < menubar->count; i++) {
        uint32_t item_width = strlen(menubar->labels[i]) * 8 + 16;

        if (x >= item_x && x < item_x + (int32_t)item_width) {
            if (pressed && button == MOUSE_BUTTON_LEFT) {
                if ((int32_t)i == menubar->active_index) {
                    // Close menu
                    if (menubar->menus[i]) {
                        dks_menu_hide(menubar->menus[i]);
                    }
                    menubar->active_index = -1;
                } else {
                    // Open menu
                    if (menubar->active_index >= 0 && menubar->menus[menubar->active_index]) {
                        dks_menu_hide(menubar->menus[menubar->active_index]);
                    }
                    menubar->active_index = i;
                    if (menubar->menus[i]) {
                        dks_menu_show(menubar->menus[i], item_x, menubar->bounds.y + 24);
                    }
                }
            }
            return true;
        }
        item_x += item_width;
    }

    return false;
}
