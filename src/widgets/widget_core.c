/*
 * Widget Core Implementation
 * Event-driven widget system for DKS desktop environment
 */

#include "../include/widgets/widget_core.h"
#include "../include/dks/dks_draw.h"
#include <string.h>
#include <stdlib.h>

// Widget ID counter
static uint32_t next_widget_id = 1;

// Currently focused widget (global)
static widget_t* global_focused_widget = NULL;

// Memory allocation wrappers (using kernel heap)
extern void* kmalloc(size_t size);
extern void kfree(void* ptr);

// Widget creation

widget_t* widget_create(widget_type_t type) {
    widget_t* widget = (widget_t*)kmalloc(sizeof(widget_t));
    if (!widget) return NULL;

    memset(widget, 0, sizeof(widget_t));

    widget->type = type;
    widget->id = next_widget_id++;
    widget->visible = true;
    widget->enabled = true;
    widget->focusable = (type == WIDGET_TYPE_BUTTON ||
                         type == WIDGET_TYPE_TEXT_INPUT ||
                         type == WIDGET_TYPE_CHECKBOX ||
                         type == WIDGET_TYPE_DROPDOWN ||
                         type == WIDGET_TYPE_SLIDER);
    widget->dirty = true;

    // Default colors (will be overridden by theme)
    widget->fg_color = (graphics_color_t){240, 240, 245, 255};
    widget->bg_color = (graphics_color_t){60, 60, 70, 255};

    // Default text alignment
    widget->text_align = ALIGN_CENTER;

    // Allocate type-specific data
    switch (type) {
        case WIDGET_TYPE_BUTTON: {
            widget_button_data_t* data = (widget_button_data_t*)kmalloc(sizeof(widget_button_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_button_data_t));
            }
            widget->type_data = data;
            break;
        }
        case WIDGET_TYPE_TEXT_INPUT: {
            widget_textinput_data_t* data = (widget_textinput_data_t*)kmalloc(sizeof(widget_textinput_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_textinput_data_t));
                data->password_char = '*';
                data->max_length = WIDGET_MAX_TEXT - 1;
            }
            widget->type_data = data;
            break;
        }
        case WIDGET_TYPE_CHECKBOX: {
            widget_checkbox_data_t* data = (widget_checkbox_data_t*)kmalloc(sizeof(widget_checkbox_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_checkbox_data_t));
            }
            widget->type_data = data;
            break;
        }
        case WIDGET_TYPE_DROPDOWN: {
            widget_dropdown_data_t* data = (widget_dropdown_data_t*)kmalloc(sizeof(widget_dropdown_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_dropdown_data_t));
                data->selected_index = -1;
                data->visible_items = 5;
            }
            widget->type_data = data;
            break;
        }
        case WIDGET_TYPE_SLIDER: {
            widget_slider_data_t* data = (widget_slider_data_t*)kmalloc(sizeof(widget_slider_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_slider_data_t));
                data->max_value = 100;
                data->step = 1;
            }
            widget->type_data = data;
            break;
        }
        case WIDGET_TYPE_SCROLL_CONTAINER: {
            widget_scroll_data_t* data = (widget_scroll_data_t*)kmalloc(sizeof(widget_scroll_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_scroll_data_t));
                data->scrollbar_size = 12;
            }
            widget->type_data = data;
            break;
        }
        case WIDGET_TYPE_LIST_VIEW: {
            widget_listview_data_t* data = (widget_listview_data_t*)kmalloc(sizeof(widget_listview_data_t));
            if (data) {
                memset(data, 0, sizeof(widget_listview_data_t));
                data->selected_index = -1;
                data->item_height = 24;
            }
            widget->type_data = data;
            break;
        }
        default:
            widget->type_data = NULL;
            break;
    }

    return widget;
}

widget_t* widget_create_with_bounds(widget_type_t type, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    widget_t* widget = widget_create(type);
    if (widget) {
        widget->x = x;
        widget->y = y;
        widget->width = w;
        widget->height = h;
    }
    return widget;
}

void widget_destroy(widget_t* widget) {
    if (!widget) return;

    // Clear focus if this was focused
    if (global_focused_widget == widget) {
        global_focused_widget = NULL;
    }

    // Free type-specific data
    if (widget->type_data) {
        // Special handling for dropdown options
        if (widget->type == WIDGET_TYPE_DROPDOWN) {
            widget_dropdown_data_t* data = (widget_dropdown_data_t*)widget->type_data;
            if (data->options) {
                for (uint32_t i = 0; i < data->option_count; i++) {
                    if (data->options[i]) kfree(data->options[i]);
                }
                kfree(data->options);
            }
        }
        // Special handling for list view items
        else if (widget->type == WIDGET_TYPE_LIST_VIEW) {
            widget_listview_data_t* data = (widget_listview_data_t*)widget->type_data;
            if (data->items) {
                kfree(data->items);
            }
        }
        kfree(widget->type_data);
    }

    kfree(widget);
}

void widget_destroy_recursive(widget_t* widget) {
    if (!widget) return;

    // Destroy all children first
    widget_t* child = widget->first_child;
    while (child) {
        widget_t* next = child->next_sibling;
        widget_destroy_recursive(child);
        child = next;
    }

    widget_destroy(widget);
}

// Property setters

