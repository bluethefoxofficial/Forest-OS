#include "include/audio_wav.h"
#include "include/memory.h"
#include "include/libc/string.h"
#include "include/screen.h"

// Kernel heap allocators (size-only variant)
extern void* kmalloc(size_t size);

#pragma pack(push, 1)
typedef struct {
    char riff_id[4];
    uint32 chunk_size;
    char wave_id[4];
} wav_riff_header_t;

typedef struct {
    char id[4];
    uint32 size;
} wav_chunk_header_t;

typedef struct {
    uint16 audio_format;
    uint16 num_channels;
    uint32 sample_rate;
    uint32 byte_rate;
    uint16 block_align;
    uint16 bits_per_sample;
} wav_fmt_chunk_t;

typedef struct {
    uint16 audio_format;
    uint16 num_channels;
    uint32 sample_rate;
    uint32 byte_rate;
    uint16 block_align;
    uint16 bits_per_sample;
    uint16 extension_size;
    uint16 valid_bits_per_sample;
    uint32 channel_mask;
    uint8  sub_format[16];
} wav_fmt_extensible_t;
#pragma pack(pop)

wav_error_t wav_parse(const uint8* file_data, uint32 file_size, wav_info_t* info) {
    if (!file_data || !info) {
        return WAV_ERROR_NULL_PTR;
    }

    if (file_size < sizeof(wav_riff_header_t) + sizeof(wav_chunk_header_t)) {
        return WAV_ERROR_TOO_SMALL;
    }

    // Parse RIFF header
    const wav_riff_header_t* riff = (const wav_riff_header_t*)file_data;
    if (memcmp(riff->riff_id, "RIFF", 4) != 0) {
        return WAV_ERROR_INVALID_RIFF;
    }
    if (memcmp(riff->wave_id, "WAVE", 4) != 0) {
        return WAV_ERROR_INVALID_WAVE;
    }

    // Initialize info
    memset(info, 0, sizeof(wav_info_t));

    // Parse chunks
    const uint8* ptr = file_data + sizeof(wav_riff_header_t);
    const uint8* end = file_data + file_size;
    bool have_fmt = false;
    bool have_data = false;

    while (ptr + sizeof(wav_chunk_header_t) <= end) {
        const wav_chunk_header_t* chunk = (const wav_chunk_header_t*)ptr;
        ptr += sizeof(wav_chunk_header_t);

        if (ptr + chunk->size > end) {
            break;
        }

        if (memcmp(chunk->id, "fmt ", 4) == 0) {
            if (chunk->size < sizeof(wav_fmt_chunk_t)) {
                return WAV_ERROR_NO_FMT_CHUNK;
            }

            const wav_fmt_chunk_t* fmt = (const wav_fmt_chunk_t*)ptr;
            info->format_code = fmt->audio_format;
            info->channels = fmt->num_channels;
            info->sample_rate = fmt->sample_rate;
            info->byte_rate = fmt->byte_rate;
            info->block_align = fmt->block_align;
            info->bits_per_sample = fmt->bits_per_sample;

            // Handle extensible format
            if (fmt->audio_format == WAV_FORMAT_EXTENSIBLE &&
                chunk->size >= sizeof(wav_fmt_extensible_t)) {
                const wav_fmt_extensible_t* ext = (const wav_fmt_extensible_t*)ptr;
                // Extract actual format from GUID (first 2 bytes)
                info->format_code = ext->sub_format[0] | (ext->sub_format[1] << 8);
            }

            have_fmt = true;
        } else if (memcmp(chunk->id, "data", 4) == 0) {
            info->data_ptr = ptr;
            info->data_size = chunk->size;
            have_data = true;
        }

        // Move to next chunk (with padding for word alignment)
        uint32 advance = chunk->size;
        if (advance & 1) advance++;
        ptr += advance;
    }

    if (!have_fmt) return WAV_ERROR_NO_FMT_CHUNK;
    if (!have_data) return WAV_ERROR_NO_DATA_CHUNK;

    return WAV_OK;
}

// Convert IEEE 754 float sample to 16-bit signed PCM
static int16 float_to_pcm16(float sample) {
    // Clamp to [-1.0, 1.0]
    if (sample > 1.0f) sample = 1.0f;
    if (sample < -1.0f) sample = -1.0f;

    // Convert to 16-bit signed
    return (int16)(sample * 32767.0f);
}

// Convert 8-bit unsigned to 16-bit signed PCM
static int16 u8_to_pcm16(uint8 sample) {
    // 8-bit WAV is unsigned (0-255), center is 128
    // Convert to signed 16-bit
    return (int16)((sample - 128) << 8);
}

// Convert 24-bit signed to 16-bit signed PCM
static int16 s24_to_pcm16(const uint8* sample) {
    // 24-bit is little-endian, signed
    int32 val = sample[0] | (sample[1] << 8) | (sample[2] << 16);
    // Sign extend
    if (val & 0x800000) val |= 0xFF000000;
    // Convert to 16-bit
    return (int16)(val >> 8);
}

// Convert 32-bit signed to 16-bit signed PCM
static int16 s32_to_pcm16(int32 sample) {
    return (int16)(sample >> 16);
}

