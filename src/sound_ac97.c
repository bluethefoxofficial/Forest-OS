#include "include/sound.h"
#include "include/pci.h"
#include "include/io_ports.h"
#include "include/screen.h"

#define AC97_NAM_OFFSET_MASTER_VOL 0x02
#define AC97_NAM_OFFSET_PCM_VOL    0x18
#define AC97_NAM_OFFSET_POWER      0x26
#define AC97_NABM_OFFSET_PO_BDBAR  0x10
#define AC97_NABM_OFFSET_PO_CIV    0x14
#define AC97_NABM_OFFSET_PO_LVI    0x15
#define AC97_NABM_OFFSET_PO_SR     0x16
#define AC97_NABM_OFFSET_PO_PICB   0x18
#define AC97_NABM_OFFSET_PO_CR     0x1B

#define AC97_PO_CR_RUN             0x01
#define AC97_PO_CR_RESET           0x02

#define AC97_SR_DCH                0x01
#define AC97_SR_LVBCI              0x04

#define AC97_BUFFER_ENTRIES        32

typedef struct {
    uint32 phys_addr;
    uint16 length;
    uint16 flags;
} __attribute__((packed)) ac97_bdl_entry_t;

typedef struct {
    pci_device_t pci;
    uint16 nam_base;
    uint16 nabm_base;
    ac97_bdl_entry_t bdl[AC97_BUFFER_ENTRIES];
    bool initialized;
} ac97_state_t;

static uint16 ac97_read_nam(ac97_state_t* state, uint8 reg) {
    return inportw(state->nam_base + reg);
}

static void ac97_write_nam(ac97_state_t* state, uint8 reg, uint16 value) {
    outportw(state->nam_base + reg, value);
}

static uint16 ac97_read_nabm_word(ac97_state_t* state, uint8 reg) {
    return inportw(state->nabm_base + reg);
}

static void ac97_write_nabm_word(ac97_state_t* state, uint8 reg, uint16 value) {
    outportw(state->nabm_base + reg, value);
}

static void ac97_write_nabm_byte(ac97_state_t* state, uint8 reg, uint8 value) {
    outportb(state->nabm_base + reg, value);
}

static bool ac97_detect(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    if (!state) {
        static ac97_state_t static_state;
        state = &static_state;
        driver->state = state;
    }

    pci_device_t device;
    if (!pci_find_by_class(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AUDIO, &device)) {
        return false;
    }

    state->pci = device;
    state->nam_base = (uint16)(device.bar[0] & ~0x1);
    state->nabm_base = (uint16)(device.bar[1] & ~0x1);

    if (!state->nam_base || !state->nabm_base) {
        return false;
    }

    return true;
}

static void ac97_reset_channel(ac97_state_t* state) {
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, AC97_PO_CR_RESET);
    for (volatile uint32 i = 0; i < 10000; i++) {
        __asm__ volatile("nop");
    }
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, 0);
}

static bool ac97_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;

    ac97_write_nam(state, AC97_NAM_OFFSET_MASTER_VOL, 0x0000);
    ac97_write_nam(state, AC97_NAM_OFFSET_PCM_VOL, 0x0000);

    state->initialized = true;
    ac97_reset_channel(state);
    ac97_write_nam(state, AC97_NAM_OFFSET_POWER, 0x0000);

    outportd(state->nabm_base + AC97_NABM_OFFSET_PO_BDBAR, (uint32)&state->bdl[0]);
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_LVI, AC97_BUFFER_ENTRIES - 1);

    return true;
}

static void ac97_program_bdl(ac97_state_t* state, const uint8* data, uint32 length) {
    for (uint32 i = 0; i < AC97_BUFFER_ENTRIES; i++) {
        uint32 chunk = length / AC97_BUFFER_ENTRIES;
        if (chunk < 1) {
            chunk = length;
        }
        state->bdl[i].phys_addr = (uint32)(data + (i * chunk));
        state->bdl[i].length = (uint16)chunk;
        state->bdl[i].flags = (i == AC97_BUFFER_ENTRIES - 1) ? 0x4000 : 0x0000;
    }
}

static bool ac97_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    if (!driver || !driver->state || !data || !format) {
        return false;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }
    if (format->bits_per_sample != 16) {
        return false;
    }

    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, 0);
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CIV, 0);

    ac97_program_bdl(state, data, length);

    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, AC97_PO_CR_RUN);
    return true;
}

static void ac97_set_volume(SoundDriver* driver, uint8 volume) {
    if (!driver || !driver->state) {
        return;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    uint16 value = (uint16)((volume >> 1) & 0x1F);
    uint16 reg_value = (value << 8) | value;
    ac97_write_nam(state, AC97_NAM_OFFSET_PCM_VOL, reg_value);
}

static void ac97_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    (void)driver;
    (void)frequency_hz;
    (void)duration_ms;
}

static void ac97_shutdown(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, 0);
}

static SoundDriver g_ac97_driver = {
    .name = "AC'97",
    .type = SOUND_DEVICE_AC97,
    .detect = ac97_detect,
    .init = ac97_init,
    .play_pcm = ac97_play_pcm,
    .set_volume = ac97_set_volume,
    .beep = ac97_beep,
    .shutdown = ac97_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_ac97_driver(void) {
    return &g_ac97_driver;
}
