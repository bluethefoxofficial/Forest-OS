#include "include/sound.h"
#include "include/io_ports.h"
#include "include/timer.h"

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43
#define SPEAKER_PORT 0x61

typedef struct {
    bool initialized;
} pcspk_state_t;

static bool pcspk_detect(SoundDriver* driver) {
    (void)driver;
    return true;
}

static bool pcspk_init(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    pcspk_state_t* state = (pcspk_state_t*)driver->state;
    if (!state) {
        static pcspk_state_t static_state;
        state = &static_state;
        driver->state = state;
    }
    state->initialized = true;
    return true;
}

static void pcspk_tone(uint32 frequency_hz) {
    if (frequency_hz == 0) {
        return;
    }
    uint32 divisor = 1193180 / frequency_hz;
    outportb(PIT_COMMAND, 0xB6);
    outportb(PIT_CHANNEL2, (uint8)(divisor & 0xFF));
    outportb(PIT_CHANNEL2, (uint8)((divisor >> 8) & 0xFF));
    uint8 tmp = inportb(SPEAKER_PORT);
    if ((tmp & 3) != 3) {
        outportb(SPEAKER_PORT, tmp | 3);
    }
}

static void pcspk_off(void) {
    uint8 tmp = inportb(SPEAKER_PORT) & 0xFC;
    outportb(SPEAKER_PORT, tmp);
}

static bool pcspk_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    (void)driver;
    (void)data;
    (void)length;
    (void)format;
    return false;
}

static void pcspk_set_volume(SoundDriver* driver, uint8 value) {
    (void)driver;
    (void)value;
}

static void pcspk_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    (void)driver;
    if (frequency_hz == 0 || duration_ms == 0) {
        return;
    }
    pcspk_tone(frequency_hz);
    uint32 start = timer_get_ticks();
    while ((timer_get_ticks() - start) * 10 < duration_ms) {
    }
    pcspk_off();
}

static void pcspk_shutdown(SoundDriver* driver) {
    (void)driver;
    pcspk_off();
}

static SoundDriver g_pcspk_driver = {
    .name = "PC Speaker",
    .type = SOUND_DEVICE_PC_SPEAKER,
    .detect = pcspk_detect,
    .init = pcspk_init,
    .play_pcm = pcspk_play_pcm,
    .set_volume = pcspk_set_volume,
    .beep = pcspk_beep,
    .shutdown = pcspk_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_pc_speaker_driver(void) {
    return &g_pcspk_driver;
}