void widget_set_position(widget_t* widget, int32_t x, int32_t y) {
    if (!widget) return;
    widget->x = x;
    widget->y = y;
    widget->dirty = true;
}

void widget_set_size(widget_t* widget, uint32_t width, uint32_t height) {
    if (!widget) return;
    widget->width = width;
    widget->height = height;
    widget->dirty = true;
}

void widget_set_bounds(widget_t* widget, int32_t x, int32_t y, uint32_t w, uint32_t h) {
    if (!widget) return;
    widget->x = x;
    widget->y = y;
    widget->width = w;
    widget->height = h;
    widget->dirty = true;
}

void widget_set_text(widget_t* widget, const char* text) {
    if (!widget) return;
    if (text) {
        strncpy(widget->text, text, WIDGET_MAX_TEXT - 1);
        widget->text[WIDGET_MAX_TEXT - 1] = '\0';
    } else {
        widget->text[0] = '\0';
    }
    widget->dirty = true;
}

void widget_set_icon(widget_t* widget, bmp_image_t* icon) {
    if (!widget) return;
    widget->icon = icon;
    widget->dirty = true;
}

void widget_set_enabled(widget_t* widget, bool enabled) {
    if (!widget) return;
    widget->enabled = enabled;
    if (enabled) {
        widget->state &= ~WIDGET_STATE_DISABLED;
    } else {
        widget->state |= WIDGET_STATE_DISABLED;
    }
    widget->dirty = true;
}

void widget_set_visible(widget_t* widget, bool visible) {
    if (!widget) return;
    widget->visible = visible;
    widget->dirty = true;
}

void widget_set_focusable(widget_t* widget, bool focusable) {
    if (!widget) return;
    widget->focusable = focusable;
}

void widget_set_colors(widget_t* widget, graphics_color_t fg, graphics_color_t bg) {
    if (!widget) return;
    widget->fg_color = fg;
    widget->bg_color = bg;
    widget->dirty = true;
}

void widget_set_user_data(widget_t* widget, void* data) {
    if (!widget) return;
    widget->user_data = data;
}

// Property getters

const char* widget_get_text(widget_t* widget) {
    return widget ? widget->text : NULL;
}

bool widget_is_enabled(widget_t* widget) {
    return widget ? widget->enabled : false;
}

bool widget_is_visible(widget_t* widget) {
    return widget ? widget->visible : false;
}

bool widget_has_state(widget_t* widget, widget_state_t state) {
    return widget ? (widget->state & state) != 0 : false;
}

void* widget_get_user_data(widget_t* widget) {
    return widget ? widget->user_data : NULL;
}

void widget_get_absolute_position(widget_t* widget, int32_t* x, int32_t* y) {
    if (!widget) {
        if (x) *x = 0;
        if (y) *y = 0;
        return;
    }

    int32_t abs_x = widget->x;
    int32_t abs_y = widget->y;

    widget_t* parent = widget->parent;
    while (parent) {
        abs_x += parent->x;
        abs_y += parent->y;
        parent = parent->parent;
    }

    if (x) *x = abs_x;
    if (y) *y = abs_y;
}

// State management

void widget_set_state(widget_t* widget, widget_state_t state, bool set) {
    if (!widget) return;
    if (set) {
        widget->state |= state;
    } else {
        widget->state &= ~state;
    }
    widget->dirty = true;
}

void widget_add_state(widget_t* widget, widget_state_t state) {
    widget_set_state(widget, state, true);
}

void widget_remove_state(widget_t* widget, widget_state_t state) {
    widget_set_state(widget, state, false);
}

void widget_toggle_state(widget_t* widget, widget_state_t state) {
    if (!widget) return;
    widget->state ^= state;
    widget->dirty = true;
}

// Hierarchy management

void widget_add_child(widget_t* parent, widget_t* child) {
    if (!parent || !child) return;

    // Remove from previous parent if any
    if (child->parent) {
        widget_remove_from_parent(child);
    }

    child->parent = parent;
    child->next_sibling = NULL;
    child->prev_sibling = parent->last_child;

    if (parent->last_child) {
        parent->last_child->next_sibling = child;
    } else {
        parent->first_child = child;
    }
    parent->last_child = child;
    parent->child_count++;
    parent->dirty = true;
}

void widget_remove_child(widget_t* parent, widget_t* child) {
    if (!parent || !child || child->parent != parent) return;

    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        parent->last_child = child->prev_sibling;
    }

    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;
    parent->child_count--;
    parent->dirty = true;
}

void widget_insert_child(widget_t* parent, widget_t* child, uint32_t index) {
    if (!parent || !child) return;

    if (index >= parent->child_count) {
        widget_add_child(parent, child);
        return;
    }

    // Remove from previous parent
    if (child->parent) {
        widget_remove_from_parent(child);
    }

    // Find the widget at the index
    widget_t* at = parent->first_child;
    for (uint32_t i = 0; i < index && at; i++) {
        at = at->next_sibling;
    }

    if (at) {
        child->parent = parent;
        child->next_sibling = at;
        child->prev_sibling = at->prev_sibling;

        if (at->prev_sibling) {
            at->prev_sibling->next_sibling = child;
        } else {
            parent->first_child = child;
        }
        at->prev_sibling = child;
        parent->child_count++;
    } else {
        widget_add_child(parent, child);
    }
    parent->dirty = true;
}

