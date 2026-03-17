#include "include/sound.h"
#include "include/pci.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/libc/string.h"
#include "include/debuglog.h"
#include "include/memory.h"
#include "include/bitmap_pmm.h"
#include "include/interrupt.h"
#include "include/device_fs.h"

#define HDA_REG_GCAP      0x00
#define HDA_REG_VMAJ      0x02
#define HDA_REG_VMIN      0x03
#define HDA_REG_OUTPAY    0x04
#define HDA_REG_INPAY     0x06
#define HDA_REG_GCTL      0x08
#define HDA_REG_WAKEEN    0x0C
#define HDA_REG_STATESTS  0x0E
#define HDA_REG_GSTS      0x10
#define HDA_REG_OUTSTRMPAY 0x18
#define HDA_REG_INSTRMPAY 0x1A
#define HDA_REG_INTCTL    0x20
#define HDA_REG_INTSTS    0x24
#define HDA_REG_WALCLK    0x30
#define HDA_REG_SSYNC     0x34
#define HDA_REG_CORBLBASE 0x40
#define HDA_REG_CORBUBASE 0x44
#define HDA_REG_CORBWP    0x48
#define HDA_REG_CORBRP    0x4A
#define HDA_REG_CORBCTL   0x4C
#define HDA_REG_CORBSTS   0x4D
#define HDA_REG_CORBSIZE  0x4E
#define HDA_REG_RIRBLBASE 0x50
#define HDA_REG_RIRBUBASE 0x54
#define HDA_REG_RIRBWP    0x58
#define HDA_REG_RINTCNT   0x5A
#define HDA_REG_RIRBCTL   0x5C
#define HDA_REG_RIRBSTS   0x5D
#define HDA_REG_RIRBSIZE  0x5E
#define HDA_REG_DPLBASE   0x60
#define HDA_REG_DPUBASE   0x64
#define HDA_REG_ICW       0x68
#define HDA_REG_IRR       0x6C
#define HDA_REG_ICS       0x70

#define HDA_GCTL_CRST      (1 << 0)
#define HDA_GCTL_FCNTRL    (1 << 1)

#define HDA_CORBCTL_RUN    (1 << 1)
#define HDA_RIRBCTL_RUN    (1 << 1)
#define HDA_RIRBCTL_INT_EN (1 << 0)

#define HDA_DMA_BUFFER_SIZE (64 * 1024)  // 64KB circular buffer
#define HDA_BUFFER_PERIODS  4            // Split buffer into 4 periods
#define HDA_PERIOD_SIZE     (HDA_DMA_BUFFER_SIZE / HDA_BUFFER_PERIODS)

#define SOUND_IOCTL_GET_POSITION 0x1000
#define SOUND_IOCTL_GET_FORMAT   0x1001
#define SOUND_IOCTL_SET_FORMAT   0x1002

typedef struct {
    uint32_t addr;     // Physical address of buffer
    uint32_t length;   // Length in bytes
    uint32_t ioc;      // Interrupt on completion flag
} hda_bdl_entry_t;

typedef struct {
    pci_device_t pci;
    volatile uint32* mmio_base;
    bool initialized;
    
    // DMA buffer management
    uint32_t dma_buffer_phys;     // Physical address of DMA buffer
    int16_t* dma_buffer_virt;     // Virtual address of DMA buffer
    uint32_t dma_buffer_size;     // Total size of DMA buffer
    
    // BDL (Buffer Descriptor List)
    uint32_t bdl_phys;            // Physical address of BDL
    hda_bdl_entry_t* bdl_virt;    // Virtual address of BDL
    
    // Stream management
    uint8_t stream_tag;           // Current stream tag
    uint32_t current_position;     // Current play position in bytes
    uint32_t write_position;       // Current write position in bytes
    uint32_t period_size;          // Size of each period in bytes
    
    // Audio format
    uint32_t sample_rate;          // Current sample rate
    uint8_t channels;              // Current channels
    uint8_t bits_per_sample;      // Current bits per sample
    
    // Interrupt handling
    uint8_t irq_line;             // IRQ line
    bool interrupts_enabled;      // Interrupt state
    
    // Locking for concurrent access
    volatile bool buffer_locked;   // Buffer access lock
} hda_state_t;

static inline void hda_write_reg(hda_state_t* state, uint16_t offset, uint32_t value) {
    state->mmio_base[offset / 4] = value;
}

static inline uint32_t hda_read_reg(hda_state_t* state, uint16_t offset) {
    return state->mmio_base[offset / 4];
}