uint8* wav_to_pcm(const wav_info_t* info, uint32* out_size, SoundFormat* out_format) {
    if (!info || !out_size || !out_format || !info->data_ptr) {
        return NULL;
    }

    // Validate channel count
    if (info->channels < 1 || info->channels > 2) {
        print("[WAV] Unsupported channel count: ");
        print_dec(info->channels);
        print("\n");
        return NULL;
    }

    out_format->sample_rate = info->sample_rate;
    out_format->channels = info->channels;

    // Handle different formats
    switch (info->format_code) {
        case WAV_FORMAT_PCM: {
            if (info->bits_per_sample == 8) {
                // Convert 8-bit unsigned to 16-bit signed
                uint32 sample_count = info->data_size;
                uint32 pcm_size = sample_count * sizeof(int16);

                uint8* pcm_data = kmalloc(pcm_size);
                if (!pcm_data) return NULL;

                const uint8* src = info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    dst[i] = u8_to_pcm16(src[i]);
                }

                *out_size = pcm_size;
                out_format->bits_per_sample = 16;
                out_format->signed_samples = true;
                return pcm_data;
            } else if (info->bits_per_sample == 16) {
                // Direct 16-bit PCM - copy as-is
                uint8* pcm_data = kmalloc(info->data_size);
                if (!pcm_data) return NULL;

                memcpy(pcm_data, info->data_ptr, info->data_size);
                *out_size = info->data_size;
                out_format->bits_per_sample = 16;
                out_format->signed_samples = true;
                return pcm_data;
            } else if (info->bits_per_sample == 24) {
                // Convert 24-bit to 16-bit
                uint32 sample_count = info->data_size / 3;
                uint32 pcm_size = sample_count * sizeof(int16);

                uint8* pcm_data = kmalloc(pcm_size);
                if (!pcm_data) return NULL;

                const uint8* src = info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    dst[i] = s24_to_pcm16(src + i * 3);
                }

                *out_size = pcm_size;
                out_format->bits_per_sample = 16;
                out_format->signed_samples = true;
                return pcm_data;
            } else if (info->bits_per_sample == 32) {
                // Convert 32-bit signed to 16-bit
                uint32 sample_count = info->data_size / sizeof(int32);
                uint32 pcm_size = sample_count * sizeof(int16);

                uint8* pcm_data = kmalloc(pcm_size);
                if (!pcm_data) return NULL;

                const int32* src = (const int32*)info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    dst[i] = s32_to_pcm16(src[i]);
                }

                *out_size = pcm_size;
                out_format->bits_per_sample = 16;
                out_format->signed_samples = true;
                return pcm_data;
            } else {
                print("[WAV] Unsupported PCM bit depth: ");
                print_dec(info->bits_per_sample);
                print("\n");
                return NULL;
            }
        }

        case WAV_FORMAT_IEEE_FLOAT: {
            // Convert float to 16-bit PCM
            if (info->bits_per_sample == 32) {
                uint32 sample_count = info->data_size / sizeof(float);
                uint32 pcm_size = sample_count * sizeof(int16);

                uint8* pcm_data = kmalloc(pcm_size);
                if (!pcm_data) return NULL;

                const float* src = (const float*)info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    dst[i] = float_to_pcm16(src[i]);
                }

                *out_size = pcm_size;
                out_format->bits_per_sample = 16;
                out_format->signed_samples = true;
                return pcm_data;
            } else if (info->bits_per_sample == 64) {
                // Convert double to 16-bit PCM
                uint32 sample_count = info->data_size / sizeof(double);
                uint32 pcm_size = sample_count * sizeof(int16);

                uint8* pcm_data = kmalloc(pcm_size);
                if (!pcm_data) return NULL;

                const double* src = (const double*)info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    double sample = src[i];
                    if (sample > 1.0) sample = 1.0;
                    if (sample < -1.0) sample = -1.0;
                    dst[i] = (int16)(sample * 32767.0);
                }

                *out_size = pcm_size;
                out_format->bits_per_sample = 16;
                out_format->signed_samples = true;
                return pcm_data;
            } else {
                print("[WAV] Unsupported float bit depth: ");
                print_dec(info->bits_per_sample);
                print("\n");
                return NULL;
            }
        }

        case WAV_FORMAT_ALAW:
        case WAV_FORMAT_MULAW:
            print("[WAV] A-law/mu-law not yet supported\n");
            return NULL;

        default:
            print("[WAV] Unsupported format code: 0x");
            print_hex(info->format_code);
            print("\n");
            return NULL;
    }
}

const char* wav_error_string(wav_error_t error) {
    switch (error) {
        case WAV_OK: return "Success";
        case WAV_ERROR_NULL_PTR: return "Null pointer";
        case WAV_ERROR_TOO_SMALL: return "File too small";
        case WAV_ERROR_INVALID_RIFF: return "Invalid RIFF header";
        case WAV_ERROR_INVALID_WAVE: return "Invalid WAVE header";
        case WAV_ERROR_NO_FMT_CHUNK: return "No fmt chunk";
        case WAV_ERROR_NO_DATA_CHUNK: return "No data chunk";
        case WAV_ERROR_UNSUPPORTED_FORMAT: return "Unsupported format";
        case WAV_ERROR_UNSUPPORTED_CHANNELS: return "Unsupported channels";
        case WAV_ERROR_UNSUPPORTED_BITDEPTH: return "Unsupported bit depth";
        case WAV_ERROR_MEMORY: return "Memory allocation failed";
        default: return "Unknown error";
    }
}
