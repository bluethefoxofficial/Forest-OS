#ifndef FONT8X8_H
#define FONT8X8_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * 8x8 monochrome bitmap fonts for rendering
 * Author: Daniel Hepper <daniel@hepper.net>
 * 
 * License: Public Domain
 * 
 * Based on:
 * // Summary: font8x8.h
 * // 8x8 monochrome bitmap fonts for rendering
 * //
 * // Author:
 * //     Marcel Sondaar
 * //     International Business Machines (public domain VGA fonts)
 * //
 * // License:
 * //     Public Domain
 **/

// Font8x8 basic latin (U+0000 - U+007F)
extern const char font8x8_basic[128][8];

// Font8x8 block elements (U+2580 - U+259F) 
extern const char font8x8_block[32][8];

// Font8x8 box drawing (U+2500 - U+257F)
extern const char font8x8_box[128][8];

// Font8x8 extended latin (U+00A0 - U+00FF)
extern const char font8x8_ext_latin[96][8];

// Font8x8 greek characters (U+0390 - U+03C9)
extern const char font8x8_greek[58][8];

// Font8x8 hiragana characters (U+3040 - U+309F)
extern const char font8x8_hiragana[96][8];

// Font8x8 miscellaneous characters
extern const char font8x8_misc[10][8];

// Font8x8 control characters (U+0080 - U+009F)
extern const char font8x8_control[32][8];

// Font8x8 SGA alphabet (26 characters)
extern const char font8x8_sga[26][8];

// Font rendering utility functions
typedef struct {
    uint32_t unicode_start;     // Starting Unicode codepoint
    uint32_t unicode_end;       // Ending Unicode codepoint  
    uint32_t count;            // Number of characters in array
    const char (*data)[8];     // Pointer to font data array
    const char* name;          // Human-readable name
} font8x8_block_t;

// Font block registry
extern const font8x8_block_t font8x8_blocks[];
extern const uint32_t font8x8_block_count;

// Utility functions
const char* font8x8_get_glyph(uint32_t codepoint);
bool font8x8_is_supported(uint32_t codepoint);
uint32_t font8x8_get_block_count(void);
const font8x8_block_t* font8x8_get_block_info(uint32_t index);

// Render a single 8x8 character to a buffer
void font8x8_render_char(const char glyph[8], uint8_t* buffer, 
                        uint32_t buffer_width, uint32_t x, uint32_t y,
                        uint32_t fg_color, uint32_t bg_color);

// Render a string using 8x8 font
void font8x8_render_string(const char* str, uint8_t* buffer,
                          uint32_t buffer_width, uint32_t buffer_height,
                          uint32_t x, uint32_t y,
                          uint32_t fg_color, uint32_t bg_color);

#endif // FONT8X8_H
