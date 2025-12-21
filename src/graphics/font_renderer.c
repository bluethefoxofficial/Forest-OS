#include "../include/graphics/font_renderer.h"
#include "../include/graphics/graphics_manager.h"
#include "../include/graphics/font8x8.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/debuglog.h"

// Built-in glyph information for 8x8 font
static font_glyph_t builtin_glyphs_8x8_data[2048]; // Support for extended Unicode blocks

// Font renderer state
static struct {
    bool initialized;
    font_t* system_font;
    font_t* builtin_fonts[2]; // 8x8 and 8x16
    uint32_t font_count;
} font_renderer_state = {
    .initialized = false,
    .system_font = NULL,
    .font_count = 0
};

// Helper functions
static graphics_result_t create_builtin_8x8_font(void);
static graphics_result_t render_glyph(font_t* font, font_glyph_t* glyph, 
                                     graphics_surface_t* surface, int32_t x, int32_t y,
                                     const text_style_t* style);
static void apply_text_effects(graphics_surface_t* surface, const graphics_rect_t* bounds,
                              const text_style_t* style);
static uint32_t find_glyph_index(font_t* font, uint32_t codepoint);

graphics_result_t font_renderer_init(void) {
    debuglog(DEBUG_INFO, "Initializing font renderer...\n");
    
    if (font_renderer_state.initialized) {
        debuglog(DEBUG_WARN, "Font renderer already initialized\n");
        return GRAPHICS_SUCCESS;
    }
    
    // Initialize built-in glyph data for 8x8 font using font8x8 system
    uint32_t glyph_index = 0;
    for (uint32_t block_idx = 0; block_idx < font8x8_get_block_count(); block_idx++) {
        const font8x8_block_t* block = font8x8_get_block_info(block_idx);
        if (!block || glyph_index >= 2048) break;
        
        for (uint32_t char_idx = 0; char_idx < block->count && glyph_index < 2048; char_idx++) {
            uint32_t codepoint = block->unicode_start + char_idx;
            builtin_glyphs_8x8_data[glyph_index].codepoint = codepoint;
            builtin_glyphs_8x8_data[glyph_index].width = 8;
            builtin_glyphs_8x8_data[glyph_index].height = 8;
            builtin_glyphs_8x8_data[glyph_index].bearing_x = 0;
            builtin_glyphs_8x8_data[glyph_index].bearing_y = 8;
            builtin_glyphs_8x8_data[glyph_index].advance = 8;
            // Get pointer to the 8-byte bitmap array for this character
            builtin_glyphs_8x8_data[glyph_index].bitmap = (uint8_t*)&(block->data[char_idx][0]);
            glyph_index++;
        }
    }
    
    // Create built-in 8x8 font
    graphics_result_t result = create_builtin_8x8_font();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_ERROR, "Failed to create built-in 8x8 font\n");
        return result;
    }
    
    font_renderer_state.initialized = true;
    debuglog(DEBUG_INFO, "Font renderer initialized successfully\n");
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t font_renderer_shutdown(void) {
    if (!font_renderer_state.initialized) {
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_INFO, "Shutting down font renderer...\n");
    
    // Unload built-in fonts
    for (int i = 0; i < 2; i++) {
        if (font_renderer_state.builtin_fonts[i]) {
            font_unload(font_renderer_state.builtin_fonts[i]);
            font_renderer_state.builtin_fonts[i] = NULL;
        }
    }
    
    font_renderer_state.system_font = NULL;
    font_renderer_state.initialized = false;
    
    debuglog(DEBUG_INFO, "Font renderer shutdown complete\n");
    return GRAPHICS_SUCCESS;
}

bool font_renderer_is_initialized(void) {
    return font_renderer_state.initialized;
}