void widget_remove_from_parent(widget_t* widget) {
    if (!widget || !widget->parent) return;
    widget_remove_child(widget->parent, widget);
}

widget_t* widget_get_child(widget_t* parent, uint32_t index) {
    if (!parent || index >= parent->child_count) return NULL;

    widget_t* child = parent->first_child;
    for (uint32_t i = 0; i < index && child; i++) {
        child = child->next_sibling;
    }
    return child;
}

widget_t* widget_find_by_id(widget_t* root, uint32_t id) {
    if (!root) return NULL;
    if (root->id == id) return root;

    widget_t* child = root->first_child;
    while (child) {
        widget_t* found = widget_find_by_id(child, id);
        if (found) return found;
        child = child->next_sibling;
    }
    return NULL;
}

widget_t* widget_find_by_name(widget_t* root, const char* name) {
    if (!root || !name) return NULL;
    if (strcmp(root->name, name) == 0) return root;

    widget_t* child = root->first_child;
    while (child) {
        widget_t* found = widget_find_by_name(child, name);
        if (found) return found;
        child = child->next_sibling;
    }
    return NULL;
}

// Event handling

bool widget_dispatch_event(widget_t* widget, widget_event_t* event) {
    if (!widget || !event || !widget->visible || !widget->enabled) return false;

    event->handled = false;
    event->propagate = true;

    // Handle event based on type
    switch (event->type) {
        case WIDGET_EVENT_MOUSE_ENTER:
            widget_add_state(widget, WIDGET_STATE_HOVERED);
            if (widget->on_mouse_enter) {
                widget->on_mouse_enter(widget, event, widget->callback_data);
            }
            break;

        case WIDGET_EVENT_MOUSE_LEAVE:
            widget_remove_state(widget, WIDGET_STATE_HOVERED);
            widget_remove_state(widget, WIDGET_STATE_PRESSED);
            if (widget->on_mouse_leave) {
                widget->on_mouse_leave(widget, event, widget->callback_data);
            }
            break;

        case WIDGET_EVENT_MOUSE_DOWN:
            widget_add_state(widget, WIDGET_STATE_PRESSED);
            break;

        case WIDGET_EVENT_MOUSE_UP:
            widget_remove_state(widget, WIDGET_STATE_PRESSED);
            break;

        case WIDGET_EVENT_CLICK:
            if (widget->on_click) {
                widget->on_click(widget, event, widget->callback_data);
                event->handled = true;
            }
            // Handle checkbox toggle
            if (widget->type == WIDGET_TYPE_CHECKBOX) {
                checkbox_toggle(widget);
                if (widget->on_change) {
                    widget->on_change(widget, event, widget->callback_data);
                }
                event->handled = true;
            }
            break;

        case WIDGET_EVENT_DOUBLE_CLICK:
            if (widget->on_double_click) {
                widget->on_double_click(widget, event, widget->callback_data);
                event->handled = true;
            }
            break;

        case WIDGET_EVENT_RIGHT_CLICK:
            if (widget->on_right_click) {
                widget->on_right_click(widget, event, widget->callback_data);
                event->handled = true;
            }
            break;

        case WIDGET_EVENT_MIDDLE_CLICK:
            if (widget->on_middle_click) {
                widget->on_middle_click(widget, event, widget->callback_data);
                event->handled = true;
            }
            break;

        case WIDGET_EVENT_FOCUS_GAIN:
            widget_add_state(widget, WIDGET_STATE_FOCUSED);
            if (widget->on_focus) {
                widget->on_focus(widget, event, widget->callback_data);
            }
            break;

        case WIDGET_EVENT_FOCUS_LOSE:
            widget_remove_state(widget, WIDGET_STATE_FOCUSED);
            if (widget->on_blur) {
                widget->on_blur(widget, event, widget->callback_data);
            }
            break;

        case WIDGET_EVENT_KEY_DOWN:
            if (widget->on_key_down) {
                widget->on_key_down(widget, event, widget->callback_data);
            }
            // Handle text input for text fields
            if (widget->type == WIDGET_TYPE_TEXT_INPUT && event->key_char) {
                textinput_insert_char(widget, event->key_char);
                event->handled = true;
            }
            break;

        case WIDGET_EVENT_KEY_UP:
            if (widget->on_key_up) {
                widget->on_key_up(widget, event, widget->callback_data);
            }
            break;

        case WIDGET_EVENT_SCROLL:
            if (widget->on_scroll) {
                widget->on_scroll(widget, event, widget->callback_data);
                event->handled = true;
            }
            break;

        case WIDGET_EVENT_VALUE_CHANGE:
            if (widget->on_change) {
                widget->on_change(widget, event, widget->callback_data);
                event->handled = true;
            }
            break;

        default:
            break;
    }

    widget->dirty = true;
    return event->handled;
}

