#include "include/libc/audio.h"
#include "include/system.h"
#include "include/util.h"

static audio_driver_hooks_t g_audio_hooks = {0};
static uint8 g_master_volume = 100; // percentage

static bool format_is_supported(const audio_format_t* format) {
    if (!format) {
        return false;
    }
    if (format->channels == 0 || format->channels > 2) {
        return false;
    }
    if (format->bits_per_sample != 8 && format->bits_per_sample != 16) {
        return false;
    }
    if (format->sample_rate < 8000 || format->sample_rate > 192000) {
        return false;
    }
    return true;
}

bool audio_register_driver(const audio_driver_hooks_t* hooks) {
    if (!hooks || !hooks->output) {
        return false;
    }

    g_audio_hooks = *hooks;
    return true;
}

void audio_unregister_driver(void) {
    memory_set((uint8*)&g_audio_hooks, 0, sizeof(g_audio_hooks));
}

bool audio_driver_ready(void) {
    return g_audio_hooks.output != 0;
}

bool audio_play_pcm(const uint8* data, uint32 length, const audio_format_t* format) {
    if (!audio_driver_ready() || !data || length == 0 || !format_is_supported(format)) {
        return false;
    }

    return g_audio_hooks.output(data, length, format, g_audio_hooks.context);
}

void audio_beep(uint32 frequency_hz, uint32 duration_ms) {
    if (g_audio_hooks.beep) {
        g_audio_hooks.beep(frequency_hz, duration_ms, g_audio_hooks.context);
    }
}

void audio_set_master_volume(uint8 volume) {
    g_master_volume = (volume > 100) ? 100 : volume;
}

uint8 audio_get_master_volume(void) {
    return g_master_volume;
}
