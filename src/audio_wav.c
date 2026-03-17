#include "include/audio_wav.h"
#include "include/memory.h"
#include "include/libc/string.h"
#include "include/screen.h"

// Kernel heap allocators (size-only variant)
extern void* kmalloc(size_t size);

// WAV chunk identifiers (little-endian)
#define WAV_RIFF 0x46464952  // 'RIFF'
#define WAV_WAVE 0x45564157  // 'WAVE'
#define WAV_FMT  0x20746D66  // 'fmt '
#define WAV_DATA 0x61746164  // 'data'

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

wav_error_t wav_parse_header(const uint8* file_data, uint32 file_size, wav_info_t* info) {
    if (!file_data || !info) {
        return WAV_ERROR_NULL_PTR;
    }

    if (file_size < sizeof(wav_riff_header_t) + sizeof(wav_chunk_header_t)) {
        return WAV_ERROR_TOO_SMALL;
    }

    // Parse RIFF header
    if (*(uint32_t*)file_data != WAV_RIFF) {
        return WAV_ERROR_INVALID_RIFF;
    }
    if (*(uint32_t*)(file_data + 8) != WAV_WAVE) {
        return WAV_ERROR_INVALID_WAVE;
    }

    // Initialize info
    memset(info, 0, sizeof(wav_info_t));

    // Parse chunks until we find fmt and data
    const uint8* end = file_data + file_size;
    bool have_fmt = false;
    bool have_data = false;

    // Track offsets so callers can stream from disk
    uint32 offset = 12; // already consumed RIFF + size + WAVE

    while (offset + 8 <= file_size) {
        const uint8* chunk_header = file_data + offset;
        uint32_t chunk_id = *(const uint32_t*)chunk_header;
        uint32_t chunk_size = *(const uint32_t*)(chunk_header + 4);
        const uint8* chunk_data = chunk_header + 8;
        uint32_t next_offset = offset + 8 + chunk_size;

        if (chunk_id == WAV_FMT) {
            if (chunk_size < sizeof(wav_fmt_chunk_t) || chunk_data + sizeof(wav_fmt_chunk_t) > end) {
                return WAV_ERROR_NO_FMT_CHUNK;
            }

            const wav_fmt_chunk_t* fmt = (const wav_fmt_chunk_t*)chunk_data;
            info->format_code = fmt->audio_format;
            info->channels = fmt->num_channels;
            info->sample_rate = fmt->sample_rate;
            info->byte_rate = fmt->byte_rate;
            info->block_align = fmt->block_align;
            info->bits_per_sample = fmt->bits_per_sample;

            // Handle extensible format
            if (fmt->audio_format == WAV_FORMAT_EXTENSIBLE &&
                chunk_size >= sizeof(wav_fmt_extensible_t) &&
                chunk_data + sizeof(wav_fmt_extensible_t) <= end) {
                const wav_fmt_extensible_t* ext = (const wav_fmt_extensible_t*)chunk_data;
                info->format_code = ext->sub_format[0] | (ext->sub_format[1] << 8);
                info->bits_per_sample = ext->bits_per_sample;
                info->block_align = ext->block_align;
                info->byte_rate = ext->byte_rate;
            }

            // Some files leave byte_rate/block_align inconsistent—recompute if possible
            if (info->channels && info->bits_per_sample) {
                uint16 expected_block = (uint16)((info->channels * info->bits_per_sample) / 8);
                if (info->block_align == 0) {
                    info->block_align = expected_block;
                }
                if (info->byte_rate == 0 && info->sample_rate) {
                    info->byte_rate = info->sample_rate * info->block_align;
                }
            }

            have_fmt = true;
        } else if (chunk_id == WAV_DATA) {
            info->data_ptr = (chunk_data <= end) ? chunk_data : NULL;
            info->data_offset = offset + 8;
            info->data_size = chunk_size;
            have_data = true;
            break;  // data chunk found — stop scanning
        } else {
            // Unknown chunk, skip if possible
        }

        // Word alignment
        if (chunk_size & 1) {
            next_offset += 1;
        }

        // Prevent infinite loops on malformed files
        if (next_offset <= offset || next_offset > file_size) {
            break;
        }
        offset = next_offset;
    }

    if (!have_fmt) return WAV_ERROR_NO_FMT_CHUNK;
    if (!have_data) return WAV_ERROR_NO_DATA_CHUNK;

    return WAV_OK;
}