void widget_set_callback(widget_t* widget, widget_event_type_t event_type, widget_callback_t callback, void* data) {
    if (!widget) return;

    widget->callback_data = data;

    switch (event_type) {
        case WIDGET_EVENT_CLICK:
            widget->on_click = callback;
            break;
        case WIDGET_EVENT_DOUBLE_CLICK:
            widget->on_double_click = callback;
            break;
        case WIDGET_EVENT_RIGHT_CLICK:
            widget->on_right_click = callback;
            break;
        case WIDGET_EVENT_MIDDLE_CLICK:
            widget->on_middle_click = callback;
            break;
        case WIDGET_EVENT_MOUSE_ENTER:
            widget->on_mouse_enter = callback;
            break;
        case WIDGET_EVENT_MOUSE_LEAVE:
            widget->on_mouse_leave = callback;
            break;
        case WIDGET_EVENT_MOUSE_MOVE:
            widget->on_mouse_move = callback;
            break;
        case WIDGET_EVENT_FOCUS_GAIN:
            widget->on_focus = callback;
            break;
        case WIDGET_EVENT_FOCUS_LOSE:
            widget->on_blur = callback;
            break;
        case WIDGET_EVENT_KEY_DOWN:
            widget->on_key_down = callback;
            break;
        case WIDGET_EVENT_KEY_UP:
            widget->on_key_up = callback;
            break;
        case WIDGET_EVENT_VALUE_CHANGE:
        case WIDGET_EVENT_SELECTION_CHANGE:
            widget->on_change = callback;
            break;
        case WIDGET_EVENT_SCROLL:
            widget->on_scroll = callback;
            break;
        default:
            break;
    }
}

// Hit testing

widget_t* widget_hit_test(widget_t* root, int32_t x, int32_t y) {
    if (!root || !root->visible) return NULL;

    // Check if point is within this widget
    int32_t abs_x, abs_y;
    widget_get_absolute_position(root, &abs_x, &abs_y);

    if (x < abs_x || y < abs_y ||
        x >= abs_x + (int32_t)root->width ||
        y >= abs_y + (int32_t)root->height) {
        return NULL;
    }

    // Check children in reverse order (front to back)
    widget_t* child = root->last_child;
    while (child) {
        widget_t* hit = widget_hit_test(child, x, y);
        if (hit) return hit;
        child = child->prev_sibling;
    }

    return root;
}

bool widget_contains_point(widget_t* widget, int32_t x, int32_t y) {
    if (!widget) return false;

    int32_t abs_x, abs_y;
    widget_get_absolute_position(widget, &abs_x, &abs_y);

    return x >= abs_x && y >= abs_y &&
           x < abs_x + (int32_t)widget->width &&
           y < abs_y + (int32_t)widget->height;
}

// Layout and rendering

void widget_layout(widget_t* widget) {
    if (!widget) return;

    // Calculate absolute position
    widget->abs_x = widget->x;
    widget->abs_y = widget->y;
    if (widget->parent) {
        widget->abs_x += widget->parent->abs_x;
        widget->abs_y += widget->parent->abs_y;
    }

    // Apply layout to children
    if (widget->layout != LAYOUT_NONE && widget->first_child) {
        int32_t x_offset = widget->padding.left;
        int32_t y_offset = widget->padding.top;

        widget_t* child = widget->first_child;
        while (child) {
            if (child->visible) {
                switch (widget->layout) {
                    case LAYOUT_HORIZONTAL:
                        child->x = x_offset;
                        child->y = widget->padding.top;
                        x_offset += child->width + widget->spacing;
                        break;

                    case LAYOUT_VERTICAL:
                        child->x = widget->padding.left;
                        child->y = y_offset;
                        y_offset += child->height + widget->spacing;
                        break;

                    default:
                        break;
                }
            }
            child = child->next_sibling;
        }
    }

    // Recursively layout children
    widget_t* child = widget->first_child;
    while (child) {
        widget_layout(child);
        child = child->next_sibling;
    }
}

void widget_invalidate(widget_t* widget) {
    if (!widget) return;
    widget->dirty = true;
}

void widget_invalidate_recursive(widget_t* widget) {
    if (!widget) return;
    widget->dirty = true;

    widget_t* child = widget->first_child;
    while (child) {
        widget_invalidate_recursive(child);
        child = child->next_sibling;
    }
}

