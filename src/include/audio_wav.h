#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H

#include "types.h"
#include "sound.h"
#include <stdbool.h>

// WAV format codes
#define WAV_FORMAT_PCM         0x0001
#define WAV_FORMAT_ADPCM       0x0002
#define WAV_FORMAT_IEEE_FLOAT  0x0003
#define WAV_FORMAT_ALAW        0x0006
#define WAV_FORMAT_MULAW       0x0007
#define WAV_FORMAT_OKI_ADPCM   0x0010
#define WAV_FORMAT_IMA_ADPCM   0x0011
#define WAV_FORMAT_MEDIASPACE_ADPCM 0x0012
#define WAV_FORMAT_SIERRA_ADPCM 0x0013
#define WAV_FORMAT_G723_ADPCM  0x0014
#define WAV_FORMAT_DIGISTD     0x0015
#define WAV_FORMAT_DIGIFIX     0x0016
#define WAV_FORMAT_DIALOGIC_OKI_ADPCM 0x0017
#define WAV_FORMAT_YAMAHA_ADPCM 0x0020
#define WAV_FORMAT_SONARC      0x0021
#define WAV_FORMAT_DSPGROUP_TRUESPEECH 0x0022
#define WAV_FORMAT_ECHOSC1     0x0023
#define WAV_FORMAT_AUDIOFILE_AF36 0x0024
#define WAV_FORMAT_APTX        0x0025
#define WAV_FORMAT_AUDIOFILE_AF10 0x0026
#define WAV_FORMAT_DOLBY_AC2   0x0030
#define WAV_FORMAT_GSM610      0x0031
#define WAV_FORMAT_MSNAUDIO    0x0032
#define WAV_FORMAT_ANTEX_ADPCM 0x0033
#define WAV_FORMAT_CONTROL_RES_VQLPC 0x0034
#define WAV_FORMAT_DIGIREAL    0x0035
#define WAV_FORMAT_DOLBY_AC3_SPDIF 0x0036
#define WAV_FORMAT_ROCKWELL_ADPCM 0x003B
#define WAV_FORMAT_ROCKWELL_DIGITALK 0x003C
#define WAV_FORMAT_G723_1      0x0042
#define WAV_FORMAT_G729A       0x0083
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
    uint32 data_offset;  // Byte offset to the start of the data chunk
    uint32 data_size;
    const uint8* data_ptr;
} wav_info_t;

// Parse WAV file header and extract info (streaming-friendly)
wav_error_t wav_parse_header(const uint8* file_data, uint32 file_size, wav_info_t* info);

// Convert WAV data chunk to PCM in chunks (streaming)
wav_error_t wav_convert_chunk(const wav_info_t* info, const uint8* chunk_data, uint32 chunk_size,
                              uint8* pcm_output, uint32 pcm_output_size, uint32* bytes_written);

// Convert WAV chunk to canonical PCM format (S16, original channels/rate)
wav_error_t wav_decode_to_canonical(const wav_info_t* info, const uint8* chunk_data, uint32 chunk_size,
                                   uint8* output, uint32 output_size, uint32* bytes_written, PcmDesc* out_desc);

// Device-specific conversion functions
uint32_t convert_to_sb16_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                            uint8_t* dst, uint32_t dst_max_frames, PcmFormat target_format,
                            uint16_t target_channels, uint32_t target_rate);

uint32_t convert_to_ac97_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                             int16_t* dst, uint32_t dst_max_frames, uint32_t* out_sample_rate);

uint32_t convert_to_hda_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                           int16_t* dst, uint32_t dst_max_frames);

uint32_t convert_to_ensoniq_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                               int16_t* dst, uint32_t dst_max_frames);

// Get the required PCM buffer size for a given WAV info and chunk size
uint32 wav_get_pcm_chunk_size(const wav_info_t* info, uint32 data_chunk_size);

// Legacy function - loads entire file into memory
wav_error_t wav_parse(const uint8* file_data, uint32 file_size, wav_info_t* info);
uint8* wav_to_pcm(const wav_info_t* info, uint32* out_size, SoundFormat* out_format);

// Get error string
const char* wav_error_string(wav_error_t error);

#endif
