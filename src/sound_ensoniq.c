#include "include/sound.h"
#include "include/pci.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/memory.h"
#include "include/bitmap_pmm.h"
#include "include/debuglog.h"
#include "include/cpu_ops.h"
#include "include/libc/string.h"

#define ES1371_SERIAL_INTERFACE 0x20
#define ES1371_CONTROL          0x00
#define ES1371_STATUS           0x04
#define ES1371_SAMPLE_RATE_CONV 0x10
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
    uint8* dma_buffer_virt;
    uint32 dma_buffer_phys;
    uint32 dma_buffer_size;
    uint32 dma_buffer_frame;
    uint32 dma_buffer_pages;
} es1371_state_t;

static inline void es1371_write32(es1371_state_t* state, uint32 offset, uint32 value) {
    outportd((uint16)(state->base_io + offset), value);
}

static inline uint32 es1371_read32(es1371_state_t* state, uint32 offset) {
    return inportd((uint16)(state->base_io + offset));
}

static void es1371_set_sample_rate(es1371_state_t* state, uint32 rate) {
    uint32 frequency = (rate << 16) / 3000;
    es1371_write32(state, ES1371_SAMPLE_RATE_CONV, 0x75); // Select register 0x75
    es1371_write32(state, ES1371_SAMPLE_RATE_CONV, (frequency >> 6) & 0xFC00);
    es1371_write32(state, ES1371_SAMPLE_RATE_CONV, 0x77); // Select register 0x77
    es1371_write32(state, ES1371_SAMPLE_RATE_CONV, frequency >> 1);
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

    // Enable bus mastering
    uint16 command = pci_config_read16(device.segment, device.bus, device.device, device.function, 4);
    command |= 0x0004; // Bus master enable
    pci_config_write16(device.segment, device.bus, device.device, device.function, 4, command);

    return true;
}

static bool es1371_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;

    es1371_write32(state, ES1371_STATUS, 0x20);

    es1371_write32(state, ES1371_CODEC_RW, 0x7F7F);

    state->dma_buffer_size = 64 * 1024;
    state->dma_buffer_pages = (state->dma_buffer_size + MEMORY_PAGE_SIZE - 1) / MEMORY_PAGE_SIZE;
    state->dma_buffer_frame = bitmap_pmm_alloc_pages(state->dma_buffer_pages, PMM_ALLOC_LOW_MEMORY);
    if (state->dma_buffer_frame == 0) {
        debuglog(DEBUG_ERROR, "ES1371: Failed to allocate DMA pages\n");
        return false;
    }

    state->dma_buffer_phys = state->dma_buffer_frame * MEMORY_PAGE_SIZE;
    state->dma_buffer_virt = (uint8*)mm_map_physical_page(state->dma_buffer_phys, 0);
    if (!state->dma_buffer_virt) {
        debuglog(DEBUG_ERROR, "ES1371: Failed to map DMA buffer\n");
        bitmap_pmm_free_pages(state->dma_buffer_frame, state->dma_buffer_pages);
        state->dma_buffer_frame = 0;
        state->dma_buffer_pages = 0;
        return false;
    }
    memset(state->dma_buffer_virt, 0, state->dma_buffer_size);

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

    // Set sample rate
    es1371_set_sample_rate(state, format->sample_rate);

    if (!state->dma_buffer_virt || state->dma_buffer_phys == 0 || state->dma_buffer_size == 0) {
        return false;
    }

    if (length > state->dma_buffer_size) {
        length = state->dma_buffer_size;
    }
    if (length < 4) {
        return false;
    }

    memory_copy((const char*)data, (char*)state->dma_buffer_virt, length);

    uint32 frames = length / (format->channels * (format->bits_per_sample / 8));
    if (frames == 0) {
        return false;
    }

    // Set serial interface based on format
    uint32 serial_cfg = 0x0020020C; // Default: 16-bit stereo
    if (format->bits_per_sample == 8) {
        serial_cfg = 0x0000000C; // 8-bit stereo
    }
    if (format->channels == 1) {
        serial_cfg &= ~0x00100000; // Mono
    }
    es1371_write32(state, ES1371_SERIAL_INTERFACE, serial_cfg);

    es1371_write32(state, ES1371_PLAYBACK2_ADDR, state->dma_buffer_phys);
    es1371_write32(state, ES1371_PLAYBACK2_LENGTH, (length / 4) - 1);
    es1371_write32(state, ES1371_PLAYBACK2_FRAMES, frames - 1);

    es1371_write32(state, ES1371_CONTROL, ES1371_CONTROL_DAC2);

    // For streaming, don't wait for completion - just start playback and return
    // The caller handles timing between chunks
    return true;
}

static void es1371_set_volume(SoundDriver* driver, uint8 volume) {
    if (!driver || !driver->state) {
        return;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;
    if (!state->initialized) {
        return;
    }

    // Convert 0-255 to 0-127 for CODEC
    uint8 codec_vol = volume / 2;
    uint16 vol_reg = (codec_vol << 8) | codec_vol; // Left and right
    es1371_write32(state, ES1371_CODEC_RW, vol_reg);
}

static void es1371_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    (void)driver;
    (void)frequency_hz;
    (void)duration_ms;
}