// Legacy parse function
// Get the required PCM buffer size for a given WAV data chunk
uint32 wav_get_pcm_chunk_size(const wav_info_t* info, uint32 data_chunk_size) {
    if (!info) return 0;

    // Calculate based on format
    uint32 sample_count = 0;
    switch (info->format_code) {
        case WAV_FORMAT_PCM:
            if (info->bits_per_sample == 8) {
                sample_count = data_chunk_size;
            } else if (info->bits_per_sample == 16) {
                sample_count = data_chunk_size / 2;
            } else if (info->bits_per_sample == 24) {
                sample_count = data_chunk_size / 3;
            } else if (info->bits_per_sample == 32) {
                sample_count = data_chunk_size / 4;
            } else {
                return 0;
            }
            break;
        case WAV_FORMAT_IEEE_FLOAT:
            if (info->bits_per_sample == 32) {
                sample_count = data_chunk_size / 4;
            } else if (info->bits_per_sample == 64) {
                sample_count = data_chunk_size / 8;
            } else {
                return 0;
            }
            break;
        case WAV_FORMAT_ALAW:
        case WAV_FORMAT_MULAW:
            sample_count = data_chunk_size;
            break;
        default:
            return 0;
    }

    // All conversions output 16-bit signed PCM
    return sample_count * sizeof(int16);
}

// Convert IEEE 754 float sample to 16-bit signed PCM (integer math only)
static int16 float_to_pcm16_raw(uint32 raw_float) {
    // Extract IEEE 754 components
    int sign = (raw_float >> 31) & 1;
    int exponent = ((raw_float >> 23) & 0xFF) - 127;
    uint32 mantissa = (raw_float & 0x7FFFFF) | 0x800000; // Add implicit 1

    // Handle special cases
    if (exponent == -127 && (raw_float & 0x7FFFFF) == 0) {
        return 0; // Zero
    }
    if (exponent >= 1) {
        // Value >= 1.0, clamp to max
        return sign ? -32767 : 32767;
    }
    if (exponent < -15) {
        return 0; // Too small
    }

    // Scale mantissa to get value * 32767
    // mantissa is 24-bit (1.23 fixed point), we want value * 32767
    // value = mantissa * 2^(exponent-23)
    // result = value * 32767 = mantissa * 32767 * 2^(exponent-23)
    int64_t result = (int64_t)mantissa * 32767;
    int shift = 23 - exponent;
    result >>= shift;

    // Clamp to 16-bit range
    if (result > 32767) result = 32767;

    return sign ? -(int16)result : (int16)result;
}

// Wrapper for raw float data (reads 4 bytes as uint32)
static int16 float_to_pcm16_ptr(const uint8* raw_data) {
    uint32 raw_float = raw_data[0] | (raw_data[1] << 8) | (raw_data[2] << 16) | (raw_data[3] << 24);
    return float_to_pcm16_raw(raw_float);
}

