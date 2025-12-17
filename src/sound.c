#include "include/sound.h"
#include "include/vfs.h"
#include "include/screen.h"
#include "include/libc/string.h"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

typedef SoundDriver* (*sound_driver_factory_t)(void);

static SoundDriver* g_active_driver = 0;

#pragma pack(push, 1)
typedef struct {
    char riff_id[4];
    uint32 chunk_size;
    char wave_id[4];
} wav_header_t;

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
#pragma pack(pop)

static SoundDriver* fallback_pc_driver(void) {
    SoundDriver* driver = sound_pc_speaker_driver();
    if (!driver) {
        return 0;
    }
    if (driver->detect) {
        driver->detect(driver);
    }
    if (driver->init) {
        driver->init(driver);
    }
    return driver;
}

static bool parse_wav(const uint8* data, uint32 size, const uint8** audio_data,
                      uint32* audio_size, SoundFormat* format) {
    if (!data || size < sizeof(wav_header_t) || !audio_data || !audio_size || !format) {
        return false;
    }

    const wav_header_t* header = (const wav_header_t*)data;
    if (memcmp(header->riff_id, "RIFF", 4) != 0 || memcmp(header->wave_id, "WAVE", 4) != 0) {
        return false;
    }

    const uint8* ptr = data + sizeof(wav_header_t);
    const uint8* end = data + size;
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
                return false;
            }
            const wav_fmt_chunk_t* fmt = (const wav_fmt_chunk_t*)ptr;
            if (fmt->audio_format != 1) { // PCM only
                return false;
            }
            format->sample_rate = fmt->sample_rate;
            format->channels = fmt->num_channels;
            format->bits_per_sample = fmt->bits_per_sample;
            format->signed_samples = (fmt->bits_per_sample == 16);
            have_fmt = true;
        } else if (memcmp(chunk->id, "data", 4) == 0) {
            *audio_data = ptr;
            *audio_size = chunk->size;
            have_data = true;
        }

        uint32 advance = chunk->size;
        ptr += advance;
        if (advance & 1) {
            ptr++;
        }
    }

    return have_fmt && have_data;
}

static void log_driver_failure(const char* driver_name, const char* reason) {
    print("[SOUND] ");
    print(driver_name ? driver_name : "Unknown");
    print(" unavailable: ");
    print(reason ? reason : "no reason given");
    print("\n");
}

bool sound_system_init(void) {
    if (g_active_driver) {
        return true;
    }

    sound_driver_factory_t factories[] = {
        sound_hda_driver,
        sound_ac97_driver,
        sound_ensoniq_driver,
        sound_sb16_driver,
        sound_pc_speaker_driver
    };

    const uint32 factory_count = sizeof(factories) / sizeof(factories[0]);

    for (uint32 i = 0; i < factory_count; i++) {
        SoundDriver* driver = factories[i] ? factories[i]() : 0;
        if (!driver) {
            continue;
        }
        bool detected = true;
        if (driver->detect) {
            detected = driver->detect(driver);
        }
        if (!detected) {
            continue;
        }
        if (!driver->init || !driver->init(driver)) {
            log_driver_failure(driver->name, "init failed");
            continue;
        }

        g_active_driver = driver;
        print("[SOUND] Active driver: ");
        print(driver->name);
        print("\n");
        return true;
    }

    print("[SOUND] No usable sound devices detected.\n");
    return false;
}

void sound_shutdown(void) {
    if (g_active_driver && g_active_driver->shutdown) {
        g_active_driver->shutdown(g_active_driver);
    }
    g_active_driver = 0;
}

const SoundDriver* sound_active_driver(void) {
    return g_active_driver;
}

bool sound_play_wav(const char* path) {
    if (!path) {
        return false;
    }

    if (!g_active_driver && !sound_system_init()) {
        return false;
    }

    if (!g_active_driver || !g_active_driver->play_pcm) {
        return false;
    }

    const uint8* file_data = 0;
    uint32 file_size = 0;
    if (!vfs_read_file(path, &file_data, &file_size)) {
        print("[SOUND] Failed to read file: ");
        print(path);
        print("\n");
        return false;
    }

    const uint8* audio_data = 0;
    uint32 audio_size = 0;
    SoundFormat format;
    if (!parse_wav(file_data, file_size, &audio_data, &audio_size, &format)) {
        print("[SOUND] Unsupported WAV format: ");
        print(path);
        print("\n");
        return false;
    }

    if (!g_active_driver->play_pcm(g_active_driver, audio_data, audio_size, &format)) {
        print("[SOUND] Driver failed to play PCM data.\n");
        return false;
    }
    return true;
}

void sound_beep(uint32 frequency_hz, uint32 duration_ms) {
    if (!g_active_driver || !g_active_driver->beep) {
        SoundDriver* fallback = fallback_pc_driver();
        if (fallback && fallback->beep) {
            fallback->beep(fallback, frequency_hz, duration_ms);
        }
        return;
    }
    g_active_driver->beep(g_active_driver, frequency_hz, duration_ms);
}

void sound_set_volume(uint8 volume) {
    if (!g_active_driver) {
        return;
    }
    g_active_driver->volume = volume;
    if (g_active_driver->set_volume) {
        g_active_driver->set_volume(g_active_driver, volume);
    }
}
