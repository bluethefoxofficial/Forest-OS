#ifndef STB_VORBIS_PORT_H
#define STB_VORBIS_PORT_H

/*
 * stb_vorbis configuration header for Forest-OS.
 *
 * This header exposes the stb_vorbis API types and functions.
 * The actual implementation lives in src/stb_vorbis_port.c, which
 * pulls in the upstream stb_vorbis.c when present.
 *
 * To obtain stb_vorbis.c:
 *   tools/fetch_stb_vorbis.sh      # downloads to src/stb_vorbis.c
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Shared configuration for both header-only and implementation compilation
#ifndef STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_STDIO
#endif
#ifndef STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_PUSHDATA_API
#endif

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include("../stb_vorbis.c")
#define STB_VORBIS_PORT_HAVE_UPSTREAM 1
#ifndef STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_HEADER_ONLY
#endif
#include "../stb_vorbis.c"
#undef STB_VORBIS_HEADER_ONLY
#else
#define STB_VORBIS_PORT_HAVE_UPSTREAM 0

// Minimal fallback declarations when stb_vorbis.c is not present
typedef struct stb_vorbis stb_vorbis;

// Allocation buffer (matches upstream definition)
typedef struct {
    char *alloc_buffer;
    int alloc_buffer_length_in_bytes;
} stb_vorbis_alloc;

typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;

typedef struct {
    char *vendor;
    int comment_list_length;
    char **comment_list;
} stb_vorbis_comment;

// Error codes
enum STBVorbisError {
    VORBIS__no_error = 0,
    VORBIS_need_more_data = 1,
    VORBIS_invalid_api_mixing = 2,
    VORBIS_outofmem = 3,
    VORBIS_feature_not_supported = 4,
    VORBIS_too_many_channels = 5,
    VORBIS_file_open_failure = 6,
    VORBIS_seek_without_length = 7,
    VORBIS_unexpected_eof = 10,
    VORBIS_seek_invalid = 11,
    VORBIS_invalid_setup = 20,
    VORBIS_invalid_stream = 21,
    VORBIS_missing_capture_pattern = 30,
    VORBIS_invalid_stream_structure_version = 31,
    VORBIS_continued_packet_flag_invalid = 32,
    VORBIS_incorrect_stream_serial_number = 33,
    VORBIS_invalid_first_page = 34,
    VORBIS_bad_packet_type = 35,
    VORBIS_cant_find_last_page = 36,
    VORBIS_seek_failed = 37,
    VORBIS_ogg_skeleton_not_supported = 38
};

#endif // fallback declarations

// API functions we'll use
stb_vorbis* stb_vorbis_open_memory(const unsigned char *data, int len,
                                    int *error, const stb_vorbis_alloc *alloc_buffer);
stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
stb_vorbis_comment stb_vorbis_get_comment(stb_vorbis *f);
unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f);
float stb_vorbis_stream_length_in_seconds(stb_vorbis *f);
void stb_vorbis_close(stb_vorbis *f);

// Decode entire file to interleaved samples
int stb_vorbis_decode_memory(const unsigned char *mem, int len,
                              int *channels, int *sample_rate,
                              short **output);

// Get samples from a file
int stb_vorbis_get_samples_short_interleaved(stb_vorbis *f, int channels,
                                              short *buffer, int num_shorts);
int stb_vorbis_get_frame_short_interleaved(stb_vorbis *f, int num_c,
                                            short *buffer, int num_shorts);

#endif // STB_VORBIS_PORT_H