// Convert IEEE 754 double to 16-bit signed PCM (integer math only)
static int16 double_to_pcm16_ptr(const uint8* raw_data) {
    // Read 64-bit little-endian
    uint64_t raw_double = 0;
    for (int i = 0; i < 8; i++) {
        raw_double |= ((uint64_t)raw_data[i]) << (i * 8);
    }

    // Extract IEEE 754 double components
    int sign = (raw_double >> 63) & 1;
    int exponent = ((raw_double >> 52) & 0x7FF) - 1023;
    uint64_t mantissa = (raw_double & 0xFFFFFFFFFFFFFULL) | 0x10000000000000ULL; // Add implicit 1

    // Handle special cases
    if (exponent == -1023 && (raw_double & 0xFFFFFFFFFFFFFULL) == 0) {
        return 0; // Zero
    }
    if (exponent >= 1) {
        // Value >= 1.0, clamp to max
        return sign ? -32767 : 32767;
    }
    if (exponent < -15) {
        return 0; // Too small
    }

    // Scale mantissa to get value * 32767
    // mantissa is 53-bit (1.52 fixed point)
    int64_t result = (int64_t)(mantissa >> 20) * 32767; // Use high 33 bits
    int shift = 33 - exponent;
    result >>= shift;

    // Clamp
    if (result > 32767) result = 32767;

    return sign ? -(int16)result : (int16)result;
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

// A-law to linear PCM conversion table
static const int16 alaw_to_pcm[256] = {
    -5504, -5248, -6016, -5760, -4480, -4224, -4992, -4736,
    -7552, -7296, -8064, -7808, -6528, -6272, -7040, -6784,
    -2752, -2624, -3008, -2880, -2240, -2112, -2496, -2368,
    -3776, -3648, -4032, -3904, -3264, -3136, -3520, -3392,
    -22016, -20992, -24064, -23040, -17920, -16896, -19968, -18944,
    -30208, -29184, -32256, -31232, -26112, -25088, -28160, -27136,
    -11008, -10496, -12032, -11520, -8960, -8448, -9984, -9472,
    -15104, -14592, -16128, -15616, -13056, -12544, -14080, -13568,
    -344, -328, -376, -360, -280, -264, -312, -296,
    -472, -456, -504, -488, -408, -392, -440, -424,
    -88, -72, -120, -104, -24, -8, -56, -40,
    -216, -200, -248, -232, -152, -136, -184, -168,
    -1376, -1312, -1504, -1440, -1120, -1056, -1248, -1184,
    -1888, -1824, -2016, -1952, -1632, -1568, -1760, -1696,
    -688, -656, -752, -720, -560, -528, -624, -592,
    -944, -912, -1008, -976, -816, -784, -880, -848,
    5504, 5248, 6016, 5760, 4480, 4224, 4992, 4736,
    7552, 7296, 8064, 7808, 6528, 6272, 7040, 6784,
    2752, 2624, 3008, 2880, 2240, 2112, 2496, 2368,
    3776, 3648, 4032, 3904, 3264, 3136, 3520, 3392,
    22016, 20992, 24064, 23040, 17920, 16896, 19968, 18944,
    30208, 29184, 32256, 31232, 26112, 25088, 28160, 27136,
    11008, 10496, 12032, 11520, 8960, 8448, 9984, 9472,
    15104, 14592, 16128, 15616, 13056, 12544, 14080, 13568,
    344, 328, 376, 360, 280, 264, 312, 296,
    472, 456, 504, 488, 408, 392, 440, 424,
    88, 72, 120, 104, 24, 8, 56, 40,
    216, 200, 248, 232, 152, 136, 184, 168,
    1376, 1312, 1504, 1440, 1120, 1056, 1248, 1184,
    1888, 1824, 2016, 1952, 1632, 1568, 1760, 1696,
    688, 656, 752, 720, 560, 528, 624, 592,
    944, 912, 1008, 976, 816, 784, 880, 848
};

// μ-law to linear PCM conversion table
static const int16 mulaw_to_pcm[256] = {
    -32124, -31100, -30076, -29052, -28028, -27004, -25980, -24956,
    -23932, -22908, -21884, -20860, -19836, -18812, -17788, -16764,
    -15996, -15484, -14972, -14460, -13948, -13436, -12924, -12412,
    -11900, -11388, -10876, -10364, -9852, -9340, -8828, -8316,
    -7932, -7676, -7420, -7164, -6908, -6652, -6396, -6140,
    -5884, -5628, -5372, -5116, -4860, -4604, -4348, -4092,
    -3900, -3772, -3644, -3516, -3388, -3260, -3132, -3004,
    -2876, -2748, -2620, -2492, -2364, -2236, -2108, -1980,
    -1884, -1820, -1756, -1692, -1628, -1564, -1500, -1436,
    -1372, -1308, -1244, -1180, -1116, -1052, -988, -924,
    -876, -844, -812, -780, -748, -716, -684, -652,
    -620, -588, -556, -524, -492, -460, -428, -396,
    -372, -356, -340, -324, -308, -292, -276, -260,
    -244, -228, -212, -196, -180, -164, -148, -132,
    -120, -112, -104, -96, -88, -80, -72, -64,
    -56, -48, -40, -32, -24, -16, -8, 0,
    32124, 31100, 30076, 29052, 28028, 27004, 25980, 24956,
    23932, 22908, 21884, 20860, 19836, 18812, 17788, 16764,
    15996, 15484, 14972, 14460, 13948, 13436, 12924, 12412,
    11900, 11388, 10876, 10364, 9852, 9340, 8828, 8316,
    7932, 7676, 7420, 7164, 6908, 6652, 6396, 6140,
    5884, 5628, 5372, 5116, 4860, 4604, 4348, 4092,
    3900, 3772, 3644, 3516, 3388, 3260, 3132, 3004,
    2876, 2748, 2620, 2492, 2364, 2236, 2108, 1980,
    1884, 1820, 1756, 1692, 1628, 1564, 1500, 1436,
    1372, 1308, 1244, 1180, 1116, 1052, 988, 924,
    876, 844, 812, 780, 748, 716, 684, 652,
    620, 588, 556, 524, 492, 460, 428, 396,
    372, 356, 340, 324, 308, 292, 276, 260,
    244, 228, 212, 196, 180, 164, 148, 132,
    120, 112, 104, 96, 88, 80, 72, 64,
    56, 48, 40, 32, 24, 16, 8, 0
};

// Convert a WAV data chunk to PCM
wav_error_t wav_convert_chunk(const wav_info_t* info, const uint8* chunk_data, uint32 chunk_size,
                              uint8* pcm_output, uint32 pcm_output_size, uint32* bytes_written) {
    if (!info || !chunk_data || !pcm_output || !bytes_written) {
        return WAV_ERROR_NULL_PTR;
    }

    *bytes_written = 0;

    // Validate format
    if (info->format_code != WAV_FORMAT_PCM &&
        info->format_code != WAV_FORMAT_IEEE_FLOAT &&
        info->format_code != WAV_FORMAT_ALAW &&
        info->format_code != WAV_FORMAT_MULAW) {
        return WAV_ERROR_UNSUPPORTED_FORMAT;
    }

    if (info->channels != 1 && info->channels != 2) {
        return WAV_ERROR_UNSUPPORTED_CHANNELS;
    }

    // Calculate expected PCM size
    uint32 expected_pcm_size = wav_get_pcm_chunk_size(info, chunk_size);
    if (expected_pcm_size == 0) {
        return WAV_ERROR_UNSUPPORTED_BITDEPTH;
    }

    if (pcm_output_size < expected_pcm_size) {
        return WAV_ERROR_MEMORY;
    }

    int16* dst = (int16*)pcm_output;

    switch (info->format_code) {
        case WAV_FORMAT_PCM: {
            if (info->bits_per_sample == 8) {
                const uint8* src = chunk_data;
                for (uint32 i = 0; i < chunk_size; i++) {
                    dst[i] = u8_to_pcm16(src[i]);
                }
                *bytes_written = chunk_size * sizeof(int16);
            } else if (info->bits_per_sample == 16) {
                memcpy(dst, chunk_data, chunk_size);
                *bytes_written = chunk_size;
            } else if (info->bits_per_sample == 24) {
                const uint8* src = chunk_data;
                for (uint32 i = 0; i < chunk_size / 3; i++) {
                    dst[i] = s24_to_pcm16(src + i * 3);
                }
                *bytes_written = (chunk_size / 3) * sizeof(int16);
            } else if (info->bits_per_sample == 32) {
                const int32* src = (const int32*)chunk_data;
                for (uint32 i = 0; i < chunk_size / 4; i++) {
                    dst[i] = s32_to_pcm16(src[i]);
                }
                *bytes_written = (chunk_size / 4) * sizeof(int16);
            } else {
                return WAV_ERROR_UNSUPPORTED_BITDEPTH;
            }
            break;
        }

        case WAV_FORMAT_IEEE_FLOAT: {
            if (info->bits_per_sample == 32) {
                const uint8* src = (const uint8*)chunk_data;
                for (uint32 i = 0; i < chunk_size / 4; i++) {
                    dst[i] = float_to_pcm16_ptr(src + i * 4);
                }
                *bytes_written = (chunk_size / 4) * sizeof(int16);
            } else if (info->bits_per_sample == 64) {
                const uint8* src = (const uint8*)chunk_data;
                for (uint32 i = 0; i < chunk_size / 8; i++) {
                    dst[i] = double_to_pcm16_ptr(src + i * 8);
                }
                *bytes_written = (chunk_size / 8) * sizeof(int16);
            } else {
                return WAV_ERROR_UNSUPPORTED_BITDEPTH;
            }
            break;
        }

        case WAV_FORMAT_ALAW: {
            const uint8* src = chunk_data;
            for (uint32 i = 0; i < chunk_size; i++) {
                dst[i] = alaw_to_pcm[src[i]];
            }
            *bytes_written = chunk_size * sizeof(int16);
            break;
        }

        case WAV_FORMAT_MULAW: {
            const uint8* src = chunk_data;
            for (uint32 i = 0; i < chunk_size; i++) {
                dst[i] = mulaw_to_pcm[src[i]];
            }
            *bytes_written = chunk_size * sizeof(int16);
            break;
        }

        default:
            return WAV_ERROR_UNSUPPORTED_FORMAT;
    }

    return WAV_OK;
}

// Downmix stereo to mono by averaging channels
static void stereo_to_mono(int16* samples, uint32 sample_count) {
    for (uint32 i = 0; i < sample_count; i++) {
        int32 left = samples[i * 2];
        int32 right = samples[i * 2 + 1];
        samples[i] = (int16)((left + right) / 2);
    }
}

// Simple linear interpolation resampling (nearest neighbor for SB16, linear for others)
// Uses 16.16 fixed-point math to avoid floating point operations
static void resample_audio(int16* input, uint32 input_samples, int16* output, uint32 output_samples,
                          uint32 input_rate, uint32 output_rate, bool high_quality) {
    if (input_rate == output_rate) {
        // No resampling needed
        memcpy(output, input, input_samples * sizeof(int16));
        return;
    }

    if (!high_quality) {
        // Nearest neighbor (for SB16)
        for (uint32 i = 0; i < output_samples; i++) {
            uint32 input_idx = (i * input_rate) / output_rate;
            if (input_idx >= input_samples) input_idx = input_samples - 1;
            output[i] = input[input_idx];
        }
    } else {
        // Linear interpolation using 16.16 fixed-point
        // step = (input_rate << 16) / output_rate
        uint32 step = ((uint64_t)input_rate << 16) / output_rate;
        uint32 pos = 0; // 16.16 fixed-point position

        for (uint32 i = 0; i < output_samples; i++) {
            uint32 input_idx = pos >> 16;
            uint32 frac = pos & 0xFFFF; // Fractional part (0-65535)

            if (input_idx >= input_samples - 1) {
                output[i] = input[input_samples - 1];
            } else {
                int32 s1 = input[input_idx];
                int32 s2 = input[input_idx + 1];
                // Linear interpolation: s1 + (s2 - s1) * frac / 65536
                output[i] = (int16)(s1 + (((s2 - s1) * (int32)frac) >> 16));
            }
            pos += step;
        }
    }
}

// Convert signed 16-bit to unsigned 8-bit
static void s16_to_u8(const int16* input, uint8* output, uint32 sample_count) {
    for (uint32 i = 0; i < sample_count; i++) {
        // Convert to unsigned: 0x8000 -> 0x00, 0x0000 -> 0x80, 0x7FFF -> 0xFF
        int32 val = input[i] + 32768; // Shift range from [-32768,32767] to [0,65535]
        val = val * 255 / 65535; // Scale to [0,255]
        if (val < 0) val = 0;
        if (val > 255) val = 255;
        output[i] = (uint8)val;
    }
}

// Convert little-endian to big-endian (if needed)
static void convert_endianness(int16* samples, uint32 sample_count) {
    for (uint32 i = 0; i < sample_count; i++) {
        uint16 val = *(uint16*)&samples[i];
        samples[i] = (int16)((val >> 8) | (val << 8));
    }
}

// Decode WAV chunk to canonical PCM format (S16, original rate/channels)
wav_error_t wav_decode_to_canonical(const wav_info_t* info, const uint8* chunk_data, uint32 chunk_size,
                                   uint8* output, uint32 output_size, uint32* bytes_written, PcmDesc* out_desc) {
    if (!info || !chunk_data || !output || !bytes_written || !out_desc) {
        return WAV_ERROR_NULL_PTR;
    }

    *bytes_written = 0;

    // Validate input format
    if (info->format_code != WAV_FORMAT_PCM &&
        info->format_code != WAV_FORMAT_IEEE_FLOAT &&
        info->format_code != WAV_FORMAT_ALAW &&
        info->format_code != WAV_FORMAT_MULAW) {
        return WAV_ERROR_UNSUPPORTED_FORMAT;
    }

    if (info->channels != 1 && info->channels != 2) {
        return WAV_ERROR_UNSUPPORTED_CHANNELS;
    }

    // Convert chunk to 16-bit signed PCM
    uint32 pcm16_size = wav_get_pcm_chunk_size(info, chunk_size);
    if (pcm16_size == 0) {
        return WAV_ERROR_UNSUPPORTED_BITDEPTH;
    }

    if (pcm16_size > output_size) {
        return WAV_ERROR_MEMORY;
    }

    uint32 dummy;
    wav_error_t err = wav_convert_chunk(info, chunk_data, chunk_size, output, pcm16_size, &dummy);
    if (err != WAV_OK) {
        return err;
    }

    // Set output descriptor
    out_desc->format = PCM_S16;
    out_desc->channels = info->channels;
    out_desc->sample_rate = info->sample_rate;

    *bytes_written = pcm16_size;
    return WAV_OK;
}

// Legacy parse function - just calls header parser
wav_error_t wav_parse(const uint8* file_data, uint32 file_size, wav_info_t* info) {
    return wav_parse_header(file_data, file_size, info);
}


uint8* wav_to_pcm(const wav_info_t* info, uint32* out_size, SoundFormat* out_format) {
    if (!info || !out_size || !out_format || !info->data_ptr) {
        return NULL;
    }

    // Validate channel count
    if (info->channels < 1 || info->channels > 2) {
        print("[WAV] Unsupported channel count: ");
        print_dec(info->channels);
        print(" (only mono/stereo supported)\n");
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

                const uint8* src = (const uint8*)info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    dst[i] = float_to_pcm16_ptr(src + i * 4);
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

                const uint8* src = (const uint8*)info->data_ptr;
                int16* dst = (int16*)pcm_data;

                for (uint32 i = 0; i < sample_count; i++) {
                    dst[i] = double_to_pcm16_ptr(src + i * 8);
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

        case WAV_FORMAT_ALAW: {
            // Convert A-law to 16-bit signed PCM
            uint32 sample_count = info->data_size;
            uint32 pcm_size = sample_count * sizeof(int16);

            uint8* pcm_data = kmalloc(pcm_size);
            if (!pcm_data) return NULL;

            const uint8* src = info->data_ptr;
            int16* dst = (int16*)pcm_data;

            for (uint32 i = 0; i < sample_count; i++) {
                dst[i] = alaw_to_pcm[src[i]];
            }

            *out_size = pcm_size;
            out_format->bits_per_sample = 16;
            out_format->signed_samples = true;
            return pcm_data;
        }

        case WAV_FORMAT_MULAW: {
            // Convert μ-law to 16-bit signed PCM
            uint32 sample_count = info->data_size;
            uint32 pcm_size = sample_count * sizeof(int16);

            uint8* pcm_data = kmalloc(pcm_size);
            if (!pcm_data) return NULL;

            const uint8* src = info->data_ptr;
            int16* dst = (int16*)pcm_data;

            for (uint32 i = 0; i < sample_count; i++) {
                dst[i] = mulaw_to_pcm[src[i]];
            }

            *out_size = pcm_size;
            out_format->bits_per_sample = 16;
            out_format->signed_samples = true;
            return pcm_data;
        }

        case WAV_FORMAT_ADPCM:
        case WAV_FORMAT_IMA_ADPCM:
        case WAV_FORMAT_GSM610:
        case WAV_FORMAT_G723_ADPCM:
            print("[WAV] Compressed format 0x");
            print_hex(info->format_code);
            print(" not yet supported (ADPCM/GSM/G.723)\n");
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

uint32_t convert_to_ac97_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                             int16_t* dst, uint32_t dst_max_frames, uint32_t* out_sample_rate) {
    if (!src || !src_desc || !dst || !out_sample_rate) {
        return 0;
    }

    uint32_t src_samples = src_frames * src_desc->channels;
    uint32_t dst_frames = src_frames < dst_max_frames ? src_frames : dst_max_frames;
    uint32_t dst_samples = dst_frames * 2;

    const int16_t* src_pcm = (const int16_t*)src;

    if (src_desc->channels == 2 && src_desc->sample_rate == 48000) {
        memcpy(dst, src_pcm, dst_samples * sizeof(int16_t));
        *out_sample_rate = 48000;
        return dst_frames;
    }

    if (src_desc->channels == 2 && src_desc->sample_rate != 48000) {
        uint32_t target_frames = (dst_frames * 48000) / src_desc->sample_rate;
        if (target_frames > dst_max_frames) target_frames = dst_max_frames;

        for (uint32_t i = 0; i < target_frames; i++) {
            uint32_t src_idx = (i * src_desc->sample_rate) / 48000;
            if (src_idx >= src_frames) src_idx = src_frames - 1;
            dst[i * 2] = src_pcm[src_idx * 2];
            dst[i * 2 + 1] = src_pcm[src_idx * 2 + 1];
        }
        *out_sample_rate = 48000;
        return target_frames;
    }

    if (src_desc->channels == 1) {
        for (uint32_t i = 0; i < dst_frames; i++) {
            uint32_t src_idx = i;
            if (src_idx >= src_frames) src_idx = src_frames - 1;
            int16_t sample = src_pcm[src_idx];
            dst[i * 2] = sample;
            dst[i * 2 + 1] = sample;
        }
        *out_sample_rate = 48000;
        return dst_frames;
    }

    memcpy(dst, src_pcm, dst_samples * sizeof(int16_t));
    *out_sample_rate = 48000;
    return dst_frames;
}
