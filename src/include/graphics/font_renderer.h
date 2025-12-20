#ifndef FONT_RENDERER_H
#define FONT_RENDERER_H

#include "graphics_types.h"

// Font types
typedef enum {
    FONT_TYPE_BITMAP = 0,       // Bitmap/raster font
    FONT_TYPE_VECTOR,           // Vector/outline font (future)
    FONT_TYPE_SYSTEM           // Built-in system font
} font_type_t;

// Font metrics
typedef struct {
    uint8_t ascent;             // Distance from baseline to top of highest glyph
    uint8_t descent;            // Distance from baseline to bottom of lowest glyph
    uint8_t line_gap;           // Space between lines
    uint8_t max_advance;        // Maximum horizontal advance
    uint8_t height;             // Total font height (ascent + descent + line_gap)
} font_metrics_t;

// Extended font structure with more details
struct font {
    char name[32];              // Font name
    uint8_t size;               // Font size in pixels
    font_type_t type;           // Font type
    font_metrics_t metrics;     // Font metrics
    
    uint32_t num_glyphs;        // Number of glyphs in font
    font_glyph_t* glyphs;       // Glyph data
    
    // Lookup tables for performance
    uint32_t* codepoint_map;    // Maps codepoints to glyph indices
    uint8_t* advance_table;     // Pre-computed advance widths
    
    // Font file data (if loaded from file)
    void* font_data;
    size_t data_size;
    
    // Rendering cache
    struct {
        bool enabled;
        uint32_t max_entries;
        void* cache_data;
    } cache;
    
    bool is_fixed_width;        // True if monospace font
    uint8_t fixed_width;        // Width if monospace
};

// Text alignment options
typedef enum {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER,
    TEXT_ALIGN_RIGHT,
    TEXT_ALIGN_JUSTIFY
} text_align_t;

// Text rendering styles
typedef struct {
    graphics_color_t foreground;
    graphics_color_t background;
    bool has_background;        // Whether to draw background
    bool bold;                  // Bold text
    bool italic;                // Italic text (if supported)
    bool underline;             // Underlined text
    bool strikethrough;         // Strikethrough text
    uint8_t shadow_offset;      // Shadow offset (0 = no shadow)
    graphics_color_t shadow_color;
} text_style_t;

// Text layout and measurement
typedef struct {
    uint32_t width;             // Required width in pixels
    uint32_t height;            // Required height in pixels
    uint32_t line_count;        // Number of lines
    uint32_t* line_widths;      // Width of each line
    uint32_t max_line_width;    // Width of widest line
} text_layout_t;

// Font rendering system initialization
graphics_result_t font_renderer_init(void);
graphics_result_t font_renderer_shutdown(void);
bool font_renderer_is_initialized(void);

// Font management
graphics_result_t font_load_builtin(const char* name, uint8_t size, font_t** font);
graphics_result_t font_load_from_memory(const void* data, size_t size, 
                                       const char* name, uint8_t point_size, font_t** font);
graphics_result_t font_load_from_file(const char* filename, 
                                     const char* name, uint8_t point_size, font_t** font);
graphics_result_t font_unload(font_t* font);

// Font enumeration
graphics_result_t font_enumerate_builtin(char*** font_names, uint32_t* count);
graphics_result_t font_get_info(font_t* font, font_metrics_t* metrics);

// Text measurement
graphics_result_t font_measure_text(font_t* font, const char* text, 
                                   uint32_t* width, uint32_t* height);
graphics_result_t font_measure_char(font_t* font, uint32_t codepoint, 
                                   uint32_t* width, uint32_t* height);
graphics_result_t font_layout_text(font_t* font, const char* text, 
                                  uint32_t max_width, text_layout_t** layout);
graphics_result_t font_free_layout(text_layout_t* layout);

// Text rendering to surfaces
graphics_result_t font_render_text(font_t* font, graphics_surface_t* surface,
                                  int32_t x, int32_t y, const char* text,
                                  const text_style_t* style);
graphics_result_t font_render_char(font_t* font, graphics_surface_t* surface,
                                  int32_t x, int32_t y, uint32_t codepoint,
                                  const text_style_t* style);
graphics_result_t font_render_text_aligned(font_t* font, graphics_surface_t* surface,
                                          const graphics_rect_t* bounds, const char* text,
                                          text_align_t alignment, const text_style_t* style);

// Advanced text rendering
graphics_result_t font_render_text_wrapped(font_t* font, graphics_surface_t* surface,
                                          const graphics_rect_t* bounds, const char* text,
                                          const text_style_t* style, uint32_t* lines_rendered);
graphics_result_t font_render_text_with_effects(font_t* font, graphics_surface_t* surface,
                                               int32_t x, int32_t y, const char* text,
                                               const text_style_t* style);

// Glyph management
graphics_result_t font_get_glyph(font_t* font, uint32_t codepoint, font_glyph_t** glyph);
graphics_result_t font_cache_glyph(font_t* font, uint32_t codepoint);
graphics_result_t font_clear_cache(font_t* font);

// UTF-8 support utilities
uint32_t utf8_decode(const char* str, uint32_t* bytes_consumed);
uint32_t utf8_encode(uint32_t codepoint, char* buffer);
uint32_t utf8_strlen(const char* str);
const char* utf8_next_char(const char* str);

// Built-in fonts
extern const uint8_t builtin_font_8x16[];
extern const font_glyph_t builtin_glyphs_8x8[];
extern const font_glyph_t builtin_glyphs_8x16[];

// Default text style
#define DEFAULT_TEXT_STYLE { \
    .foreground = COLOR_WHITE, \
    .background = COLOR_BLACK, \
    .has_background = false, \
    .bold = false, \
    .italic = false, \
    .underline = false, \
    .strikethrough = false, \
    .shadow_offset = 0, \
    .shadow_color = COLOR_BLACK \
}

// System font access
graphics_result_t font_get_system_font(font_t** font);
graphics_result_t font_set_system_font(font_t* font);

// Font file format support (for future expansion)
typedef enum {
    FONT_FORMAT_BDF = 0,        // Adobe Bitmap Distribution Format
    FONT_FORMAT_PCF,            // Portable Compiled Format
    FONT_FORMAT_TTF,            // TrueType Font (future)
    FONT_FORMAT_OTF            // OpenType Font (future)
} font_format_t;

graphics_result_t font_detect_format(const void* data, size_t size, font_format_t* format);

#endif // FONT_RENDERER_H