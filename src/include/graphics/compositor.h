#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include "graphics_types.h"
#include "window_manager.h"

// Compositor modes
typedef enum {
    COMPOSITOR_MODE_IMMEDIATE = 0,  // Immediate mode - draw directly
    COMPOSITOR_MODE_RETAINED,       // Retained mode - buffer and composite
    COMPOSITOR_MODE_HYBRID         // Hybrid - optimize based on content
} compositor_mode_t;

// Compositor capabilities
typedef struct {
    bool hardware_acceleration;     // Hardware-accelerated compositing
    bool alpha_blending;           // Alpha blending support
    bool color_correction;         // Color correction support
    bool effects_pipeline;         // Effects pipeline (blur, shadows, etc.)
    uint32_t max_layers;           // Maximum compositing layers
    uint32_t max_windows;          // Maximum windows per frame
} compositor_capabilities_t;

// Layer types for compositing
typedef enum {
    LAYER_TYPE_DESKTOP = 0,        // Desktop background layer
    LAYER_TYPE_WINDOW,             // Regular window layer
    LAYER_TYPE_OVERLAY,            // Overlay layer (tooltips, menus)
    LAYER_TYPE_CURSOR             // Cursor layer (always on top)
} layer_type_t;

// Compositing layer structure
typedef struct compositor_layer {
    layer_type_t type;
    int32_t z_order;
    graphics_rect_t bounds;
    graphics_surface_t* surface;
    float alpha;                   // Alpha transparency (0.0 = transparent, 1.0 = opaque)
    bool visible;
    bool dirty;
    
    // Effects
    bool drop_shadow;
    graphics_color_t shadow_color;
    int32_t shadow_offset_x;
    int32_t shadow_offset_y;
    int32_t shadow_blur_radius;
    
    void* user_data;
    struct compositor_layer* next;
} compositor_layer_t;

// Compositor initialization and shutdown
graphics_result_t compositor_init(void);
graphics_result_t compositor_shutdown(void);
bool compositor_is_initialized(void);

// Compositor configuration
graphics_result_t compositor_set_mode(compositor_mode_t mode);
graphics_result_t compositor_get_mode(compositor_mode_t* mode);
graphics_result_t compositor_get_capabilities(compositor_capabilities_t* caps);

// Layer management
uint32_t compositor_create_layer(layer_type_t type, const graphics_rect_t* bounds);
graphics_result_t compositor_destroy_layer(uint32_t layer_id);
graphics_result_t compositor_set_layer_surface(uint32_t layer_id, graphics_surface_t* surface);
graphics_result_t compositor_set_layer_bounds(uint32_t layer_id, const graphics_rect_t* bounds);
graphics_result_t compositor_set_layer_alpha(uint32_t layer_id, float alpha);
graphics_result_t compositor_set_layer_visible(uint32_t layer_id, bool visible);
graphics_result_t compositor_invalidate_layer(uint32_t layer_id);

// Compositing operations
graphics_result_t compositor_begin_frame(void);
graphics_result_t compositor_end_frame(void);
graphics_result_t compositor_present(void);
graphics_result_t compositor_flush(void);

// Performance and optimization
graphics_result_t compositor_enable_optimization(bool enable);
graphics_result_t compositor_set_refresh_rate(uint32_t hz);
graphics_result_t compositor_get_frame_stats(uint32_t* fps, uint32_t* frame_time_us);

// Effects and post-processing
graphics_result_t compositor_enable_effects(bool enable);
graphics_result_t compositor_set_global_alpha(float alpha);
graphics_result_t compositor_set_color_correction(float gamma, float brightness, float contrast);

// Debug and diagnostics
graphics_result_t compositor_enable_debug_overlay(bool enable);
graphics_result_t compositor_dump_layers(void);
graphics_result_t compositor_get_memory_usage(size_t* total_bytes, size_t* surface_bytes);

// Integration with window manager
graphics_result_t compositor_register_window_callbacks(void);
graphics_result_t compositor_on_window_created(window_t* window);
graphics_result_t compositor_on_window_destroyed(window_t* window);
graphics_result_t compositor_on_window_moved(window_t* window);
graphics_result_t compositor_on_window_resized(window_t* window);
graphics_result_t compositor_on_window_state_changed(window_t* window);

#endif // COMPOSITOR_H