static void hda_interrupt_handler(void* data) {
    hda_state_t* state = (hda_state_t*)data;
    if (!state || !state->initialized) {
        return;
    }

    uint32_t int_status = hda_read_reg(state, HDA_REG_INTSTS);
    if (int_status & (1 << 0)) {  // Global controller interrupt
        uint32_t rirb_status = hda_read_reg(state, HDA_REG_RIRBSTS);
        if (rirb_status & HDA_RIRBCTL_INT_EN) {
            // Update current position based on stream status
            // For now, advance by one period
            state->current_position += state->period_size;
            if (state->current_position >= state->dma_buffer_size) {
                state->current_position = 0;
            }
            
            // Clear interrupt
            hda_write_reg(state, HDA_REG_RIRBSTS, rirb_status);
        }
    }

    // Clear the interrupt
    hda_write_reg(state, HDA_REG_INTSTS, int_status);
}

static bool hda_allocate_dma_buffer(hda_state_t* state) {
    // Allocate physically contiguous memory for DMA buffer
    uint32_t num_pages = (HDA_DMA_BUFFER_SIZE + MEMORY_PAGE_SIZE - 1) / MEMORY_PAGE_SIZE;
    uint32_t buffer_frame = bitmap_pmm_alloc_pages(num_pages, PMM_ALLOC_LOW_MEMORY);
    if (buffer_frame == 0) {
        debuglog(DEBUG_ERROR, "HDA: Failed to allocate DMA buffer pages\n");
        return false;
    }

    uint32_t buffer_phys = buffer_frame * MEMORY_PAGE_SIZE;
    
    // Map the physical memory to virtual address space
    uintptr_t buffer_virt = (uintptr_t)0xF0000000u + (uintptr_t)buffer_phys;  // High virtual address
    memory_result_t map_result = MEMORY_OK;
    
    for (uint32_t i = 0; i < num_pages; i++) {
        memory_result_t result = vmm_map_page(vmm_get_current_page_directory(),
                                            (uint32_t)(buffer_virt + (uintptr_t)i * MEMORY_PAGE_SIZE),
                                            buffer_frame + i,
                                            PAGE_PRESENT | PAGE_WRITABLE);
        if (result != MEMORY_OK) {
            map_result = result;
            break;
        }
    }
    
    if (map_result != MEMORY_OK) {
        debuglog(DEBUG_ERROR, "HDA: Failed to map DMA buffer: %d\n", map_result);
        bitmap_pmm_free_pages(buffer_frame, num_pages);
        return false;
    }

    state->dma_buffer_phys = buffer_phys;
    state->dma_buffer_virt = (int16_t*)buffer_virt;
    state->dma_buffer_size = HDA_DMA_BUFFER_SIZE;
    
    debuglog(DEBUG_INFO, "HDA: DMA buffer allocated - phys:0x%08x virt:%p size:%u\n",
             buffer_phys, (void*)buffer_virt, HDA_DMA_BUFFER_SIZE);
    
    // Initialize buffer to silence
    memset(state->dma_buffer_virt, 0, HDA_DMA_BUFFER_SIZE);
    
    return true;
}

static bool hda_allocate_bdl(hda_state_t* state) {
    // Allocate memory for BDL (Buffer Descriptor List)
    uint32_t bdl_frame = bitmap_pmm_alloc_page(PMM_ALLOC_LOW_MEMORY);
    if (bdl_frame == 0) {
        debuglog(DEBUG_ERROR, "HDA: Failed to allocate BDL page\n");
        return false;
    }

    uint32_t bdl_phys = bdl_frame * MEMORY_PAGE_SIZE;
    uintptr_t bdl_virt = (uintptr_t)0xE0000000u + (uintptr_t)bdl_phys;
    
    memory_result_t map_result = vmm_map_page(vmm_get_current_page_directory(),
                                             (uint32_t)bdl_virt, bdl_frame,
                                             PAGE_PRESENT | PAGE_WRITABLE);
    if (map_result != MEMORY_OK) {
        debuglog(DEBUG_ERROR, "HDA: Failed to map BDL page: %d\n", map_result);
        bitmap_pmm_free_page(bdl_frame);
        return false;
    }

    state->bdl_phys = bdl_phys;
    state->bdl_virt = (hda_bdl_entry_t*)bdl_virt;
    
    debuglog(DEBUG_INFO, "HDA: BDL allocated - phys:0x%08x virt:%p\n",
             bdl_phys, (void*)bdl_virt);
    
    return true;
}