// Forward declaration for rendering specific widget types
static void paint_button(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_label(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_textinput(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_checkbox(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_dropdown(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_slider(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_container(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);
static void paint_separator(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme);

void widget_paint(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    if (!widget || !widget->visible || !surface) return;

    // Custom paint callback
    if (widget->on_paint) {
        widget->on_paint(widget, surface, widget->callback_data);
        return;
    }

    // Default rendering based on type
    switch (widget->type) {
        case WIDGET_TYPE_BUTTON:
            paint_button(widget, surface, theme);
            break;
        case WIDGET_TYPE_LABEL:
            paint_label(widget, surface, theme);
            break;
        case WIDGET_TYPE_TEXT_INPUT:
            paint_textinput(widget, surface, theme);
            break;
        case WIDGET_TYPE_CHECKBOX:
            paint_checkbox(widget, surface, theme);
            break;
        case WIDGET_TYPE_DROPDOWN:
            paint_dropdown(widget, surface, theme);
            break;
        case WIDGET_TYPE_SLIDER:
            paint_slider(widget, surface, theme);
            break;
        case WIDGET_TYPE_CONTAINER:
        case WIDGET_TYPE_SCROLL_CONTAINER:
            paint_container(widget, surface, theme);
            break;
        case WIDGET_TYPE_SEPARATOR:
            paint_separator(widget, surface, theme);
            break;
        default:
            // Default: just draw background
            {
                graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};
                dks_fill_rect(surface, &rect, widget->bg_color);
            }
            break;
    }

    widget->dirty = false;
}

void widget_paint_recursive(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    if (!widget || !widget->visible) return;

    widget_paint(widget, surface, theme);

    // Paint children
    widget_t* child = widget->first_child;
    while (child) {
        widget_paint_recursive(child, surface, theme);
        child = child->next_sibling;
    }
}

// Specific widget rendering

static void paint_button(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};
    dks_draw_button(surface, &rect, widget->text, widget->state, theme);
}

static void paint_label(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};
    graphics_color_t color = (widget->state & WIDGET_STATE_DISABLED) ? theme->text_secondary : theme->text_color;
    dks_draw_text_clipped(surface, &rect, widget->text, color, widget->text_align);
}

static void paint_textinput(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;
    graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};

    bool focused = (widget->state & WIDGET_STATE_FOCUSED) != 0;
    const char* display_text = widget->text;

    // Show placeholder if empty
    if (!widget->text[0] && data && data->placeholder[0]) {
        display_text = data->placeholder;
    }

    dks_draw_textinput(surface, &rect, display_text,
                       data ? data->cursor_pos : 0, focused, theme);
}

static void paint_checkbox(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    widget_checkbox_data_t* data = (widget_checkbox_data_t*)widget->type_data;
    bool checked = data ? data->checked : false;

    dks_draw_checkbox(surface, widget->abs_x, widget->abs_y, checked, widget->state, theme);

    // Draw label next to checkbox
    if (widget->text[0]) {
        graphics_rect_t label_rect = {
            widget->abs_x + 20,
            widget->abs_y,
            widget->width - 20,
            widget->height
        };
        dks_draw_text_clipped(surface, &label_rect, widget->text, theme->text_color, ALIGN_START);
    }
}

static void paint_dropdown(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    widget_dropdown_data_t* data = (widget_dropdown_data_t*)widget->type_data;
    graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};

    // Draw dropdown button
    graphics_color_t bg = (widget->state & WIDGET_STATE_HOVERED) ? theme->button_hover : theme->button_background;
    dks_fill_rounded_rect(surface, &rect, bg, theme->corner_radius);
    dks_draw_rounded_rect(surface, &rect, theme->button_border, theme->corner_radius, false);

    // Draw selected text
    const char* text = widget->text;
    if (data && data->selected_index >= 0 && (uint32_t)data->selected_index < data->option_count) {
        text = data->options[data->selected_index];
    }
    graphics_rect_t text_rect = {rect.x + 4, rect.y, rect.width - 24, rect.height};
    dks_draw_text_clipped(surface, &text_rect, text, theme->button_text, ALIGN_START);

    // Draw dropdown arrow
    int32_t arrow_x = rect.x + rect.width - 16;
    int32_t arrow_y = rect.y + rect.height / 2;
    dks_draw_line(surface, arrow_x, arrow_y - 2, arrow_x + 4, arrow_y + 2, theme->button_text);
    dks_draw_line(surface, arrow_x + 4, arrow_y + 2, arrow_x + 8, arrow_y - 2, theme->button_text);

    // If expanded, draw options
    if (data && data->expanded) {
        int32_t opt_y = rect.y + rect.height;
        for (uint32_t i = 0; i < data->option_count && i < data->visible_items; i++) {
            graphics_rect_t opt_rect = {rect.x, opt_y, rect.width, theme->menu_item_height};
            graphics_color_t opt_bg = ((int32_t)i == data->selected_index) ? theme->menu_hover : theme->menu_background;
            dks_fill_rect(surface, &opt_rect, opt_bg);
            dks_draw_text_clipped(surface, &opt_rect, data->options[i], theme->menu_text, ALIGN_START);
            opt_y += theme->menu_item_height;
        }
        // Draw border around dropdown
        graphics_rect_t dropdown_rect = {rect.x, rect.y + rect.height, rect.width,
                                         data->option_count * theme->menu_item_height};
        dks_draw_rect(surface, &dropdown_rect, theme->menu_border, false);
    }
}

static void paint_slider(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    widget_slider_data_t* data = (widget_slider_data_t*)widget->type_data;
    if (!data) return;

    graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};

    // Draw track
    int32_t track_height = 4;
    graphics_rect_t track = {
        rect.x,
        rect.y + (rect.height - track_height) / 2,
        rect.width,
        track_height
    };
    dks_fill_rounded_rect(surface, &track, theme->scrollbar_track, 2);

    // Calculate thumb position
    int32_t range = data->max_value - data->min_value;
    int32_t thumb_size = 16;
    int32_t track_width = rect.width - thumb_size;
    int32_t thumb_x = rect.x;
    if (range > 0) {
        thumb_x = rect.x + (data->current_value - data->min_value) * track_width / range;
    }

    // Draw filled portion
    graphics_rect_t filled = {rect.x, track.y, thumb_x - rect.x + thumb_size / 2, track_height};
    dks_fill_rounded_rect(surface, &filled, theme->primary_color, 2);

    // Draw thumb
    graphics_color_t thumb_color = (widget->state & WIDGET_STATE_PRESSED) ? theme->primary_color :
                                   (widget->state & WIDGET_STATE_HOVERED) ? dks_color_lighten(theme->primary_color, 20) :
                                   theme->surface_color;
    dks_draw_circle(surface, thumb_x + thumb_size / 2, rect.y + rect.height / 2, thumb_size / 2, thumb_color, true);
    dks_draw_circle(surface, thumb_x + thumb_size / 2, rect.y + rect.height / 2, thumb_size / 2, theme->primary_color, false);
}

