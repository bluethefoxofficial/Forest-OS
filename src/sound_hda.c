#include "include/sound.h"
#include "include/pci.h"
#include "include/io_ports.h"
#include "include/screen.h"

#define HDA_REG_GCAP   0x00
#define HDA_REG_CORBCTL 0x4C
#define HDA_REG_CORBSIZE 0x4E
#define HDA_REG_CORBLBASE 0x40
#define HDA_REG_CORBUBASE 0x44
#define HDA_REG_CORBWP 0x48
#define HDA_REG_CORBRP 0x4A
#define HDA_REG_RIRBCTL 0x5C
#define HDA_REG_RIRBSIZE 0x5E
#define HDA_REG_RIRBLBASE 0x50
#define HDA_REG_RIRBUBASE 0x54
#define HDA_REG_RIRBWP 0x58
#define HDA_REG_RINTCNT 0x5A

#define HDA_CORBCTL_RUN  (1 << 1)
#define HDA_RIRBCTL_RUN  (1 << 1)

typedef struct {
    pci_device_t pci;
    volatile uint32* mmio_base;
    bool initialized;
} hda_state_t;

static bool hda_detect(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    hda_state_t* state = (hda_state_t*)driver->state;
    if (!state) {
        static hda_state_t static_state;
        state = &static_state;
        driver->state = state;
    }

    pci_device_t device;
    if (!pci_find_by_class(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HD_AUDIO, &device)) {
        return false;
    }

    state->pci = device;
    uint32 bar0 = device.bar[0];
    if (!(bar0 & 0x1)) {
        state->mmio_base = (volatile uint32*)(bar0 & ~0xF);
    } else {
        return false;
    }
    return true;
}

static bool hda_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    hda_state_t* state = (hda_state_t*)driver->state;
    if (!state->mmio_base) {
        return false;
    }
    state->initialized = true;
    return true;
}

static bool hda_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    (void)driver;
    (void)data;
    (void)length;
    (void)format;
    return false;
}

static void hda_set_volume(SoundDriver* driver, uint8 volume) {
    (void)driver;
    (void)volume;
}

static void hda_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    (void)driver;
    (void)frequency_hz;
    (void)duration_ms;
}

static void hda_shutdown(SoundDriver* driver) {
    (void)driver;
}

static SoundDriver g_hda_driver = {
    .name = "Intel HDA (stub)",
    .type = SOUND_DEVICE_HDA,
    .detect = hda_detect,
    .init = hda_init,
    .play_pcm = hda_play_pcm,
    .set_volume = hda_set_volume,
    .beep = hda_beep,
    .shutdown = hda_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_hda_driver(void) {
    return &g_hda_driver;
}