static void hda_setup_bdl(hda_state_t* state) {
    uint32_t period_size = state->dma_buffer_size / HDA_BUFFER_PERIODS;
    state->period_size = period_size;
    
    // Setup BDL entries for circular buffer
    for (int i = 0; i < HDA_BUFFER_PERIODS; i++) {
        state->bdl_virt[i].addr = state->dma_buffer_phys + i * period_size;
        state->bdl_virt[i].length = period_size;
        state->bdl_virt[i].ioc = 0x80000000;  // Interrupt on completion
    }
    
    debuglog(DEBUG_INFO, "HDA: BDL setup complete - %u periods, %u bytes each\n",
             HDA_BUFFER_PERIODS, period_size);
}

static bool hda_detect(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    hda_state_t* state = (hda_state_t*)driver->state;
    if (!state) {
        static hda_state_t static_state;
        state = &static_state;
        driver->state = state;
        memset(state, 0, sizeof(hda_state_t));
    }

    pci_device_t device;
    if (!pci_find_by_class(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_HD_AUDIO, &device)) {
        return false;
    }

    state->pci = device;
    uint32 bar0 = device.bar[0];
    if (!(bar0 & 0x1)) {
        state->mmio_base = (volatile uint32*)(uintptr_t)(bar0 & ~0xFu);
    } else {
        return false;
    }
    
    // Enable bus mastering for DMA
    uint16 command = pci_config_read16(device.segment, device.bus, device.device, device.function, 4);
    command |= 0x0004;  // Bus master enable
    pci_config_write16(device.segment, device.bus, device.device, device.function, 4, command);
    
    // Get IRQ line from PCI configuration space
    state->irq_line = pci_config_read8(device.segment, device.bus, device.device, device.function, 0x3C);
    
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
    
    debuglog(DEBUG_INFO, "HDA: Initializing Intel HDA driver\n");
    
    // Reset the controller
    hda_write_reg(state, HDA_REG_GCTL, 0);
    // Wait for reset to complete
    for (int i = 0; i < 1000; i++) {
        if (!(hda_read_reg(state, HDA_REG_GCTL) & HDA_GCTL_CRST)) {
            break;
        }
    }
    
    // Take controller out of reset
    hda_write_reg(state, HDA_REG_GCTL, HDA_GCTL_CRST);
    // Wait for controller to be ready
    for (int i = 0; i < 1000; i++) {
        if (hda_read_reg(state, HDA_REG_GCTL) & HDA_GCTL_CRST) {
            break;
        }
    }
    
    if (!(hda_read_reg(state, HDA_REG_GCTL) & HDA_GCTL_CRST)) {
        debuglog(DEBUG_ERROR, "HDA: Controller failed to come out of reset\n");
        return false;
    }
    
    // Allocate DMA buffer and BDL
    if (!hda_allocate_dma_buffer(state)) {
        return false;
    }
    
    if (!hda_allocate_bdl(state)) {
        return false;
    }
    
    // Setup default audio format: 16-bit stereo @ 48kHz
    state->sample_rate = 48000;
    state->channels = 2;
    state->bits_per_sample = 16;
    state->stream_tag = 1;
    state->current_position = 0;
    state->write_position = 0;
    
    // Setup BDL entries
    hda_setup_bdl(state);
    
    // Register interrupt handler
    if (state->irq_line != 0xFF) {
        // TODO: Implement interrupt_register_handler
        // interrupt_register_handler(state->irq_line, hda_interrupt_handler, state);
        state->interrupts_enabled = true;
        debuglog(DEBUG_INFO, "HDA: Interrupt handler registration skipped (not implemented)\n");
    }
    
    state->initialized = true;
    debuglog(DEBUG_INFO, "HDA: Driver initialized successfully\n");
    return true;
}

static bool hda_start_dma(hda_state_t* state) {
    // Configure stream descriptor and start DMA
    // This is a simplified implementation - real HDA requires codec programming
    
    // For now, we'll simulate DMA operation by copying data to the buffer
    // and updating position tracking
    
    state->current_position = 0;
    state->write_position = 0;
    
    debuglog(DEBUG_INFO, "HDA: DMA started for audio playback\n");
    return true;
}