static void paint_container(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    graphics_rect_t rect = {widget->abs_x, widget->abs_y, widget->width, widget->height};
    dks_fill_rect(surface, &rect, widget->bg_color);
}

static void paint_separator(widget_t* widget, graphics_surface_t* surface, const dks_theme_t* theme) {
    if (widget->width > widget->height) {
        // Horizontal
        dks_draw_separator_h(surface, widget->abs_x, widget->abs_y + widget->height / 2, widget->width, theme);
    } else {
        // Vertical
        dks_draw_separator_v(surface, widget->abs_x + widget->width / 2, widget->abs_y, widget->height, theme);
    }
}

// Focus management

void widget_focus(widget_t* widget) {
    if (!widget || !widget->focusable || !widget->enabled) return;

    if (global_focused_widget && global_focused_widget != widget) {
        widget_event_t blur_event = {0};
        blur_event.type = WIDGET_EVENT_FOCUS_LOSE;
        widget_dispatch_event(global_focused_widget, &blur_event);
    }

    global_focused_widget = widget;

    widget_event_t focus_event = {0};
    focus_event.type = WIDGET_EVENT_FOCUS_GAIN;
    widget_dispatch_event(widget, &focus_event);
}

void widget_blur(widget_t* widget) {
    if (!widget || global_focused_widget != widget) return;

    widget_event_t blur_event = {0};
    blur_event.type = WIDGET_EVENT_FOCUS_LOSE;
    widget_dispatch_event(widget, &blur_event);

    global_focused_widget = NULL;
}

widget_t* widget_get_focused(widget_t* root) {
    (void)root;
    return global_focused_widget;
}

widget_t* widget_get_next_focusable(widget_t* current) {
    if (!current) return NULL;

    // Try next sibling first
    widget_t* next = current->next_sibling;
    while (next) {
        if (next->visible && next->enabled && next->focusable) {
            return next;
        }
        // Check children
        if (next->first_child) {
            widget_t* child = next->first_child;
            while (child) {
                if (child->visible && child->enabled && child->focusable) {
                    return child;
                }
                child = child->next_sibling;
            }
        }
        next = next->next_sibling;
    }

    // Try parent's next sibling
    if (current->parent) {
        return widget_get_next_focusable(current->parent);
    }

    return NULL;
}

widget_t* widget_get_prev_focusable(widget_t* current) {
    if (!current) return NULL;

    widget_t* prev = current->prev_sibling;
    while (prev) {
        if (prev->visible && prev->enabled && prev->focusable) {
            return prev;
        }
        prev = prev->prev_sibling;
    }

    if (current->parent && current->parent->focusable) {
        return current->parent;
    }

    return NULL;
}

// Widget creation helpers

widget_t* widget_create_button(const char* text, widget_callback_t on_click, void* data) {
    widget_t* btn = widget_create(WIDGET_TYPE_BUTTON);
    if (btn) {
        widget_set_text(btn, text);
        btn->on_click = on_click;
        btn->callback_data = data;
        btn->width = 80;
        btn->height = 28;
    }
    return btn;
}

widget_t* widget_create_label(const char* text) {
    widget_t* lbl = widget_create(WIDGET_TYPE_LABEL);
    if (lbl) {
        widget_set_text(lbl, text);
        lbl->height = 20;
        lbl->width = strlen(text) * 8 + 8;
    }
    return lbl;
}

widget_t* widget_create_textinput(const char* placeholder) {
    widget_t* input = widget_create(WIDGET_TYPE_TEXT_INPUT);
    if (input && input->type_data) {
        widget_textinput_data_t* data = (widget_textinput_data_t*)input->type_data;
        if (placeholder) {
            strncpy(data->placeholder, placeholder, WIDGET_MAX_TEXT - 1);
        }
        input->width = 150;
        input->height = 24;
    }
    return input;
}

widget_t* widget_create_checkbox(const char* label, bool checked) {
    widget_t* cb = widget_create(WIDGET_TYPE_CHECKBOX);
    if (cb) {
        widget_set_text(cb, label);
        if (cb->type_data) {
            ((widget_checkbox_data_t*)cb->type_data)->checked = checked;
        }
        cb->width = strlen(label) * 8 + 24;
        cb->height = 20;
    }
    return cb;
}

widget_t* widget_create_dropdown(const char** options, uint32_t count) {
    widget_t* dd = widget_create(WIDGET_TYPE_DROPDOWN);
    if (dd && dd->type_data) {
        widget_dropdown_data_t* data = (widget_dropdown_data_t*)dd->type_data;
        data->options = (char**)kmalloc(count * sizeof(char*));
        if (data->options) {
            for (uint32_t i = 0; i < count; i++) {
                data->options[i] = (char*)kmalloc(strlen(options[i]) + 1);
                if (data->options[i]) {
                    strcpy(data->options[i], options[i]);
                }
            }
            data->option_count = count;
            data->selected_index = 0;
        }
        dd->width = 150;
        dd->height = 28;
    }
    return dd;
}

