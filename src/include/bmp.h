#ifndef BMP_H
#define BMP_H

#include "types.h"
#include "graphics/graphics_types.h"

// BMP file header (14 bytes)
typedef struct __attribute__((packed)) {
    uint16 signature;      // "BM" (0x4D42)
    uint32 file_size;      // Size of the BMP file
    uint16 reserved1;      // Reserved
    uint16 reserved2;      // Reserved
    uint32 data_offset;    // Offset to start of image data
} bmp_file_header_t;

// BMP info header (40 bytes for BITMAPINFOHEADER)
typedef struct __attribute__((packed)) {
    uint32 header_size;    // Size of this header (40 bytes)
    int32 width;           // Image width in pixels
    int32 height;          // Image height in pixels (positive = bottom-up, negative = top-down)
    uint16 planes;         // Number of color planes (must be 1)
    uint16 bits_per_pixel; // Bits per pixel (1, 4, 8, 16, 24, 32)
    uint32 compression;    // Compression method (0 = none)
    uint32 image_size;     // Image size in bytes (0 for uncompressed)
    int32 x_resolution;    // Horizontal resolution (pixels per meter)
    int32 y_resolution;    // Vertical resolution (pixels per meter)
    uint32 colors_used;    // Number of colors in palette (0 = all)
    uint32 important_colors; // Number of important colors (0 = all)
} bmp_info_header_t;

// BMP image structure
typedef struct {
    uint32 width;
    uint32 height;
    uint32 bpp;
    uint32 row_stride;     // Bytes per row (including padding)
    bool top_down;         // true if height is negative (top-down), false if bottom-up
    bool owns_data;        // true if pixel_data was heap-allocated and must be freed
    uint8 format;          // 0 = BMP, 1 = TGA, 2 = PNG, 255 = unknown
    uint8* pixel_data;     // Raw pixel data (BGR or BGRA format)
    uint32 pixel_data_size;
} bmp_image_t;

// Result codes
typedef enum {
    BMP_SUCCESS = 0,
    BMP_ERROR_INVALID_FILE,
    BMP_ERROR_UNSUPPORTED_FORMAT,
    BMP_ERROR_OUT_OF_MEMORY,
    BMP_ERROR_FILE_NOT_FOUND,
    BMP_ERROR_INVALID_PARAMETER,
    BMP_ERROR_DECODER_UNAVAILABLE
} bmp_result_t;

// Load an image (BMP/TGA/PNG) from the filesystem.
// Returns BMP_SUCCESS on success, error code otherwise.
bmp_result_t bmp_load_from_file(const char* path, bmp_image_t** image);

// Free a loaded BMP image
void bmp_free(bmp_image_t* image);

// Draw a BMP image to the framebuffer at the specified position
// Returns GRAPHICS_SUCCESS on success
graphics_result_t bmp_draw_image(bmp_image_t* image, int32_t x, int32_t y);

// Draw a BMP image with scaling
graphics_result_t bmp_draw_image_scaled(bmp_image_t* image, int32_t x, int32_t y, 
                                        uint32_t target_width, uint32_t target_height);

#endif // BMP_H