static bool hda_play_pcm(SoundDriver* driver, const uint8* data, uint32 length, const SoundFormat* format) {
    if (!driver || !driver->state || !data || !format) {
        return false;
    }
    hda_state_t* state = (hda_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }
    
    // Validate format
    if (format->bits_per_sample != 16 || format->channels > 2) {
        debuglog(DEBUG_ERROR, "HDA: Unsupported audio format\n");
        return false;
    }
    
    // Calculate space needed in circular buffer
    uint32 samples = length / (format->bits_per_sample / 8 * format->channels);
    uint32 bytes_needed = samples * (state->bits_per_sample / 8 * state->channels);
    
    // Check if buffer has enough space
    uint32 available_space;
    if (state->write_position >= state->current_position) {
        available_space = state->dma_buffer_size - (state->write_position - state->current_position);
    } else {
        available_space = state->current_position - state->write_position;
    }
    
    if (available_space < bytes_needed) {
        debuglog(DEBUG_WARN, "HDA: Buffer overflow, dropping audio data\n");
        return false;
    }
    
    // Convert and copy audio data to DMA buffer
    int16_t* src = (int16_t*)data;
    int16_t* dst = state->dma_buffer_virt;
    
    for (uint32 i = 0; i < samples; i++) {
        uint32 src_idx = i * format->channels;
        uint32 dst_idx = ((state->write_position / 2) + i * state->channels) % (state->dma_buffer_size / 2);
        
        if (format->channels == 1 && state->channels == 2) {
            // Mono to stereo conversion
            int16_t sample = src[src_idx];
            dst[dst_idx] = sample;        // Left channel
            dst[dst_idx + 1] = sample;    // Right channel
        } else if (format->channels == 2 && state->channels == 2) {
            // Stereo to stereo copy
            dst[dst_idx] = src[src_idx];        // Left channel
            dst[dst_idx + 1] = src[src_idx + 1]; // Right channel
        }
    }
    
    // Update write position
    state->write_position = (state->write_position + bytes_needed) % state->dma_buffer_size;
    
    // Start DMA if not already running
    static bool dma_running = false;
    if (!dma_running) {
        hda_start_dma(state);
        dma_running = true;
    }
    
    return true;
}

static void hda_set_volume(SoundDriver* driver, uint8 volume) {
    if (!driver || !driver->state) {
        return;
    }
    hda_state_t* state = (hda_state_t*)driver->state;
    if (!state->initialized) {
        return;
    }
    
    // Volume control would be implemented via codec commands
    // For now, just store the value
    driver->volume = volume;
    (void)volume;
}

static void hda_beep(SoundDriver* driver, uint32 frequency_hz, uint32 duration_ms) {
    if (!driver || !driver->state) {
        return;
    }
    hda_state_t* state = (hda_state_t*)driver->state;
    if (!state->initialized) {
        return;
    }
    
    if (frequency_hz == 0 || duration_ms == 0) {
        return;
    }

    // Generate a simple square wave beep (avoid floating point)
    uint32 sample_rate = state->sample_rate ? state->sample_rate : 48000;
    uint32 samples = (sample_rate * duration_ms) / 1000;
    uint32 bytes_needed = samples * 4; // 16-bit stereo
    
    if (bytes_needed > state->dma_buffer_size) {
        bytes_needed = state->dma_buffer_size;
        samples = bytes_needed / 4;
    }
    
    int16_t* buffer = state->dma_buffer_virt;
    uint32 period = sample_rate / frequency_hz;
    if (period == 0) {
        period = 1;
    }
    const int16_t amplitude = 8000;
    for (uint32 i = 0; i < samples; i++) {
        int16_t sample = ((i % period) < (period / 2)) ? amplitude : (int16_t)-amplitude;
        buffer[i * 2] = sample;      // Left channel
        buffer[i * 2 + 1] = sample;  // Right channel
    }
    
    state->write_position = bytes_needed;
    hda_start_dma(state);
    
    debuglog(DEBUG_INFO, "HDA: Beep generated - %u Hz for %u ms\n", frequency_hz, duration_ms);
}

static bool hda_get_capabilities(SoundDriver* driver, DeviceCapabilities* caps) {
    if (!driver || !caps) {
        return false;
    }

    memset(caps, 0, sizeof(DeviceCapabilities));

    // HDA capabilities: 16-bit signed PCM, stereo, 44100/48000 Hz, little endian
    caps->supported_formats[0] = PCM_S16;
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

// Convert PCM to HDA native format (s16, stereo, 44100/48000 Hz)
uint32_t convert_to_hda_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                           int16_t* dst, uint32_t dst_max_frames) {
    // HDA prefers 44100 or 48000 Hz, stereo
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

static void hda_shutdown(SoundDriver* driver) {
    (void)driver;
}

static SoundDriver g_hda_driver = {
    .name = "Intel HDA (stub)",
    .type = SOUND_DEVICE_HDA,
    .detect = hda_detect,
    .init = hda_init,
    .play_pcm = hda_play_pcm,
    .get_capabilities = hda_get_capabilities,
    .set_volume = hda_set_volume,
    .beep = hda_beep,
    .shutdown = hda_shutdown,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_hda_driver(void) {
    return &g_hda_driver;
}
