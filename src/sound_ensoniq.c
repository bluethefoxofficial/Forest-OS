#include "include/sound.h"
#include "include/pci.h"
#include "include/io_ports.h"
#include "include/screen.h"

#define ES1371_SERIAL_INTERFACE 0x20
#define ES1371_CONTROL          0x00
#define ES1371_STATUS           0x04
#define ES1371_CODEC_RW         0x14

#define ES1371_PLAYBACK2_ADDR   0x38
#define ES1371_PLAYBACK2_LENGTH 0x3C
#define ES1371_PLAYBACK2_FRAMES 0x28

#define ES1371_DEFAULT_SERIAL_CFG 0x0020020C
#define ES1371_CONTROL_DAC2      0x00000020

typedef struct {
    pci_device_t pci;
    uint32 base_io;
    bool initialized;
} es1371_state_t;

static inline void es1371_write32(es1371_state_t* state, uint32 offset, uint32 value) {
    outportd((uint16)(state->base_io + offset), value);
}

static inline uint32 es1371_read32(es1371_state_t* state, uint32 offset) {
    return inportd((uint16)(state->base_io + offset));
}

static bool es1371_detect(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;
    if (!state) {
        static es1371_state_t static_state;
        state = &static_state;
        driver->state = state;
    }

    pci_device_t device;
    if (!pci_find_by_vendor_device(PCI_VENDOR_ENSONIQ, PCI_DEVICE_ES1371, &device)) {
        return false;
    }

    state->pci = device;
    state->base_io = device.bar[0] & ~0x1;
    if (!state->base_io) {
        return false;
    }
    return true;
}

static bool es1371_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;

    es1371_write32(state, ES1371_STATUS, 0x20);

    es1371_write32(state, ES1371_CODEC_RW, 0x7F7F);

    state->initialized = true;
    return true;
}

static bool es1371_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    if (!driver || !driver->state || !data || !format) {
        return false;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }

    uint32 frames = length / (format->channels * (format->bits_per_sample / 8));
    if (frames == 0) {
        return false;
    }

    es1371_write32(state, ES1371_PLAYBACK2_ADDR, (uint32)data);
    es1371_write32(state, ES1371_PLAYBACK2_LENGTH, (length / 4) - 1);
    es1371_write32(state, ES1371_PLAYBACK2_FRAMES, frames - 1);

    es1371_write32(state, ES1371_SERIAL_INTERFACE, ES1371_DEFAULT_SERIAL_CFG);
    es1371_write32(state, ES1371_CONTROL, ES1371_CONTROL_DAC2);

    return true;
}

static void es1371_set_volume(SoundDriver* driver, uint8 volume) {
    (void)driver;
    (void)volume;
}

static void es1371_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    (void)driver;
    (void)frequency_hz;
    (void)duration_ms;
}

static void es1371_shutdown(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;
    es1371_write32(state, ES1371_CONTROL, 0);
}

static SoundDriver g_ensoniq_driver = {
    .name = "Ensoniq AudioPCI",
    .type = SOUND_DEVICE_ENSONIQ_AUDIOPCI,
    .detect = es1371_detect,
    .init = es1371_init,
    .play_pcm = es1371_play_pcm,
    .set_volume = es1371_set_volume,
    .beep = es1371_beep,
    .shutdown = es1371_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_ensoniq_driver(void) {
    return &g_ensoniq_driver;
}