widget_t* widget_create_slider(int32_t min, int32_t max, int32_t value) {
    widget_t* sl = widget_create(WIDGET_TYPE_SLIDER);
    if (sl && sl->type_data) {
        widget_slider_data_t* data = (widget_slider_data_t*)sl->type_data;
        data->min_value = min;
        data->max_value = max;
        data->current_value = value;
        sl->width = 150;
        sl->height = 24;
    }
    return sl;
}

widget_t* widget_create_icon(bmp_image_t* icon, const char* tooltip) {
    widget_t* ic = widget_create(WIDGET_TYPE_ICON);
    if (ic) {
        ic->icon = icon;
        if (tooltip) {
            strncpy(ic->name, tooltip, WIDGET_MAX_NAME - 1);
        }
        ic->width = 32;
        ic->height = 32;
    }
    return ic;
}

// Text input helpers

void textinput_set_text(widget_t* widget, const char* text) {
    if (!widget || widget->type != WIDGET_TYPE_TEXT_INPUT) return;
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;

    widget_set_text(widget, text);
    if (data) {
        data->cursor_pos = strlen(widget->text);
        data->selection_start = data->selection_end = data->cursor_pos;
    }
}

void textinput_insert_char(widget_t* widget, char c) {
    if (!widget || widget->type != WIDGET_TYPE_TEXT_INPUT) return;
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;
    if (!data) return;

    uint32_t len = strlen(widget->text);
    if (len >= data->max_length) return;

    // Handle backspace
    if (c == '\b') {
        textinput_delete_char(widget, false);
        return;
    }

    // Handle delete
    if (c == 127) {
        textinput_delete_char(widget, true);
        return;
    }

    // Insert character at cursor position
    if (c >= 32 && c < 127) {
        for (uint32_t i = len + 1; i > data->cursor_pos; i--) {
            widget->text[i] = widget->text[i - 1];
        }
        widget->text[data->cursor_pos] = c;
        data->cursor_pos++;
        widget->dirty = true;
    }
}

void textinput_delete_char(widget_t* widget, bool forward) {
    if (!widget || widget->type != WIDGET_TYPE_TEXT_INPUT) return;
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;
    if (!data) return;

    uint32_t len = strlen(widget->text);

    if (forward) {
        // Delete character at cursor
        if (data->cursor_pos < len) {
            for (uint32_t i = data->cursor_pos; i < len; i++) {
                widget->text[i] = widget->text[i + 1];
            }
        }
    } else {
        // Backspace
        if (data->cursor_pos > 0) {
            for (uint32_t i = data->cursor_pos - 1; i < len; i++) {
                widget->text[i] = widget->text[i + 1];
            }
            data->cursor_pos--;
        }
    }
    widget->dirty = true;
}

void textinput_move_cursor(widget_t* widget, int32_t delta) {
    if (!widget || widget->type != WIDGET_TYPE_TEXT_INPUT) return;
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;
    if (!data) return;

    int32_t new_pos = (int32_t)data->cursor_pos + delta;
    uint32_t len = strlen(widget->text);

    if (new_pos < 0) new_pos = 0;
    if ((uint32_t)new_pos > len) new_pos = len;

    data->cursor_pos = (uint32_t)new_pos;
    widget->dirty = true;
}

void textinput_select_all(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_TEXT_INPUT) return;
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;
    if (!data) return;

    data->selection_start = 0;
    data->selection_end = strlen(widget->text);
    data->cursor_pos = data->selection_end;
    widget->dirty = true;
}

void textinput_clear_selection(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_TEXT_INPUT) return;
    widget_textinput_data_t* data = (widget_textinput_data_t*)widget->type_data;
    if (!data) return;

    data->selection_start = data->selection_end = data->cursor_pos;
    widget->dirty = true;
}

// Checkbox helpers

bool checkbox_is_checked(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_CHECKBOX) return false;
    widget_checkbox_data_t* data = (widget_checkbox_data_t*)widget->type_data;
    return data ? data->checked : false;
}

void checkbox_set_checked(widget_t* widget, bool checked) {
    if (!widget || widget->type != WIDGET_TYPE_CHECKBOX) return;
    widget_checkbox_data_t* data = (widget_checkbox_data_t*)widget->type_data;
    if (data) {
        data->checked = checked;
        if (checked) {
            widget->state |= WIDGET_STATE_CHECKED;
        } else {
            widget->state &= ~WIDGET_STATE_CHECKED;
        }
        widget->dirty = true;
    }
}

void checkbox_toggle(widget_t* widget) {
    checkbox_set_checked(widget, !checkbox_is_checked(widget));
}

// Dropdown helpers

int32_t dropdown_get_selected(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_DROPDOWN) return -1;
    widget_dropdown_data_t* data = (widget_dropdown_data_t*)widget->type_data;
    return data ? data->selected_index : -1;
}

void dropdown_set_selected(widget_t* widget, int32_t index) {
    if (!widget || widget->type != WIDGET_TYPE_DROPDOWN) return;
    widget_dropdown_data_t* data = (widget_dropdown_data_t*)widget->type_data;
    if (data && index >= 0 && (uint32_t)index < data->option_count) {
        data->selected_index = index;
        widget->dirty = true;
    }
}

