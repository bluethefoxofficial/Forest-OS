#ifndef WIDGET_TYPES_H
#define WIDGET_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include "../graphics/graphics_types.h"

// Forward declarations
typedef struct widget widget_t;
typedef struct widget_event widget_event_t;

// Widget types
typedef enum {
    WIDGET_TYPE_NONE = 0,
    WIDGET_TYPE_CONTAINER,
    WIDGET_TYPE_BUTTON,
    WIDGET_TYPE_LABEL,
    WIDGET_TYPE_TEXT_INPUT,
    WIDGET_TYPE_CHECKBOX,
    WIDGET_TYPE_RADIO,
    WIDGET_TYPE_DROPDOWN,
    WIDGET_TYPE_SLIDER,
    WIDGET_TYPE_PROGRESS,
    WIDGET_TYPE_SCROLL_CONTAINER,
    WIDGET_TYPE_LIST_VIEW,
    WIDGET_TYPE_ICON_VIEW,
    WIDGET_TYPE_MENU_BAR,
    WIDGET_TYPE_MENU_ITEM,
    WIDGET_TYPE_SEPARATOR,
    WIDGET_TYPE_ICON,
    WIDGET_TYPE_IMAGE,
    WIDGET_TYPE_TOOLBAR,
    WIDGET_TYPE_STATUSBAR,
    WIDGET_TYPE_TAB_VIEW,
    WIDGET_TYPE_TREE_VIEW,
    WIDGET_TYPE_CUSTOM
} widget_type_t;

// Widget state flags (can be combined)
typedef enum {
    WIDGET_STATE_NORMAL   = 0,
    WIDGET_STATE_HOVERED  = (1 << 0),
    WIDGET_STATE_PRESSED  = (1 << 1),
    WIDGET_STATE_FOCUSED  = (1 << 2),
    WIDGET_STATE_DISABLED = (1 << 3),
    WIDGET_STATE_CHECKED  = (1 << 4),
    WIDGET_STATE_SELECTED = (1 << 5),
    WIDGET_STATE_EXPANDED = (1 << 6),
    WIDGET_STATE_DRAGGING = (1 << 7)
} widget_state_t;

// Widget event types
typedef enum {
    WIDGET_EVENT_NONE = 0,
    WIDGET_EVENT_MOUSE_ENTER,
    WIDGET_EVENT_MOUSE_LEAVE,
    WIDGET_EVENT_MOUSE_MOVE,
    WIDGET_EVENT_MOUSE_DOWN,
    WIDGET_EVENT_MOUSE_UP,
    WIDGET_EVENT_CLICK,
    WIDGET_EVENT_DOUBLE_CLICK,
    WIDGET_EVENT_RIGHT_CLICK,
    WIDGET_EVENT_MIDDLE_CLICK,
    WIDGET_EVENT_SCROLL,
    WIDGET_EVENT_DRAG_START,
    WIDGET_EVENT_DRAG,
    WIDGET_EVENT_DRAG_END,
    WIDGET_EVENT_FOCUS_GAIN,
    WIDGET_EVENT_FOCUS_LOSE,
    WIDGET_EVENT_KEY_DOWN,
    WIDGET_EVENT_KEY_UP,
    WIDGET_EVENT_TEXT_INPUT,
    WIDGET_EVENT_VALUE_CHANGE,
    WIDGET_EVENT_SELECTION_CHANGE,
    WIDGET_EVENT_RESIZE,
    WIDGET_EVENT_PAINT
} widget_event_type_t;

// Mouse button identifiers
typedef enum {
    MOUSE_BUTTON_LEFT = 0,
    MOUSE_BUTTON_RIGHT = 1,
    MOUSE_BUTTON_MIDDLE = 2,
    MOUSE_BUTTON_X1 = 3,
    MOUSE_BUTTON_X2 = 4
} mouse_button_t;

// Keyboard modifier flags
typedef enum {
    KEY_MOD_NONE  = 0,
    KEY_MOD_SHIFT = (1 << 0),
    KEY_MOD_CTRL  = (1 << 1),
    KEY_MOD_ALT   = (1 << 2),
    KEY_MOD_SUPER = (1 << 3),
    KEY_MOD_CAPS  = (1 << 4),
    KEY_MOD_NUM   = (1 << 5)
} key_modifiers_t;

// Widget event structure
struct widget_event {
    widget_event_type_t type;
    uint32_t timestamp;

    // Mouse event data
    int32_t mouse_x;
    int32_t mouse_y;
    int32_t mouse_dx;
    int32_t mouse_dy;
    mouse_button_t mouse_button;
    int32_t scroll_delta;

    // Keyboard event data
    uint32_t key_code;
    uint32_t scancode;
    char key_char;
    key_modifiers_t modifiers;

    // Generic data pointer
    void* data;

    // Event handling flags
    bool handled;
    bool propagate;
};

// Widget callback types
typedef void (*widget_callback_t)(widget_t* widget, const widget_event_t* event, void* user_data);
typedef void (*widget_paint_callback_t)(widget_t* widget, graphics_surface_t* surface, void* user_data);
typedef bool (*widget_predicate_t)(widget_t* widget, void* user_data);

// Layout types
typedef enum {
    LAYOUT_NONE = 0,
    LAYOUT_HORIZONTAL,
    LAYOUT_VERTICAL,
    LAYOUT_GRID,
    LAYOUT_ABSOLUTE
} layout_type_t;

// Alignment
typedef enum {
    ALIGN_START = 0,
    ALIGN_CENTER,
    ALIGN_END,
    ALIGN_STRETCH
} alignment_t;

// Widget padding/margin structure
typedef struct {
    int32_t top;
    int32_t right;
    int32_t bottom;
    int32_t left;
} widget_spacing_t;

#endif // WIDGET_TYPES_H
