/*
 * stb_vorbis port for Forest-OS
 *
 * This file configures and includes the stb_vorbis decoder.
 * stb_vorbis is public domain software by Sean Barrett.
 *
 * To use:
 * 1. Download stb_vorbis.c from https://github.com/nothings/stb
 *    or run tools/fetch_stb_vorbis.sh
 * 2. Place it as src/stb_vorbis.c
 * 3. Build the kernel
 */

#include "include/stb_vorbis_port.h"
#include "include/memory.h"
#include "include/libc/string.h"
#include "include/libc/math.h"
#include "include/libc/stdlib.h"
#include "include/screen.h"

// Kernel heap allocators (size-only variant)
extern void* kmalloc(size_t size);
extern void* krealloc(void* ptr, size_t size);
extern void  kfree(void* ptr);

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Disable asserts inside stb_vorbis (uses abort otherwise)
#ifndef NDEBUG
#define NDEBUG 1
#endif

// Configuration must come before including stb_vorbis
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API

// Memory allocation redirection for stb_vorbis
#define malloc(sz)       stb_malloc_wrapper(sz)
#define realloc(p, sz)   stb_realloc_wrapper(p, sz)
#define free(p)          stb_free_wrapper(p)

// Ensure stack allocations use the compiler builtin (no external symbol)
#define alloca __builtin_alloca

static void* stb_malloc_wrapper(size_t sz) {
    return kmalloc(sz);
}

static void* stb_realloc_wrapper(void* p, size_t sz) {
    return krealloc(p, sz);
}

static void stb_free_wrapper(void* p) {
    kfree(p);
}

// qsort implementation for stb_vorbis (simple insertion sort)
static void stb_qsort(void* base, size_t nmemb, size_t size,
                      int (*compar)(const void*, const void*)) {
    if (nmemb < 2 || !base || !compar) return;

    char* arr = (char*)base;
    char* temp = stb_malloc_wrapper(size);
    if (!temp) return;

    for (size_t i = 1; i < nmemb; i++) {
        memcpy(temp, arr + i * size, size);
        size_t j = i;
        while (j > 0 && compar(arr + (j - 1) * size, temp) > 0) {
            memcpy(arr + j * size, arr + (j - 1) * size, size);
            j--;
        }
        memcpy(arr + j * size, temp, size);
    }
    stb_free_wrapper(temp);
}

#define qsort stb_qsort

// Define the implementation
#define STB_VORBIS_IMPLEMENTATION

// Fallback for compilers without __has_include
#ifndef __has_include
#define __has_include(x) 0
#endif

// Check if stb_vorbis.c exists by trying to include it
#if __has_include("stb_vorbis.c")
#ifdef float
#define STB_VORBIS_RESTORE_FLOAT_MACRO 1
#undef float
#endif
#include "stb_vorbis.c"
#ifdef STB_VORBIS_RESTORE_FLOAT_MACRO
#define float double
#undef STB_VORBIS_RESTORE_FLOAT_MACRO
#endif
#else

// Fallback: provide stub implementations if stb_vorbis.c is not present
#warning "stb_vorbis.c not found - OGG support will be disabled"
#warning "Download from: https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c"
#warning "Helper: run tools/fetch_stb_vorbis.sh to download automatically"

#include "include/stb_vorbis_port.h"

stb_vorbis* stb_vorbis_open_memory(const unsigned char *data, int len,
                                    int *error, const stb_vorbis_alloc *alloc_buffer) {
    (void)data; (void)len; (void)alloc_buffer;
    if (error) *error = VORBIS_feature_not_supported;
    print("[OGG] stb_vorbis.c not included - OGG disabled\n");
    return NULL;
}

stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f) {
    (void)f;
    stb_vorbis_info info = {0};
    return info;
}

stb_vorbis_comment stb_vorbis_get_comment(stb_vorbis *f) {
    (void)f;
    stb_vorbis_comment comment = {0};
    return comment;
}

unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f) {
    (void)f;
    return 0;
}

float stb_vorbis_stream_length_in_seconds(stb_vorbis *f) {
    (void)f;
    return -1.0f;
}

void stb_vorbis_close(stb_vorbis *f) {
    (void)f;
}

int stb_vorbis_decode_memory(const unsigned char *mem, int len,
                              int *channels, int *sample_rate,
                              short **output) {
    (void)mem; (void)len;
    if (channels) *channels = 0;
    if (sample_rate) *sample_rate = 0;
    if (output) *output = NULL;
    return -1;
}

int stb_vorbis_get_samples_short_interleaved(stb_vorbis *f, int channels,
                                              short *buffer, int num_shorts) {
    (void)f; (void)channels; (void)buffer; (void)num_shorts;
    return 0;
}

int stb_vorbis_get_frame_short_interleaved(stb_vorbis *f, int num_c,
                                            short *buffer, int num_shorts) {
    (void)f; (void)num_c; (void)buffer; (void)num_shorts;
    return 0;
}

#endif // __has_include