static bool es1371_get_capabilities(SoundDriver* driver, DeviceCapabilities* caps) {
    if (!driver || !caps) {
        return false;
    }

    memset(caps, 0, sizeof(DeviceCapabilities));

    // Ensoniq capabilities: 16-bit signed PCM preferred, stereo, 44100/48000 Hz, little endian
    caps->supported_formats[0] = PCM_S16;
    caps->supported_formats[1] = PCM_U8;
    caps->max_channels = 2; // Stereo
    caps->native_sample_rates[0] = 44100;
    caps->native_sample_rates[1] = 48000;
    caps->stereo_supported = true;
    caps->little_endian = true;
    caps->max_buffer_size = 65535; // DMA limit

    return true;
}

static int16_t pcm_f32_bits_to_s16(uint32 bits) {
    uint32 sign = bits >> 31;
    uint32 exp = (bits >> 23) & 0xFF;
    uint32 mant = bits & 0x7FFFFF;

    if (exp == 0) {
        return 0;
    }
    if (exp == 255) {
        return sign ? (int16_t)-32768 : 32767;
    }

    int32 exponent = (int32)exp - 127;
    uint32 mantissa = (1u << 23) | mant;
    int32 shift = exponent - 23;
    int64 value = (int64)mantissa * 32767;

    if (shift > 0) {
        if (shift >= 31) {
            return sign ? (int16_t)-32768 : 32767;
        }
        value <<= shift;
    } else if (shift < 0) {
        value >>= -shift;
    }

    if (sign) value = -value;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    return (int16_t)value;
}

// Convert PCM to Ensoniq native format (s16, stereo, 44100/48000 Hz)
uint32_t convert_to_ensoniq_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                               int16_t* dst, uint32_t dst_max_frames) {
    // Ensoniq prefers 44100 or 48000 Hz, stereo
    const uint32_t dst_rates[2] = {44100, 48000};
    uint32_t dst_rate = dst_rates[0]; // Default to 44100

    // Choose best matching rate
    uint32_t rate_diff_44 = (src_desc->sample_rate > 44100) ? (src_desc->sample_rate - 44100) : (44100 - src_desc->sample_rate);
    uint32_t rate_diff_48 = (src_desc->sample_rate > 48000) ? (src_desc->sample_rate - 48000) : (48000 - src_desc->sample_rate);
    if (rate_diff_48 < rate_diff_44) {
        dst_rate = 48000;
    }

    uint32_t dst_frames = (src_frames * dst_rate) / src_desc->sample_rate;
    if (dst_frames > dst_max_frames) {
        dst_frames = dst_max_frames;
    }

    for (uint32_t i = 0; i < dst_frames; i++) {
        uint32_t src_i = (i * src_desc->sample_rate) / dst_rate;
        if (src_i >= src_frames) src_i = src_frames - 1;

        if (src_desc->format == PCM_S16) {
            const int16_t* s = (const int16_t*)src;
            if (src_desc->channels == 2) {
                // Stereo to stereo
                dst[i * 2] = s[src_i * 2];
                dst[i * 2 + 1] = s[src_i * 2 + 1];
            } else {
                // Mono to stereo
                dst[i * 2] = s[src_i];
                dst[i * 2 + 1] = s[src_i];
            }
        } else if (src_desc->format == PCM_F32) {
            const uint32* s = (const uint32*)src;
            uint32 l_bits;
            uint32 r_bits;
            if (src_desc->channels == 2) {
                l_bits = s[src_i * 2];
                r_bits = s[src_i * 2 + 1];
            } else {
                l_bits = s[src_i];
                r_bits = s[src_i];
            }

            dst[i * 2] = pcm_f32_bits_to_s16(l_bits);
            dst[i * 2 + 1] = pcm_f32_bits_to_s16(r_bits);
        } else {
            // Unsupported
            dst[i * 2] = dst[i * 2 + 1] = 0;
        }
    }

    return dst_frames;
}

static void es1371_shutdown(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return;
    }
    es1371_state_t* state = (es1371_state_t*)driver->state;
    es1371_write32(state, ES1371_CONTROL, 0);
    if (state->dma_buffer_frame && state->dma_buffer_pages) {
        bitmap_pmm_free_pages(state->dma_buffer_frame, state->dma_buffer_pages);
    }
    state->dma_buffer_virt = 0;
    state->dma_buffer_phys = 0;
    state->dma_buffer_size = 0;
    state->dma_buffer_frame = 0;
    state->dma_buffer_pages = 0;
    state->initialized = false;
}

static SoundDriver g_ensoniq_driver = {
    .name = "Ensoniq AudioPCI",
    .type = SOUND_DEVICE_ENSONIQ_AUDIOPCI,
    .detect = es1371_detect,
    .init = es1371_init,
    .play_pcm = es1371_play_pcm,
    .get_capabilities = es1371_get_capabilities,
    .set_volume = es1371_set_volume,
    .beep = es1371_beep,
    .shutdown = es1371_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_ensoniq_driver(void) {
    return &g_ensoniq_driver;
}
