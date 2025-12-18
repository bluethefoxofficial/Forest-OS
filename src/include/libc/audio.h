#ifndef LIBC_AUDIO_H
#define LIBC_AUDIO_H

#include "../types.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32 sample_rate;
    uint16 channels;
    uint16 bits_per_sample;
    bool signed_samples;
} audio_format_t;

typedef bool (*audio_pcm_handler_t)(const uint8* data,
                                    uint32 length,
                                    const audio_format_t* format,
                                    void* context);

typedef void (*audio_beep_handler_t)(uint32 frequency_hz,
                                     uint32 duration_ms,
                                     void* context);

typedef struct {
    audio_pcm_handler_t output;
    audio_beep_handler_t beep;
    void* context;
} audio_driver_hooks_t;

bool audio_register_driver(const audio_driver_hooks_t* hooks);
void audio_unregister_driver(void);
bool audio_driver_ready(void);
bool audio_play_pcm(const uint8* data, uint32 length, const audio_format_t* format);
void audio_beep(uint32 frequency_hz, uint32 duration_ms);
void audio_set_master_volume(uint8 volume);
uint8 audio_get_master_volume(void);

#ifdef __cplusplus
}
#endif

#endif
