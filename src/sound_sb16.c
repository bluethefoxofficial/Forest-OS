#include "include/sound.h"
#include "include/io_ports.h"
#include "include/timer.h"
#include "include/screen.h"
#include "include/memory.h"
#include "include/cpu_ops.h"
#include "include/libc/string.h"
#include "include/interrupt.h"
#include "include/bitmap_pmm.h"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define SB16_DEFAULT_BASE 0x220
#define SB16_RESET        0x226
#define SB16_READ         0x22A
#define SB16_WRITE        0x22C
#define SB16_READ_STATUS  0x22E
#define SB16_INT_ACK      0x22F
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

// 16-bit DMA channel registers (channels 5-7)
#define DMA16_CH5_MASK     0xD4
#define DMA16_CH5_MODE     0xD6
#define DMA16_CH5_CLEAR_FF 0xD8
#define DMA16_CH5_BASE_ADDR 0xC4
#define DMA16_CH5_COUNT    0xC6
#define DMA16_CH5_PAGE     0x8B

#define DMA16_CH6_MASK     0xD4
#define DMA16_CH6_MODE     0xD6
#define DMA16_CH6_CLEAR_FF 0xD8
#define DMA16_CH6_BASE_ADDR 0xC8
#define DMA16_CH6_COUNT    0xCA
#define DMA16_CH6_PAGE     0x8B

#define DMA16_CH7_MASK     0xD4
#define DMA16_CH7_MODE     0xD6
#define DMA16_CH7_CLEAR_FF 0xD8
#define DMA16_CH7_BASE_ADDR 0xCC
#define DMA16_CH7_COUNT    0xCE
#define DMA16_CH7_PAGE     0x8F

#define DSP_CMD_SET_SAMPLE_RATE 0x41
#define DSP_CMD_SET_TIME_CONST  0x40
#define DSP_CMD_SPEAKER_ON      0xD1
#define DSP_CMD_SPEAKER_OFF     0xD3
#define DSP_CMD_DMA_8BIT        0xC0
#define DSP_CMD_DMA_8BIT_AI     0xC6  // 8-bit auto-initialize
#define DSP_CMD_DMA_16BIT       0xB0
#define DSP_CMD_DMA_16BIT_AI    0xB6  // 16-bit auto-initialize
#define DSP_CMD_STOP_8BIT        0xD0  // Stop 8-bit DMA
#define DSP_CMD_STOP_16BIT       0xD5  // Stop 16-bit DMA

typedef struct {
    uint16 base_port;
    uint8 irq;
    bool initialized;
    volatile bool dma_active;
    bool streaming_mode;
    uint32 current_buffer_pos;
    uint32 buffer_size;
    uint32 last_sample_rate;
    uint8 last_bits_per_sample;
    uint8 last_channels;
    uint8* dma_buffer_virt;
    uint32 dma_buffer_phys;
    uint32 dma_buffer_size;
    uint32 dma_buffer_frame;  // Frame number for deallocation
    uint32 dma_buffer_pages;  // Number of pages allocated
    uint32 current_dma_channel;
    volatile bool interrupt_fired;
} sb16_state_t;

static sb16_state_t g_sb16_state = {0};

static inline void sb16_write(uint16 port, uint8 value) {
    outportb(port, value);
}

static inline uint8 sb16_read(uint16 port) {
    return inportb(port);
}