graphics_result_t font_load_builtin(const char* name, uint8_t size, font_t** font) {
    if (!name || !font || !font_renderer_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Check for 8x8 font
    if (strcmp(name, "system-8x8") == 0 && size == 8) {
        *font = font_renderer_state.builtin_fonts[0];
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_WARN, "Built-in font '%s' size %u not found\n", name, size);
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t font_unload(font_t* font) {
    if (!font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Don't unload built-in fonts, just return success
    if (font == font_renderer_state.builtin_fonts[0] || 
        font == font_renderer_state.builtin_fonts[1]) {
        return GRAPHICS_SUCCESS;
    }
    
    // Free font resources
    if (font->glyphs) {
        kfree(font->glyphs);
    }
    if (font->codepoint_map) {
        kfree(font->codepoint_map);
    }
    if (font->advance_table) {
        kfree(font->advance_table);
    }
    if (font->font_data) {
        kfree(font->font_data);
    }
    
    kfree(font);
    return GRAPHICS_SUCCESS;
}

graphics_result_t font_measure_text(font_t* font, const char* text, 
                                   uint32_t* width, uint32_t* height) {
    if (!font || !text || !width || !height) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    *width = 0;
    *height = font->metrics.height;
    
    uint32_t current_width = 0;
    uint32_t max_width = 0;
    uint32_t line_count = 1;
    
    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            if (current_width > max_width) {
                max_width = current_width;
            }
            current_width = 0;
            line_count++;
            p++;
            continue;
        }
        
        uint32_t bytes_consumed;
        uint32_t codepoint = utf8_decode(p, &bytes_consumed);
        
        if (font->is_fixed_width) {
            current_width += font->fixed_width;
        } else {
            // Find glyph and get advance
            uint32_t glyph_index = find_glyph_index(font, codepoint);
            if (glyph_index < font->num_glyphs) {
                current_width += font->glyphs[glyph_index].advance;
            } else {
                current_width += 8; // Default advance
            }
        }
        
        p += bytes_consumed;
    }
    
    if (current_width > max_width) {
        max_width = current_width;
    }
    
    *width = max_width;
    *height = line_count * font->metrics.height;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t font_render_text(font_t* font, graphics_surface_t* surface,
                                  int32_t x, int32_t y, const char* text,
                                  const text_style_t* style) {
    if (!font || !surface || !text) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    text_style_t default_style = DEFAULT_TEXT_STYLE;
    if (!style) {
        style = &default_style;
    }
    
    int32_t current_x = x;
    int32_t current_y = y;
    
    const char* p = text;
    while (*p) {
        if (*p == '\n') {
            current_x = x;
            current_y += font->metrics.height;
            p++;
            continue;
        }
        
        if (*p == '\r') {
            current_x = x;
            p++;
            continue;
        }
        
        if (*p == '\t') {
            // Tab to next 4-character boundary
            uint32_t tab_width = font->is_fixed_width ? font->fixed_width * 4 : 32;
            current_x = ((current_x - x + tab_width) / tab_width) * tab_width + x;
            p++;
            continue;
        }
        
        uint32_t bytes_consumed;
        uint32_t codepoint = utf8_decode(p, &bytes_consumed);
        
        graphics_result_t result = font_render_char(font, surface, current_x, current_y, 
                                                   codepoint, style);
        if (result != GRAPHICS_SUCCESS) {
            return result;
        }
        
        // Advance cursor
        if (font->is_fixed_width) {
            current_x += font->fixed_width;
        } else {
            uint32_t glyph_index = find_glyph_index(font, codepoint);
            if (glyph_index < font->num_glyphs) {
                current_x += font->glyphs[glyph_index].advance;
            } else {
                current_x += 8; // Default advance
            }
        }
        
        p += bytes_consumed;
    }
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t font_render_char(font_t* font, graphics_surface_t* surface,
                                  int32_t x, int32_t y, uint32_t codepoint,
                                  const text_style_t* style) {
    if (!font || !surface) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    text_style_t default_style = DEFAULT_TEXT_STYLE;
    if (!style) {
        style = &default_style;
    }
    
    // Find glyph for codepoint
    uint32_t glyph_index = find_glyph_index(font, codepoint);
    if (glyph_index >= font->num_glyphs) {
        // Use space character as fallback
        glyph_index = find_glyph_index(font, ' ');
        if (glyph_index >= font->num_glyphs) {
            return GRAPHICS_ERROR_NOT_SUPPORTED;
        }
    }
    
    
    font_glyph_t* glyph = &font->glyphs[glyph_index];
    return render_glyph(font, glyph, surface, x, y, style);
}

graphics_result_t font_get_system_font(font_t** font) {
    if (!font || !font_renderer_state.initialized) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!font_renderer_state.system_font) {
        // Default to 8x8 font if no system font set
        font_renderer_state.system_font = font_renderer_state.builtin_fonts[0];
    }
    
    *font = font_renderer_state.system_font;
    return GRAPHICS_SUCCESS;
}

graphics_result_t font_set_system_font(font_t* font) {
    if (!font) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    font_renderer_state.system_font = font;
    return GRAPHICS_SUCCESS;
}

// Helper function implementations
static graphics_result_t create_builtin_8x8_font(void) {
    font_t* font = kmalloc(sizeof(font_t));
    if (!font) {
        return GRAPHICS_ERROR_OUT_OF_MEMORY;
    }
    
    memset(font, 0, sizeof(font_t));
    
    strcpy(font->name, "system-8x8");
    font->size = 8;
    font->type = FONT_TYPE_BITMAP;
    font->metrics.ascent = 6;
    font->metrics.descent = 2;
    font->metrics.line_gap = 0;
    font->metrics.max_advance = 8;
    font->metrics.height = 8;
    font->num_glyphs = 512; // Conservative upper bound
    font->glyphs = builtin_glyphs_8x8_data;
    font->is_fixed_width = true;
    font->fixed_width = 8;
    
    font_renderer_state.builtin_fonts[0] = font;
    font_renderer_state.system_font = font;
    font_renderer_state.font_count++;
    
    debuglog(DEBUG_INFO, "Created built-in 8x8 font\n");
    return GRAPHICS_SUCCESS;
}

static graphics_result_t render_glyph(font_t* font, font_glyph_t* glyph, 
                                     graphics_surface_t* surface, int32_t x, int32_t y,
                                     const text_style_t* style) {
    if (!glyph || !glyph->bitmap || !surface || !surface->pixels) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Calculate glyph position - for 8x8 bitmap font, use direct positioning
    int32_t glyph_x = x;
    int32_t glyph_y = y;
    
    // Convert colors to pixel format
    uint32_t fg_pixel = graphics_color_to_pixel(style->foreground, surface->format);
    uint32_t bg_pixel = graphics_color_to_pixel(style->background, surface->format);
    
    // Render bitmap glyph (8x8 for built-in font)
    for (int dy = 0; dy < glyph->height; dy++) {
        if (glyph_y + dy < 0 || glyph_y + dy >= (int32_t)surface->height) {
            continue;
        }
        
        // Safety check for bitmap access
        if (!glyph->bitmap || dy >= glyph->height) {
            continue;
        }
        
        for (int dx = 0; dx < glyph->width; dx++) {
            if (glyph_x + dx < 0 || glyph_x + dx >= (int32_t)surface->width) {
                continue;
            }
            
            // Extract pixel from bitmap data  
            uint8_t row = glyph->bitmap[dy];
            // Fix horizontal flipping: check bits from LSB (right) to MSB (left)
            bool pixel_set = (row & (0x01 << dx)) != 0;
            
            if (pixel_set || style->has_background) {
                uint32_t pixel_value = pixel_set ? fg_pixel : bg_pixel;
                
                // Write pixel based on surface format
                uint32_t pixel_offset;
                switch (surface->format) {
                    case PIXEL_FORMAT_RGB_565:
                    case PIXEL_FORMAT_RGB_555: {
                        pixel_offset = (glyph_y + dy) * surface->pitch + (glyph_x + dx) * 2;
                        uint16_t* pixel_ptr = (uint16_t*)((uint8_t*)surface->pixels + pixel_offset);
                        *pixel_ptr = (uint16_t)pixel_value;
                        break;
                    }
                    case PIXEL_FORMAT_RGB_888: {
                        pixel_offset = (glyph_y + dy) * surface->pitch + (glyph_x + dx) * 3;
                        uint8_t* pixel_ptr = (uint8_t*)surface->pixels + pixel_offset;
                        pixel_ptr[0] = pixel_value & 0xFF;
                        pixel_ptr[1] = (pixel_value >> 8) & 0xFF;
                        pixel_ptr[2] = (pixel_value >> 16) & 0xFF;
                        break;
                    }
                    case PIXEL_FORMAT_BGR_888: {
                        pixel_offset = (glyph_y + dy) * surface->pitch + (glyph_x + dx) * 3;
                        uint8_t* pixel_ptr = (uint8_t*)surface->pixels + pixel_offset;
                        pixel_ptr[0] = (pixel_value >> 16) & 0xFF;
                        pixel_ptr[1] = (pixel_value >> 8) & 0xFF;
                        pixel_ptr[2] = pixel_value & 0xFF;
                        break;
                    }
                    case PIXEL_FORMAT_RGBA_8888:
                    case PIXEL_FORMAT_BGRA_8888: {
                        pixel_offset = (glyph_y + dy) * surface->pitch + (glyph_x + dx) * 4;
                        uint32_t* pixel_ptr = (uint32_t*)((uint8_t*)surface->pixels + pixel_offset);
                        *pixel_ptr = pixel_value;
                        break;
                    }
                    default:
                        return GRAPHICS_ERROR_NOT_SUPPORTED;
                }
            }
        }
    }
    
    // Apply text effects
    if (style->underline || style->strikethrough) {
        graphics_rect_t effect_bounds = {
            glyph_x, glyph_y, glyph->width, glyph->height
        };
        apply_text_effects(surface, &effect_bounds, style);
    }
    
    return GRAPHICS_SUCCESS;
}

static void apply_text_effects(graphics_surface_t* surface, const graphics_rect_t* bounds,
                              const text_style_t* style) {
    if (!surface || !bounds || !style) {
        return;
    }
    
    uint32_t fg_pixel = graphics_color_to_pixel(style->foreground, surface->format);
    
    // Draw underline
    if (style->underline) {
        int32_t underline_y = bounds->y + bounds->height - 1;
        if (underline_y >= 0 && underline_y < (int32_t)surface->height) {
            for (uint32_t x = bounds->x; x < bounds->x + bounds->width; x++) {
                if (x < surface->width) {
                    switch (surface->format) {
                        case PIXEL_FORMAT_RGB_565:
                        case PIXEL_FORMAT_RGB_555: {
                            uint32_t offset = underline_y * surface->pitch + x * 2;
                            uint16_t* pixel_ptr = (uint16_t*)((uint8_t*)surface->pixels + offset);
                            *pixel_ptr = (uint16_t)fg_pixel;
                            break;
                        }
                        case PIXEL_FORMAT_RGB_888: {
                            uint32_t offset = underline_y * surface->pitch + x * 3;
                            uint8_t* pixel_ptr = (uint8_t*)surface->pixels + offset;
                            pixel_ptr[0] = fg_pixel & 0xFF;
                            pixel_ptr[1] = (fg_pixel >> 8) & 0xFF;
                            pixel_ptr[2] = (fg_pixel >> 16) & 0xFF;
                            break;
                        }
                        case PIXEL_FORMAT_BGR_888: {
                            uint32_t offset = underline_y * surface->pitch + x * 3;
                            uint8_t* pixel_ptr = (uint8_t*)surface->pixels + offset;
                            pixel_ptr[0] = (fg_pixel >> 16) & 0xFF;
                            pixel_ptr[1] = (fg_pixel >> 8) & 0xFF;
                            pixel_ptr[2] = fg_pixel & 0xFF;
                            break;
                        }
                        case PIXEL_FORMAT_RGBA_8888:
                        case PIXEL_FORMAT_BGRA_8888: {
                            uint32_t offset = underline_y * surface->pitch + x * 4;
                            uint32_t* pixel_ptr = (uint32_t*)((uint8_t*)surface->pixels + offset);
                            *pixel_ptr = fg_pixel;
                            break;
                        }
                        case PIXEL_FORMAT_INDEXED_8: {
                            uint32_t offset = underline_y * surface->pitch + x;
                            uint8_t* pixel_ptr = (uint8_t*)surface->pixels + offset;
                            *pixel_ptr = (uint8_t)fg_pixel;
                            break;
                        }
                        case PIXEL_FORMAT_TEXT_MODE:
                        default:
                            // Skip unsupported formats
                            break;
                    }
                }
            }
        }
    }
    
    // Draw strikethrough
    if (style->strikethrough) {
        int32_t strike_y = bounds->y + bounds->height / 2;
        if (strike_y >= 0 && strike_y < (int32_t)surface->height) {
            for (uint32_t x = bounds->x; x < bounds->x + bounds->width; x++) {
                if (x < surface->width) {
                    switch (surface->format) {
                        case PIXEL_FORMAT_RGB_565:
                        case PIXEL_FORMAT_RGB_555: {
                            uint32_t offset = strike_y * surface->pitch + x * 2;
                            uint16_t* pixel_ptr = (uint16_t*)((uint8_t*)surface->pixels + offset);
                            *pixel_ptr = (uint16_t)fg_pixel;
                            break;
                        }
                        case PIXEL_FORMAT_RGB_888: {
                            uint32_t offset = strike_y * surface->pitch + x * 3;
                            uint8_t* pixel_ptr = (uint8_t*)surface->pixels + offset;
                            pixel_ptr[0] = fg_pixel & 0xFF;
                            pixel_ptr[1] = (fg_pixel >> 8) & 0xFF;
                            pixel_ptr[2] = (fg_pixel >> 16) & 0xFF;
                            break;
                        }
                        case PIXEL_FORMAT_BGR_888: {
                            uint32_t offset = strike_y * surface->pitch + x * 3;
                            uint8_t* pixel_ptr = (uint8_t*)surface->pixels + offset;
                            pixel_ptr[0] = (fg_pixel >> 16) & 0xFF;
                            pixel_ptr[1] = (fg_pixel >> 8) & 0xFF;
                            pixel_ptr[2] = fg_pixel & 0xFF;
                            break;
                        }
                        case PIXEL_FORMAT_RGBA_8888:
                        case PIXEL_FORMAT_BGRA_8888: {
                            uint32_t offset = strike_y * surface->pitch + x * 4;
                            uint32_t* pixel_ptr = (uint32_t*)((uint8_t*)surface->pixels + offset);
                            *pixel_ptr = fg_pixel;
                            break;
                        }
                        case PIXEL_FORMAT_INDEXED_8: {
                            uint32_t offset = strike_y * surface->pitch + x;
                            uint8_t* pixel_ptr = (uint8_t*)surface->pixels + offset;
                            *pixel_ptr = (uint8_t)fg_pixel;
                            break;
                        }
                        case PIXEL_FORMAT_TEXT_MODE:
                        default:
                            // Skip unsupported formats
                            break;
                    }
                }
            }
        }
    }
}

static uint32_t find_glyph_index(font_t* font, uint32_t codepoint) {
    if (!font || !font->glyphs) {
        return 0;
    }
    
    // Always do linear search to find the correct glyph
    for (uint32_t i = 0; i < font->num_glyphs; i++) {
        if (font->glyphs[i].codepoint == codepoint) {
            return i;
        }
    }
    
    // Fallback to space character (codepoint 32) if not found
    for (uint32_t i = 0; i < font->num_glyphs; i++) {
        if (font->glyphs[i].codepoint == 32) {
            return i;
        }
    }
    
    return 0; // Return first glyph (usually space) if not found
}

// UTF-8 utility functions
uint32_t utf8_decode(const char* str, uint32_t* bytes_consumed) {
    if (!str || !bytes_consumed) {
        *bytes_consumed = 0;
        return 0;
    }
    
    uint8_t first = (uint8_t)str[0];
    
    if ((first & 0x80) == 0) {
        // ASCII (0xxxxxxx)
        *bytes_consumed = 1;
        return first;
    } else if ((first & 0xE0) == 0xC0) {
        // 2-byte sequence (110xxxxx 10xxxxxx)
        if ((str[1] & 0xC0) == 0x80) {
            *bytes_consumed = 2;
            return ((first & 0x1F) << 6) | (str[1] & 0x3F);
        }
    } else if ((first & 0xF0) == 0xE0) {
        // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
        if ((str[1] & 0xC0) == 0x80 && (str[2] & 0xC0) == 0x80) {
            *bytes_consumed = 3;
            return ((first & 0x0F) << 12) | ((str[1] & 0x3F) << 6) | (str[2] & 0x3F);
        }
    } else if ((first & 0xF8) == 0xF0) {
        // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if ((str[1] & 0xC0) == 0x80 && (str[2] & 0xC0) == 0x80 && (str[3] & 0xC0) == 0x80) {
            *bytes_consumed = 4;
            return ((first & 0x07) << 18) | ((str[1] & 0x3F) << 12) | 
                   ((str[2] & 0x3F) << 6) | (str[3] & 0x3F);
        }
    }
    
    // Invalid UTF-8, treat as single byte
    *bytes_consumed = 1;
    return first;
}

uint32_t utf8_strlen(const char* str) {
    if (!str) {
        return 0;
    }
    
    uint32_t length = 0;
    const char* p = str;
    
    while (*p) {
        uint32_t bytes_consumed;
        utf8_decode(p, &bytes_consumed);
        p += bytes_consumed;
        length++;
    }
    
    return length;
}