#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H

#include "types.h"
#include "sound.h"
#include <stdbool.h>

// WAV format codes
#define WAV_FORMAT_PCM         0x0001
#define WAV_FORMAT_IEEE_FLOAT  0x0003
#define WAV_FORMAT_ALAW        0x0006
#define WAV_FORMAT_MULAW       0x0007
#define WAV_FORMAT_EXTENSIBLE  0xFFFE

// Error codes
typedef enum {
    WAV_OK = 0,
    WAV_ERROR_NULL_PTR,
    WAV_ERROR_TOO_SMALL,
    WAV_ERROR_INVALID_RIFF,
    WAV_ERROR_INVALID_WAVE,
    WAV_ERROR_NO_FMT_CHUNK,
    WAV_ERROR_NO_DATA_CHUNK,
    WAV_ERROR_UNSUPPORTED_FORMAT,
    WAV_ERROR_UNSUPPORTED_CHANNELS,
    WAV_ERROR_UNSUPPORTED_BITDEPTH,
    WAV_ERROR_MEMORY
} wav_error_t;

// Extended WAV info structure
typedef struct {
    uint16 format_code;
    uint16 channels;
    uint32 sample_rate;
    uint32 byte_rate;
    uint16 block_align;
    uint16 bits_per_sample;
    uint32 data_size;
    const uint8* data_ptr;
} wav_info_t;

// Parse WAV file and extract info
wav_error_t wav_parse(const uint8* file_data, uint32 file_size, wav_info_t* info);

// Convert parsed WAV to PCM format suitable for sound drivers
// Returns allocated buffer (caller must kfree) or NULL on error
uint8* wav_to_pcm(const wav_info_t* info, uint32* out_size, SoundFormat* out_format);

// Get error string
const char* wav_error_string(wav_error_t error);

#endif