static void sb16_interrupt_handler(struct interrupt_frame* frame, uint32_t error_code) {
    // For now, assume single SB16 device on IRQ 5
    // In a full implementation, we'd need to track which device this is for
    sb16_state_t* state = &g_sb16_state;

    if (!state || !state->initialized) {
        return;
    }

    // Check interrupt status register (mixer register 0x82)
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x82);
    uint8 irq_status = sb16_read(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE));

    // Bit 0: 8-bit DMA complete
    // Bit 1: 16-bit DMA complete
    bool dma_interrupt = false;
    
    if (irq_status & 0x01) {
        // 8-bit DMA interrupt - acknowledge by reading status
        sb16_read(state->base_port + (SB16_READ_STATUS - SB16_DEFAULT_BASE));
        dma_interrupt = true;
    }
    if (irq_status & 0x02) {
        // 16-bit DMA interrupt - acknowledge by reading 0x0F
        sb16_read(state->base_port + (SB16_INT_ACK - SB16_DEFAULT_BASE));
        dma_interrupt = true;
    }

    // For auto-init DMA, the interrupt indicates buffer completion
    // In streaming mode, the DMA continues but we need to track completion
    if (dma_interrupt && state->dma_active) {
        state->interrupt_fired = true;
        
        if (!state->streaming_mode) {
            // In single-shot mode, playback is complete
            state->dma_active = false;
            state->streaming_mode = false;
        }
        // In auto-init mode, keep dma_active = true so playback continues
    }
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

    // Only report detection success if the DSP actually responds to reset
    if (sb16_reset_dsp(state->base_port)) {
        // Verify we really have a 16‑bit DSP (version 4.x or newer)
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), 0xE1); // Get DSP version
        for (uint32 t = 0; t < 0x1000; t++) {
            if (sb16_read(state->base_port + (SB16_READ_STATUS - SB16_DEFAULT_BASE)) & 0x80) {
                uint8 ver_major = sb16_read(state->base_port + (SB16_READ - SB16_DEFAULT_BASE));
                uint8 ver_minor = sb16_read(state->base_port + (SB16_READ - SB16_DEFAULT_BASE));
                (void)ver_minor;
                if (ver_major >= 4) {
                    return true;
                }
                break;
            }
        }
        print("[SB16] DSP responded but version is not SB16-class\n");
    }

    print("[SB16] Hardware detection failed (no DSP response)\n");
    return false;
}

static bool sb16_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;

    // Set up mixer
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x22);
    sb16_write(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE), 0xFF);
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x80);
    sb16_write(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE), 0x02);

    // Configure IRQ (default to IRQ 5)
    state->irq = 5;
    sb16_write(state->base_port + (SB16_MIXER_ADDR - SB16_DEFAULT_BASE), 0x80);
    sb16_write(state->base_port + (SB16_MIXER_DATA - SB16_DEFAULT_BASE), state->irq);

    state->initialized = true;
    state->dma_active = false;

    // Allocate a DMA-safe bounce buffer (64KB) from physical frames
    // ISA DMA requires buffer within first 16MB and 64KB aligned
    // Use bitmap_pmm_alloc_contiguous_pages with 16-page alignment (64KB = 16 * 4KB pages)
    const uint32 dma_pages = 16; // 64KB = 16 pages
    const uint32 alignment_pages = 16; // 64KB alignment = 16 pages
    uint32 dma_frame = bitmap_pmm_alloc_contiguous_pages(dma_pages, alignment_pages);
    if (dma_frame == 0) {
        print("[SB16] Failed to allocate 64KB-aligned DMA buffer\n");
        return false;
    }
    
    // Convert frame number to physical address
    uint32 dma_phys = dma_frame * MEMORY_PAGE_SIZE;
    
    // Verify the buffer is within ISA DMA limits (first 16MB)
    if (dma_phys >= 0x1000000) {
        print("[SB16] DMA buffer not in low memory (ISA DMA requires < 16MB)\n");
        bitmap_pmm_free_pages(dma_frame, dma_pages);
        return false;
    }
    
    state->dma_buffer_phys = dma_phys;
    state->dma_buffer_size = 65536; // 64KB fixed size for 16-bit compatibility
    state->dma_buffer_frame = dma_frame;
    state->dma_buffer_pages = dma_pages;
    // Map the physical address to virtual
    state->dma_buffer_virt = (uint8*)mm_map_physical_page(dma_phys, 0);
    if (!state->dma_buffer_virt) {
        print("[SB16] Failed to map DMA buffer\n");
        bitmap_pmm_free_pages(dma_frame, dma_pages);
        return false;
    }
    
    // Store original allocation for cleanup
    state->current_dma_channel = 5; // Default to channel 5 for 16-bit
    state->interrupt_fired = false;

    // Register interrupt handler
    interrupt_set_handler_legacy(0x20 + state->irq, sb16_interrupt_handler);

    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SPEAKER_ON);
    return true;
}

static void sb16_program_dma8(uint32 phys_addr, uint32 length_bytes, bool auto_init) {
    outportb(DMA8_MASK, 0x05);
    outportb(DMA8_CLEAR_FF, 0);
    uint8 mode = auto_init ? 0x58 : 0x48; // Auto-init or single transfer, increment, read
    outportb(DMA8_MODE, mode);
    
    uint16 count = (uint16)(length_bytes - 1); // Count in bytes for 8-bit DMA

    outportb(DMA8_PAGE, (phys_addr >> 16) & 0xFF);
    outportb(DMA8_BASE_ADDR, phys_addr & 0xFF);
    outportb(DMA8_BASE_ADDR, (phys_addr >> 8) & 0xFF);
    outportb(DMA8_COUNT, count & 0xFF);
    outportb(DMA8_COUNT, (count >> 8) & 0xFF);

    outportb(DMA8_MASK, 0x01);
}

