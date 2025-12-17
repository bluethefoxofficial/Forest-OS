#include "include/sound.h"
#include "include/io_ports.h"
#include "include/timer.h"
#include "include/screen.h"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define SB16_DEFAULT_BASE 0x220
#define SB16_RESET        0x226
#define SB16_READ         0x22A
#define SB16_WRITE        0x22C
#define SB16_READ_STATUS  0x22E
#define SB16_MIXER_ADDR   0x224
#define SB16_MIXER_DATA   0x225

#define DMA8_MASK         0x0A
#define DMA8_MODE         0x0B
#define DMA8_CLEAR_FF     0x0C
#define DMA8_BASE_ADDR    0x02
#define DMA8_COUNT        0x03
#define DMA8_PAGE         0x83

#define DMA16_MASK        0xD4
#define DMA16_MODE        0xD6
#define DMA16_CLEAR_FF    0xD8
#define DMA16_BASE_ADDR   0xC4
#define DMA16_COUNT       0xC6
#define DMA16_PAGE        0x8B

#define DSP_CMD_SET_SAMPLE_RATE 0x41
#define DSP_CMD_SET_TIME_CONST  0x40
#define DSP_CMD_SPEAKER_ON      0xD1
#define DSP_CMD_SPEAKER_OFF     0xD3
#define DSP_CMD_DMA_8BIT        0xC0
#define DSP_CMD_DMA_16BIT       0xB0

typedef struct {
    uint16 base_port;
    bool initialized;
} sb16_state_t;

static inline void sb16_write(uint16 port, uint8 value) {
    outportb(port, value);
}

static inline uint8 sb16_read(uint16 port) {
    return inportb(port);
}

static bool sb16_reset_dsp(uint16 base) {
    outportb(base + (SB16_RESET - SB16_DEFAULT_BASE), 1);
    for (volatile int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }
    outportb(base + (SB16_RESET - SB16_DEFAULT_BASE), 0);
    for (uint32 timeout = 0; timeout < 0xFFFF; timeout++) {
        if (inportb(base + (SB16_READ_STATUS - SB16_DEFAULT_BASE)) & 0x80) {
            if (inportb(base + (SB16_READ - SB16_DEFAULT_BASE)) == 0xAA) {
                return true;
            }
        }
    }
    return false;
}

static bool sb16_detect(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    if (!state) {
        static sb16_state_t static_state;
        state = &static_state;
        driver->state = state;
    }
    state->base_port = SB16_DEFAULT_BASE;
    if (!sb16_reset_dsp(state->base_port)) {
        return false;
    }
    return true;
}

static bool sb16_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x22);
    sb16_write(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE), 0xFF);
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x80);
    sb16_write(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE), 0x02);
    state->initialized = true;
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SPEAKER_ON);
    return true;
}

static void sb16_program_dma16(const uint8* data, uint32 length_bytes) {
    outportb(DMA16_MASK, 0x05);
    outportb(DMA16_CLEAR_FF, 0);
    outportb(DMA16_MODE, 0x58);

    uint32 addr = (uint32)data;
    uint16 count = (uint16)(length_bytes - 1);

    outportb(DMA16_PAGE, (addr >> 16) & 0xFF);
    outportb(DMA16_BASE_ADDR, addr & 0xFF);
    outportb(DMA16_BASE_ADDR, (addr >> 8) & 0xFF);
    outportb(DMA16_COUNT, count & 0xFF);
    outportb(DMA16_COUNT, (count >> 8) & 0xFF);

    outportb(DMA16_MASK, 0x01);
}

static bool sb16_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    if (!driver || !driver->state || !data || !format) {
        return false;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SET_SAMPLE_RATE);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((format->sample_rate >> 8) & 0xFF));
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(format->sample_rate & 0xFF));

    uint32 length_samples = length / (format->bits_per_sample / 8);
    if (length_samples == 0) {
        return false;
    }

    sb16_program_dma16(data, length);

    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_DMA_16BIT);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((length_samples - 1) & 0xFF));
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(((length_samples - 1) >> 8) & 0xFF));
    return true;
}

static void sb16_set_volume(SoundDriver* driver, uint8 volume) {
    if (!driver || !driver->state) {
        return;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    uint8 vol = (uint8)((volume >> 4) & 0x0F);
    uint8 packed = (vol << 4) | vol;
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x22);
    sb16_write(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE), packed);
}

static void sb16_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    if (!driver || !driver->state) {
        return;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    uint8 sample = 0x7F;
    uint32 rate = MIN(frequency_hz * 2, 48000);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SET_SAMPLE_RATE);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((rate >> 8) & 0xFF));
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(rate & 0xFF));
    sb16_program_dma16(&sample, 1);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_DMA_8BIT);
    uint32 start = timer_get_ticks();
    while ((timer_get_ticks() - start) * 10 < duration_ms) {
    }
}

static void sb16_shutdown(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SPEAKER_OFF);
    state->initialized = false;
}

static SoundDriver g_sb16_driver = {
    .name = "Sound Blaster 16",
    .type = SOUND_DEVICE_SOUND_BLASTER16,
    .detect = sb16_detect,
    .init = sb16_init,
    .play_pcm = sb16_play_pcm,
    .set_volume = sb16_set_volume,
    .beep = sb16_beep,
    .shutdown = sb16_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_sb16_driver(void) {
    return &g_sb16_driver;
}
