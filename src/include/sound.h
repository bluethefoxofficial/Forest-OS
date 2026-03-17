
#ifndef SOUND_H
#define SOUND_H

#include "types.h"
#include <stdbool.h>

typedef enum {
    SOUND_DEVICE_NONE = 0,
    SOUND_DEVICE_PC_SPEAKER,
    SOUND_DEVICE_SOUND_BLASTER16,
    SOUND_DEVICE_SOUND_BLASTER_PRO,
    SOUND_DEVICE_AC97,
    SOUND_DEVICE_HDA,
    SOUND_DEVICE_ENSONIQ_AUDIOPCI,
    SOUND_DEVICE_OPL3,
    SOUND_DEVICE_USB_AUDIO,
    SOUND_DEVICE_UNIVERSAL
} SoundDeviceType;

typedef enum {
    PCM_U8 = 1,
    PCM_S16 = 2,
    PCM_F32 = 3
} PcmFormat;

typedef struct {
    PcmFormat format;
    uint16_t channels;
    uint32_t sample_rate;
} PcmDesc;

typedef struct {
    uint32 sample_rate;
    uint16 channels;
    uint16 bits_per_sample;
    bool   signed_samples;
} SoundFormat;

typedef struct {
    PcmFormat supported_formats[4]; // Array of supported PCM formats
    uint32 max_channels;            // Maximum channels supported
    uint32 native_sample_rates[8];  // Supported sample rates
    bool stereo_supported;          // Whether stereo is supported
    bool little_endian;             // Endianness requirement
    uint32 max_buffer_size;         // Maximum DMA buffer size in bytes
} DeviceCapabilities;

typedef struct SoundDriver {
    const char* name;
    SoundDeviceType type;
    bool (*detect)(struct SoundDriver* driver);
    bool (*init)(struct SoundDriver* driver);
    bool (*play_pcm)(struct SoundDriver* driver,
                      const uint8* data,
                      uint32 length,
                      const SoundFormat* format);
    bool (*get_capabilities)(struct SoundDriver* driver, DeviceCapabilities* caps);
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
SoundDriver* sound_sbpro_driver(void);
SoundDriver* sound_ac97_driver(void);
SoundDriver* sound_hda_driver(void);
SoundDriver* sound_ensoniq_driver(void);
SoundDriver* sound_opl3_driver(void);
SoundDriver* sound_usb_sound_driver(void);
SoundDriver* sound_universal_driver(void);

#endif