static void sb16_program_dma16_channel(uint8 channel, uint32 phys_addr, uint32 length_bytes, bool auto_init) {
    if (channel < 5 || channel > 7) return;

    // Select the appropriate registers for the channel
    uint16 mask_reg, mode_reg, clear_ff_reg, addr_reg, count_reg, page_reg;

    switch (channel) {
        case 5:
            mask_reg = DMA16_CH5_MASK;
            mode_reg = DMA16_CH5_MODE;
            clear_ff_reg = DMA16_CH5_CLEAR_FF;
            addr_reg = DMA16_CH5_BASE_ADDR;
            count_reg = DMA16_CH5_COUNT;
            page_reg = DMA16_CH5_PAGE;
            break;
        case 6:
            mask_reg = DMA16_CH6_MASK;
            mode_reg = DMA16_CH6_MODE;
            clear_ff_reg = DMA16_CH6_CLEAR_FF;
            addr_reg = DMA16_CH6_BASE_ADDR;
            count_reg = DMA16_CH6_COUNT;
            page_reg = DMA16_CH6_PAGE;
            break;
        case 7:
            mask_reg = DMA16_CH7_MASK;
            mode_reg = DMA16_CH7_MODE;
            clear_ff_reg = DMA16_CH7_CLEAR_FF;
            addr_reg = DMA16_CH7_BASE_ADDR;
            count_reg = DMA16_CH7_COUNT;
            page_reg = DMA16_CH7_PAGE;
            break;
        default:
            return;
    }

    // Mask the channel
    outportb(mask_reg, channel & 0x03);

    // Clear flip-flop
    outportb(clear_ff_reg, 0);

    // Set mode: increment, read, optional auto-init
    uint8 mode = auto_init ? 0x58 : 0x48;
    outportb(mode_reg, mode | (channel & 0x03));

    // Program address (word aligned for 16-bit)
    // Ensure word alignment
    if (phys_addr & 1) {
        phys_addr &= ~1;
    }

    // 16-bit DMA channels use word addresses in address registers.
    // Program address as phys_addr >> 1 while keeping page from original address.
    uint32 dma_word_addr = (phys_addr >> 1);
    outportb(page_reg, (phys_addr >> 16) & 0xFF);
    outportb(addr_reg, dma_word_addr & 0xFF);
    outportb(addr_reg, (dma_word_addr >> 8) & 0xFF);

    // Program count (in words - 1)
    uint16 word_count = (uint16)((length_bytes / 2) - 1);
    outportb(count_reg, word_count & 0xFF);
    outportb(count_reg, (word_count >> 8) & 0xFF);

    // Unmask the channel
    outportb(mask_reg, channel & 0x03);
}

// Convenience wrapper for channel 5 single-shot programming (legacy callers)
static void sb16_program_dma16(uint32 phys_addr, uint32 length_bytes) {
    sb16_program_dma16_channel(5, phys_addr, length_bytes, false);
}