void dropdown_add_option(widget_t* widget, const char* option) {
    if (!widget || widget->type != WIDGET_TYPE_DROPDOWN || !option) return;
    widget_dropdown_data_t* data = (widget_dropdown_data_t*)widget->type_data;
    if (!data) return;

    // Reallocate options array
    char** new_options = (char**)kmalloc((data->option_count + 1) * sizeof(char*));
    if (!new_options) return;

    for (uint32_t i = 0; i < data->option_count; i++) {
        new_options[i] = data->options[i];
    }

    new_options[data->option_count] = (char*)kmalloc(strlen(option) + 1);
    if (new_options[data->option_count]) {
        strcpy(new_options[data->option_count], option);
    }

    if (data->options) kfree(data->options);
    data->options = new_options;
    data->option_count++;
    widget->dirty = true;
}

void dropdown_clear_options(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_DROPDOWN) return;
    widget_dropdown_data_t* data = (widget_dropdown_data_t*)widget->type_data;
    if (!data) return;

    for (uint32_t i = 0; i < data->option_count; i++) {
        if (data->options[i]) kfree(data->options[i]);
    }
    if (data->options) kfree(data->options);

    data->options = NULL;
    data->option_count = 0;
    data->selected_index = -1;
    widget->dirty = true;
}

// Slider helpers

int32_t slider_get_value(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_SLIDER) return 0;
    widget_slider_data_t* data = (widget_slider_data_t*)widget->type_data;
    return data ? data->current_value : 0;
}

void slider_set_value(widget_t* widget, int32_t value) {
    if (!widget || widget->type != WIDGET_TYPE_SLIDER) return;
    widget_slider_data_t* data = (widget_slider_data_t*)widget->type_data;
    if (data) {
        if (value < data->min_value) value = data->min_value;
        if (value > data->max_value) value = data->max_value;
        data->current_value = value;
        widget->dirty = true;
    }
}

void slider_set_range(widget_t* widget, int32_t min, int32_t max) {
    if (!widget || widget->type != WIDGET_TYPE_SLIDER) return;
    widget_slider_data_t* data = (widget_slider_data_t*)widget->type_data;
    if (data) {
        data->min_value = min;
        data->max_value = max;
        if (data->current_value < min) data->current_value = min;
        if (data->current_value > max) data->current_value = max;
        widget->dirty = true;
    }
}

// List view helpers

void listview_add_item(widget_t* widget, const char* text, bmp_image_t* icon, void* data) {
    if (!widget || widget->type != WIDGET_TYPE_LIST_VIEW) return;
    widget_listview_data_t* lv = (widget_listview_data_t*)widget->type_data;
    if (!lv) return;

    // Grow capacity if needed
    if (lv->item_count >= lv->item_capacity) {
        uint32_t new_capacity = lv->item_capacity ? lv->item_capacity * 2 : 16;
        list_view_item_t* new_items = (list_view_item_t*)kmalloc(new_capacity * sizeof(list_view_item_t));
        if (!new_items) return;

        if (lv->items) {
            memcpy(new_items, lv->items, lv->item_count * sizeof(list_view_item_t));
            kfree(lv->items);
        }
        lv->items = new_items;
        lv->item_capacity = new_capacity;
    }

    list_view_item_t* item = &lv->items[lv->item_count];
    memset(item, 0, sizeof(list_view_item_t));
    if (text) {
        strncpy(item->text, text, WIDGET_MAX_TEXT - 1);
    }
    item->icon = icon;
    item->data = data;
    item->selected = false;

    lv->item_count++;
    widget->dirty = true;
}

void listview_remove_item(widget_t* widget, uint32_t index) {
    if (!widget || widget->type != WIDGET_TYPE_LIST_VIEW) return;
    widget_listview_data_t* lv = (widget_listview_data_t*)widget->type_data;
    if (!lv || index >= lv->item_count) return;

    for (uint32_t i = index; i < lv->item_count - 1; i++) {
        lv->items[i] = lv->items[i + 1];
    }
    lv->item_count--;

    if (lv->selected_index == (int32_t)index) {
        lv->selected_index = -1;
    } else if (lv->selected_index > (int32_t)index) {
        lv->selected_index--;
    }

    widget->dirty = true;
}

void listview_clear(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_LIST_VIEW) return;
    widget_listview_data_t* lv = (widget_listview_data_t*)widget->type_data;
    if (!lv) return;

    lv->item_count = 0;
    lv->selected_index = -1;
    widget->dirty = true;
}

int32_t listview_get_selected(widget_t* widget) {
    if (!widget || widget->type != WIDGET_TYPE_LIST_VIEW) return -1;
    widget_listview_data_t* lv = (widget_listview_data_t*)widget->type_data;
    return lv ? lv->selected_index : -1;
}

void listview_set_selected(widget_t* widget, int32_t index) {
    if (!widget || widget->type != WIDGET_TYPE_LIST_VIEW) return;
    widget_listview_data_t* lv = (widget_listview_data_t*)widget->type_data;
    if (!lv) return;

    // Clear old selection
    if (lv->selected_index >= 0 && (uint32_t)lv->selected_index < lv->item_count) {
        lv->items[lv->selected_index].selected = false;
    }

    if (index >= 0 && (uint32_t)index < lv->item_count) {
        lv->selected_index = index;
        lv->items[index].selected = true;
    } else {
        lv->selected_index = -1;
    }

    widget->dirty = true;
}
