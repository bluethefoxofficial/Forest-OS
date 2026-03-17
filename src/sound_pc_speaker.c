#include "include/sound.h"
#include "include/io_ports.h"
#include "include/timer.h"
#include "include/libc/string.h"
#include "include/cpu_ops.h"
#include "include/memory.h"

#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43
#define SPEAKER_PORT 0x61

#define PCSPK_SAMPLE_RATE 22050
#define PCSPK_BUFFER_SIZE 4096

typedef struct {
    bool initialized;
    bool playing;
    uint8_t* sample_buffer;
    uint32_t buffer_size;
    uint32_t current_pos;
    uint32_t sample_rate;
    bool pwm_mode;
    uint32_t pwm_counter;
    uint8_t pwm_period;
} pcspk_state_t;

static pcspk_state_t g_pcspk_state = {0};
static bool g_pcspk_pit_initialized = false;

static void pcspk_pit_init(void) {
    if (g_pcspk_pit_initialized) return;
    
    outportb(PIT_COMMAND, 0xB6);
    outportb(PIT_CHANNEL2, 0xFF);
    outportb(PIT_CHANNEL2, 0xFF);
    
    g_pcspk_pit_initialized = true;
}

static void pcspk_pwm_output(uint8_t sample) {
    uint32_t divisor = 1193180 / PCSPK_SAMPLE_RATE;
    
    outportb(PIT_COMMAND, 0x92);
    outportb(PIT_CHANNEL2, (uint8)(divisor & 0xFF));
    outportb(PIT_CHANNEL2, (uint8)((divisor >> 8) & 0xFF));
    
    uint8_t tmp = inportb(SPEAKER_PORT);
    outportb(SPEAKER_PORT, tmp | 3);
}

static bool pcspk_detect(SoundDriver* driver) {
    (void)driver;
    return true;
}

static bool pcspk_init(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    pcspk_state_t* state = &g_pcspk_state;
    memset(state, 0, sizeof(pcspk_state_t));
    
    state->sample_buffer = (uint8_t*)kmalloc(PCSPK_BUFFER_SIZE);
    if (!state->sample_buffer) {
        return false;
    }
    
    state->buffer_size = PCSPK_BUFFER_SIZE;
    state->sample_rate = PCSPK_SAMPLE_RATE;
    state->initialized = true;
    driver->state = state;
    
    pcspk_pit_init();
    
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
    pcspk_state_t* state = &g_pcspk_state;
    if (!state->initialized || !data || !format) {
        return false;
    }
    
    if (length > state->buffer_size) {
        length = state->buffer_size;
    }
    
    memcpy(state->sample_buffer, data, length);
    state->current_pos = 0;
    state->buffer_size = length;
    state->playing = true;
    
    if (format->bits_per_sample == 16 && format->channels == 2) {
        state->pwm_mode = false;
    } else if (format->bits_per_sample == 8) {
        state->pwm_mode = true;
        for (uint32 i = 0; i < length; i++) {
            int16_t sample = ((int8_t)(state->sample_buffer[i] - 128)) * 256;
            state->sample_buffer[i] = (sample >> 8) & 0xFF;
        }
    } else {
        state->pwm_mode = true;
    }
    
    pcspk_pwm_output(state->sample_buffer[0]);
    
    return true;
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
    timer_sleep_ms(duration_ms);
    pcspk_off();
}

static bool pcspk_get_capabilities(SoundDriver* driver, DeviceCapabilities* caps) {
    (void)driver;
    if (!caps) {
        return false;
    }
    
    memset(caps, 0, sizeof(DeviceCapabilities));
    caps->supported_formats[0] = PCM_U8;
    caps->supported_formats[1] = 0;
    caps->max_channels = 1;
    caps->stereo_supported = false;
    caps->little_endian = true;
    caps->native_sample_rates[0] = PCSPK_SAMPLE_RATE;
    caps->native_sample_rates[1] = 0;
    caps->max_buffer_size = PCSPK_BUFFER_SIZE;
    
    return true;
}

static void pcspk_shutdown(SoundDriver* driver) {
    (void)driver;
    pcspk_off();
    
    pcspk_state_t* state = &g_pcspk_state;
    if (state->sample_buffer) {
        kfree(state->sample_buffer);
        state->sample_buffer = NULL;
    }
    state->initialized = false;
    state->playing = false;
}

static SoundDriver g_pcspk_driver = {
    .name = "PC Speaker",
    .type = SOUND_DEVICE_PC_SPEAKER,
    .detect = pcspk_detect,
    .init = pcspk_init,
    .play_pcm = pcspk_play_pcm,
    .get_capabilities = pcspk_get_capabilities,
    .set_volume = pcspk_set_volume,
    .beep = pcspk_beep,
    .shutdown = pcspk_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_pc_speaker_driver(void) {
    return &g_pcspk_driver;
}