// Convert any PCM format to SB16 native format (u8 mono 22050Hz or s16 mono/stereo up to 44100Hz)
uint32_t convert_to_sb16_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                            uint8_t* dst, uint32_t dst_max_frames, PcmFormat target_format,
                            uint16_t target_channels, uint32_t target_rate) {
    if (!src || !src_desc || !dst || src_frames == 0 || dst_max_frames == 0) {
        return 0;
    }

    uint32_t dst_frames = (src_frames * target_rate + src_desc->sample_rate - 1) / src_desc->sample_rate;

    if (dst_frames > dst_max_frames) {
        dst_frames = dst_max_frames;
    }

    // Clear destination buffer first
    memory_set(dst, 0, dst_max_frames * (target_format == PCM_S16 ? 2 : 1) * target_channels);

    for (uint32_t i = 0; i < dst_frames; i++) {
        // Resample index with proper bounds checking
        uint32_t src_i = (i * src_desc->sample_rate) / target_rate;
        if (src_i >= src_frames) src_i = src_frames - 1;

        int32_t sample_l = 0, sample_r = 0;

        // Decode source samples with bounds checking
        if (src_desc->format == PCM_S16) {
            const int16_t* s = (const int16_t*)src;
            if (src_desc->channels == 2) {
                sample_l = s[src_i * 2];
                sample_r = s[src_i * 2 + 1];
            } else if (src_i < src_frames) {
                sample_l = sample_r = s[src_i];
            }
        } else if (src_desc->format == PCM_F32) {
            const float* s = (const float*)src;
            float vl = 0.0f, vr = 0.0f;
            if (src_desc->channels == 2) {
                vl = s[src_i * 2];
                vr = s[src_i * 2 + 1];
            } else if (src_i < src_frames) {
                vl = vr = s[src_i];
            }
            // Clamp with proper range checking
            if (vl > 1.0f) vl = 1.0f;
            if (vl < -1.0f) vl = -1.0f;
            if (vr > 1.0f) vr = 1.0f;
            if (vr < -1.0f) vr = -1.0f;
            sample_l = (int32_t)(vl * 32767.0f);
            sample_r = (int32_t)(vr * 32767.0f);
        } else {
            // Unsupported source format - output silence
            sample_l = sample_r = 0;
        }

        // Convert to target format and channels
        if (target_channels == 2) {
            // Stereo output
            if (target_format == PCM_S16) {
                int16_t* dst_s16 = (int16_t*)dst;
                // Clamp to 16-bit range
                if (sample_l > 32767) sample_l = 32767;
                if (sample_l < -32768) sample_l = -32768;
                if (sample_r > 32767) sample_r = 32767;
                if (sample_r < -32768) sample_r = -32768;
                dst_s16[i * 2] = (int16_t)sample_l;
                dst_s16[i * 2 + 1] = (int16_t)sample_r;
            }
        } else {
            // Mono output - average channels
            int32_t final_sample = (sample_l + sample_r) / 2;

            if (target_format == PCM_U8) {
                // Convert signed 16-bit to unsigned 8-bit with proper clamping
                if (final_sample > 32767) final_sample = 32767;
                if (final_sample < -32768) final_sample = -32768;
                dst[i] = (uint8_t)((final_sample >> 8) + 128);
            } else if (target_format == PCM_S16) {
                int16_t* dst_s16 = (int16_t*)dst;
                if (final_sample > 32767) final_sample = 32767;
                if (final_sample < -32768) final_sample = -32768;
                dst_s16[i] = (int16_t)final_sample;
            }
        }
    }

    return dst_frames;
}

