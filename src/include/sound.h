
#ifndef SOUND_H
#define SOUND_H

#include "types.h"
#include <stdbool.h>

typedef enum {
    SOUND_DEVICE_NONE = 0,
    SOUND_DEVICE_PC_SPEAKER,
    SOUND_DEVICE_SOUND_BLASTER16,
    SOUND_DEVICE_AC97,
    SOUND_DEVICE_HDA,
    SOUND_DEVICE_ENSONIQ_AUDIOPCI
} SoundDeviceType;

typedef struct {
    uint32 sample_rate;
    uint16 channels;
    uint16 bits_per_sample;
    bool   signed_samples;
} SoundFormat;

typedef struct SoundDriver {
    const char* name;
    SoundDeviceType type;
    bool (*detect)(struct SoundDriver* driver);
    bool (*init)(struct SoundDriver* driver);
    bool (*play_pcm)(struct SoundDriver* driver,
                     const uint8* data,
                     uint32 length,
                     const SoundFormat* format);
    void (*set_volume)(struct SoundDriver* driver, uint8 volume);
    void (*beep)(struct SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms);
    void (*shutdown)(struct SoundDriver* driver);
    void* state;
    uint8 volume;
} SoundDriver;

bool sound_system_init(void);
void sound_shutdown(void);
const SoundDriver* sound_active_driver(void);
bool sound_play_wav(const char* path);
void sound_beep(uint32 frequency_hz, uint32 duration_ms);
void sound_set_volume(uint8 volume);

// Individual driver factories
SoundDriver* sound_pc_speaker_driver(void);
SoundDriver* sound_sb16_driver(void);
SoundDriver* sound_ac97_driver(void);
SoundDriver* sound_hda_driver(void);
SoundDriver* sound_ensoniq_driver(void);

#endif