static bool sb16_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    if (!driver || !driver->state || !data || !format || length == 0) {
        return false;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }

    // Stop any ongoing DMA before starting a new buffer to avoid hangs on missing IRQs
    if (state->dma_active) {
        // Wait briefly for current operation to complete
        for (int i = 0; i < 1000; i++) {
            __asm__ volatile("nop");
        }
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_STOP_8BIT);
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_STOP_16BIT);
        state->dma_active = false;
        state->streaming_mode = false;
        state->interrupt_fired = false;
    }

    // Update format tracking
    state->last_sample_rate = format->sample_rate;
    state->last_bits_per_sample = format->bits_per_sample;
    state->last_channels = format->channels;

    // Validate format compatibility
    if (format->bits_per_sample == 16) {
        if (format->sample_rate > 44100) {
            print("[SB16] Sample rate too high for 16-bit mode: ");
            print_dec(format->sample_rate);
            print(" Hz\n");
            return false;
        }
        if (format->channels > 2) {
            print("[SB16] Too many channels for 16-bit mode: ");
            print_dec(format->channels);
            print("\n");
            return false;
        }
    } else if (format->bits_per_sample == 8) {
        if (format->sample_rate != 22050) {
            print("[SB16] 8-bit mode requires 22050 Hz, got: ");
            print_dec(format->sample_rate);
            print(" Hz\n");
            return false;
        }
        if (format->channels != 1) {
            print("[SB16] 8-bit mode requires mono, got: ");
            print_dec(format->channels);
            print(" channels\n");
            return false;
        }
    } else {
        print("[SB16] Unsupported bit depth: ");
        print_dec(format->bits_per_sample);
        print("\n");
        return false;
    }

    uint32 bytes_per_sample = format->bits_per_sample / 8;
    uint32 length_samples = length / bytes_per_sample;
    if (length_samples == 0) {
        return false;
    }

    // Clamp length to available DMA buffer and copy into bounce buffer
    uint32 copy_len = (length > state->dma_buffer_size) ? state->dma_buffer_size : length;
    
    // Ensure proper alignment for DMA
    if (format->bits_per_sample == 16) {
        // Word align for 16-bit DMA
        copy_len &= ~1;
    }
    
    // Clear buffer first to prevent garbage data
    memory_set(state->dma_buffer_virt, 0, state->dma_buffer_size);
    
    // Copy data with bounds checking
    if (copy_len > 0) {
        memory_copy((const char*)data, (char*)state->dma_buffer_virt, copy_len);
    }

    if (format->bits_per_sample == 16) {
        // 16-bit mode
        // Validate sample rate is within SB16 limits
        if (format->sample_rate < 4000 || format->sample_rate > 44100) {
            print("[SB16] Invalid sample rate for 16-bit mode: ");
            print_dec(format->sample_rate);
            print(" Hz\n");
            return false;
        }

        // Set sample rate for 16-bit mode
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SET_SAMPLE_RATE);
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((format->sample_rate >> 8) & 0xFF));
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(format->sample_rate & 0xFF));

        // Wait for DSP to be ready
        for (int i = 0; i < 100; i++) {
            if (sb16_read(state->base_port + (SB16_READ_STATUS - SB16_DEFAULT_BASE)) & 0x80) {
                break;
            }
            __asm__ volatile("nop");
        }

        // Program 16-bit DMA (single-shot, channel 5)
        state->current_dma_channel = 5;
        sb16_program_dma16_channel(5, state->dma_buffer_phys, copy_len, false);

        // Send 16-bit single-cycle playback command
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_DMA_16BIT);

        // Mode: signed, little-endian
        uint8 mode = 0x10; // signed
        if (format->channels == 2) mode |= 0x20; // stereo
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), mode);

        // Length in samples (not bytes for 16-bit) - ensure at least 1 sample
        uint16 sample_count = (uint16)(copy_len / 2);
        if (sample_count == 0) sample_count = 1;
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((sample_count - 1) & 0xFF));
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(((sample_count - 1) >> 8) & 0xFF));

        // Mark DMA as active for interrupt handling
        state->dma_active = true;
        state->streaming_mode = false;
        state->buffer_size = copy_len;
        state->current_buffer_pos = 0;
        state->interrupt_fired = false;

    } else {
        // 8-bit mode

        // Validate 8-bit mode parameters
        if (format->sample_rate < 4000 || format->sample_rate > 23000) {
            print("[SB16] Invalid sample rate for 8-bit mode: ");
            print_dec(format->sample_rate);
            print(" Hz (should be ~22050 Hz)\n");
            return false;
        }

        // Set sample rate using time constant
        uint8 time_constant = (uint8)(256 - (1000000 / format->sample_rate));
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SET_TIME_CONST);
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), time_constant);

        // Wait for DSP to be ready
        for (int i = 0; i < 100; i++) {
            if (sb16_read(state->base_port + (SB16_READ_STATUS - SB16_DEFAULT_BASE)) & 0x80) {
                break;
            }
            __asm__ volatile("nop");
        }

        // Program 8-bit DMA (single-shot, channel 1)
        state->current_dma_channel = 1;
        sb16_program_dma8(state->dma_buffer_phys, copy_len, false);

        // Send 8-bit single-cycle DMA command
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_DMA_8BIT);

        // Mode for 8-bit: mono, unsigned
        uint8 mode = 0x00; // unsigned, mono
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), mode);

        // Length in bytes for 8-bit - ensure at least 1 byte
        if (copy_len == 0) copy_len = 1;
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((copy_len - 1) & 0xFF));
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(((copy_len - 1) >> 8) & 0xFF));

        // Mark DMA as active for interrupt handling
        state->dma_active = true;
        state->streaming_mode = false;
        state->buffer_size = copy_len;
        state->current_buffer_pos = 0;
        state->interrupt_fired = false;
    }

    // For streaming, don't wait for completion
    // The caller handles timing between chunks
    return true;
}

static bool sb16_get_capabilities(SoundDriver* driver, DeviceCapabilities* caps) {
    if (!driver || !caps) {
        return false;
    }

    memset(caps, 0, sizeof(DeviceCapabilities));

    // SB16 capabilities:
    // 8-bit unsigned PCM: mono, 22050 Hz, 8-bit DMA channels
    // 16-bit signed PCM: mono/stereo, up to 44100 Hz, 16-bit DMA channels
    caps->supported_formats[0] = PCM_U8;
    caps->supported_formats[1] = PCM_S16;
    caps->max_channels = 2; // Stereo support in 16-bit mode
    caps->native_sample_rates[0] = 22050;
    caps->native_sample_rates[1] = 44100;
    caps->stereo_supported = true; // In 16-bit mode only
    caps->little_endian = true;
    caps->max_buffer_size = 131070; // 64KB for 16-bit (128KB for 8-bit)

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
    if (!state->initialized || !state->dma_buffer_virt) {
        return;
    }

    // Validate frequency range
    if (frequency_hz == 0 || frequency_hz > 20000) {
        return;
    }

    uint32 rate = frequency_hz * 2;
    if (rate > 44100) rate = 44100;
    if (rate < 4000) rate = 4000;

    // Set sample rate
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SET_SAMPLE_RATE);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((rate >> 8) & 0xFF));
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(rate & 0xFF));

    // Generate square wave samples
    uint32 samples_needed = (rate * duration_ms) / 1000;
    if (samples_needed > state->dma_buffer_size / 2) {
        samples_needed = state->dma_buffer_size / 2;
    }

    // Clear buffer first
    memory_set(state->dma_buffer_virt, 0, state->dma_buffer_size);
    
    // Generate square wave
    int16_t* samples = (int16_t*)state->dma_buffer_virt;
    for (uint32 i = 0; i < samples_needed; i++) {
        samples[i] = ((i / (rate / (frequency_hz * 2))) % 2) ? 8192 : -8192;
    }

    // Program 16-bit DMA
    sb16_program_dma16_channel(5, state->dma_buffer_phys, samples_needed * 2, false);

    // Start playback
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_DMA_16BIT);
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), 0x10); // signed, mono

    uint16 sample_count = (uint16)samples_needed;
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)((sample_count - 1) & 0xFF));
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), (uint8)(((sample_count - 1) >> 8) & 0xFF));

    // Wait for completion with timeout
    uint32 start = timer_get_ticks();
    state->dma_active = true;
    state->interrupt_fired = false;
    
    while (state->dma_active && (timer_get_ticks() - start) < (duration_ms / 10) + 100) {
        // Check if interrupt fired
        if (state->interrupt_fired) {
            state->dma_active = false;
            break;
        }
        timer_sleep_ms(1);
    }
    
    // Stop playback
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_STOP_16BIT);
    state->dma_active = false;
}

static void sb16_shutdown(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return;
    }
    sb16_state_t* state = (sb16_state_t*)driver->state;

    // Stop any active DMA
    if (state->dma_active) {
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_STOP_8BIT);
        sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_STOP_16BIT);
        state->dma_active = false;
        state->streaming_mode = false;
        state->interrupt_fired = false;
    }

    // Wait for DMA to complete
    for (int i = 0; i < 1000; i++) {
        __asm__ volatile("nop");
    }

    // Unregister interrupt handler
    interrupt_clear_handler(0x20 + state->irq);

    // Turn off speaker
    sb16_write(state->base_port + (SB16_WRITE - SB16_DEFAULT_BASE), DSP_CMD_SPEAKER_OFF);
    
    // Clean up DMA buffer
    if (state->dma_buffer_virt) {
        // Free the allocated physical pages
        if (state->dma_buffer_frame != 0 && state->dma_buffer_pages > 0) {
            bitmap_pmm_free_pages(state->dma_buffer_frame, state->dma_buffer_pages);
        }
        state->dma_buffer_virt = 0;
    }
    state->dma_buffer_phys = 0;
    state->dma_buffer_size = 0;
    state->dma_buffer_frame = 0;
    state->dma_buffer_pages = 0;
    state->initialized = false;
    state->current_dma_channel = 0;
}

static SoundDriver g_sb16_driver = {
    .name = "Sound Blaster 16",
    .type = SOUND_DEVICE_SOUND_BLASTER16,
    .detect = sb16_detect,
    .init = sb16_init,
    .play_pcm = sb16_play_pcm,
    .get_capabilities = sb16_get_capabilities,
    .set_volume = sb16_set_volume,
    .beep = sb16_beep,
    .shutdown = sb16_shutdown,
    .state = &g_sb16_state,
    .volume = 255
};

SoundDriver* sound_sb16_driver(void) {
    return &g_sb16_driver;
}
