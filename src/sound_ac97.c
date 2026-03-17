#include "include/sound.h"
#include "include/pci.h"
#include "include/io_ports.h"
#include "include/screen.h"
#include "include/memory.h"
#include "include/libc/string.h"
#include "include/debuglog.h"
#include "include/interrupt.h"
#include "include/timer.h"
#include "include/bitmap_pmm.h"
#include "include/cpu_ops.h"

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

#define AC97_BUFFER_ENTRIES 4
#define AC97_VRA_BIT 0x0001

#define AC97_NAM_OFFSET_RESET        0x00
#define AC97_NAM_OFFSET_MASTER_VOL  0x02
#define AC97_NAM_OFFSET_PCM_VOL     0x18
#define AC97_NAM_OFFSET_FRONT_DAC_RATE 0x2C
#define AC97_NAM_OFFSET_POWER       0x26
#define AC97_NAM_OFFSET_EXT_AUDIO_ID 0x28
#define AC97_NAM_OFFSET_EXT_AUDIO_CTRL 0x2A

#define AC97_NABM_OFFSET_PO_BDBAR   0x10
#define AC97_NABM_OFFSET_PO_CIV     0x14
#define AC97_NABM_OFFSET_PO_LVI     0x15
#define AC97_NABM_OFFSET_PO_SR      0x16
#define AC97_NABM_OFFSET_PO_CR      0x1B
#define AC97_NABM_OFFSET_GLOBAL_CTRL 0x2C
#define AC97_NABM_OFFSET_GLOBAL_STATUS 0x30

#define AC97_PO_CR_RUN             0x01
#define AC97_PO_CR_RESET           0x02
#define AC97_PO_CR_IOCE            0x04
#define AC97_PO_CR_FEIE            0x08

#define AC97_SR_LVBCI              0x04
#define AC97_SR_CE                 0x02
#define AC97_SR_DCH                0x01
#define AC97_GLOBAL_CTRL_SRIE     0x02
#define AC97_GLOBAL_CTRL_COLD_RST  0x02
#define AC97_GLOBAL_CTRL_RUN       0x01

typedef struct {
    uint32_t phys_addr;
    uint16_t length;
    uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

typedef struct {
    pci_device_t pci;
    uint16_t nam_base;
    uint16_t nabm_base;
    ac97_bdl_entry_t* bdl;
    uint32_t bdl_phys;
    uint8_t* dma_buffer;
    uint8_t* dma_buffer_virt;
    uint32_t dma_buffer_phys;
    uint32_t dma_buffer_size;
    bool initialized;
    bool vra_supported;
    uint32_t current_sample_rate;
    uint8_t irq;
} ac97_state_t;

static ac97_state_t g_ac97_state = {0};
static volatile bool g_ac97_streaming = false;
static volatile uint32_t g_ac97_position = 0;

static inline uint16_t ac97_read_nam(ac97_state_t* state, uint8_t reg) {
    return inportw(state->nam_base + reg);
}

static inline void ac97_write_nam(ac97_state_t* state, uint8_t reg, uint16_t value) {
    outportw(state->nam_base + reg, value);
}

static inline uint32_t ac97_read_nabm32(ac97_state_t* state, uint8_t reg) {
    return inportd(state->nabm_base + reg);
}

static inline void ac97_write_nabm32(ac97_state_t* state, uint8_t reg, uint32_t value) {
    outportd(state->nabm_base + reg, value);
}

static inline void ac97_write_nabm_byte(ac97_state_t* state, uint8_t reg, uint8_t value) {
    outportb(state->nabm_base + reg, value);
}

static inline uint8_t ac97_read_nabm_byte(ac97_state_t* state, uint8_t reg) {
    return inportb(state->nabm_base + reg);
}

static void ac97_interrupt_handler(struct interrupt_frame* frame, uint32_t error_code) {
    (void)frame;
    (void)error_code;
    
    ac97_state_t* state = &g_ac97_state;
    if (!state || !state->initialized) {
        return;
    }

    uint32_t status = ac97_read_nabm32(state, AC97_NABM_OFFSET_PO_SR);
    if (status & AC97_SR_LVBCI) {
        g_ac97_position += state->dma_buffer_size / AC97_BUFFER_ENTRIES;
        if (g_ac97_position >= state->dma_buffer_size) {
            g_ac97_position = 0;
        }
    }
    ac97_write_nabm32(state, AC97_NABM_OFFSET_PO_SR, status);
}

static bool ac97_detect(SoundDriver* driver) {
    if (!driver) {
        return false;
    }
    
    ac97_state_t* state = &g_ac97_state;
    driver->state = state;
    memset(state, 0, sizeof(ac97_state_t));

    pci_device_t device;
    if (!pci_find_by_class(PCI_CLASS_MULTIMEDIA, PCI_SUBCLASS_AUDIO, &device)) {
        debuglog(DEBUG_INFO, "AC97: No PCI audio device found\n");
        return false;
    }

    state->pci = device;
    state->nam_base = (uint16_t)(device.bar[0] & ~0x3);
    state->nabm_base = (uint16_t)(device.bar[1] & ~0x3);

    if (!state->nam_base || !state->nabm_base) {
        debuglog(DEBUG_ERROR, "AC97: Invalid BAR addresses (BAR0=0x%x BAR1=0x%x)\n",
                device.bar[0], device.bar[1]);
        return false;
    }

    uint16_t vendor_id = pci_config_read16(device.segment, device.bus, device.device, device.function, 0);
    uint16_t device_id = pci_config_read16(device.segment, device.bus, device.device, device.function, 2);
    debuglog(DEBUG_INFO, "AC97: Found device %04X:%04X at BAR0=0x%X BAR1=0x%X\n",
             vendor_id, device_id, state->nam_base, state->nabm_base);

    return true;
}

static bool ac97_init_codec(ac97_state_t* state) {
    debuglog(DEBUG_INFO, "AC97: Initializing codec...\n");

    uint16_t pci_cmd = pci_config_read16(state->pci.segment, state->pci.bus, 
                                         state->pci.device, state->pci.function, 0x04);
    pci_cmd |= 0x0004 | 0x0001;
    pci_config_write16(state->pci.segment, state->pci.bus, 
                      state->pci.device, state->pci.function, 0x04, pci_cmd);
    debuglog(DEBUG_INFO, "AC97: PCI cmd: 0x%04X\n", pci_cmd);

    uint32_t global_ctrl = ac97_read_nabm32(state, AC97_NABM_OFFSET_GLOBAL_CTRL);
    global_ctrl |= AC97_GLOBAL_CTRL_COLD_RST;
    ac97_write_nabm32(state, AC97_NABM_OFFSET_GLOBAL_CTRL, global_ctrl);
    for (volatile int i = 0; i < 1000; i++) { }
    global_ctrl |= AC97_GLOBAL_CTRL_RUN;
    ac97_write_nabm32(state, AC97_NABM_OFFSET_GLOBAL_CTRL, global_ctrl);
    for (volatile int i = 0; i < 1000; i++) { }
    debuglog(DEBUG_INFO, "AC97: Global ctrl: 0x%08X\n", 
             ac97_read_nabm32(state, AC97_NABM_OFFSET_GLOBAL_CTRL));

    ac97_write_nam(state, AC97_NAM_OFFSET_RESET, 0);
    for (volatile int i = 0; i < 1000; i++) { }

    uint16_t ext_id = ac97_read_nam(state, AC97_NAM_OFFSET_EXT_AUDIO_ID);
    state->vra_supported = (ext_id & AC97_VRA_BIT) != 0;
    debuglog(DEBUG_INFO, "AC97: Ext ID: 0x%04X, VRA: %s\n", ext_id, 
             state->vra_supported ? "yes" : "no");

    if (state->vra_supported) {
        uint16_t ext_ctrl = ac97_read_nam(state, AC97_NAM_OFFSET_EXT_AUDIO_CTRL);
        ext_ctrl |= AC97_VRA_BIT;
        ac97_write_nam(state, AC97_NAM_OFFSET_EXT_AUDIO_CTRL, ext_ctrl);
    }

    ac97_write_nam(state, AC97_NAM_OFFSET_POWER, 0x0000);
    ac97_write_nam(state, AC97_NAM_OFFSET_MASTER_VOL, 0x0000);
    ac97_write_nam(state, AC97_NAM_OFFSET_PCM_VOL, 0x0808);

    debuglog(DEBUG_INFO, "AC97: Codec initialized\n");
    return true;
}

static bool ac97_allocate_dma_buffers(ac97_state_t* state) {
    state->dma_buffer_size = 8 * 1024;
    uint32_t num_pages = (state->dma_buffer_size + 4095) / 4096;
    
    uint32_t frame = bitmap_pmm_alloc_pages(num_pages, PMM_ALLOC_LOW_MEMORY);
    if (frame == 0 || frame == (uint32_t)-1) {
        debuglog(DEBUG_ERROR, "AC97: DMA alloc failed\n");
        return false;
    }

    state->dma_buffer_phys = frame * 4096;
    state->dma_buffer_virt = (uint8_t*)mm_map_physical_page(state->dma_buffer_phys, 0);
    
    if (!state->dma_buffer_virt) {
        debuglog(DEBUG_ERROR, "AC97: DMA map failed\n");
        bitmap_pmm_free_pages(frame, num_pages);
        return false;
    }
    
    state->dma_buffer = state->dma_buffer_virt;

    memset(state->dma_buffer_virt, 0, state->dma_buffer_size);
    debuglog(DEBUG_INFO, "AC97: DMA buf: phys=0x%08X virt=0x%08X size=%u\n",
             state->dma_buffer_phys, (uint32_t)state->dma_buffer_virt, state->dma_buffer_size);

    uint32_t bdl_frame = bitmap_pmm_alloc_pages(1, PMM_ALLOC_LOW_MEMORY);
    if (bdl_frame == 0 || bdl_frame == (uint32_t)-1) {
        debuglog(DEBUG_ERROR, "AC97: BDL alloc failed\n");
        return false;
    }

    state->bdl_phys = bdl_frame * 4096;
    state->bdl = (ac97_bdl_entry_t*)mm_map_physical_page(state->bdl_phys, 0);
    
    if (!state->bdl) {
        debuglog(DEBUG_ERROR, "AC97: BDL map failed\n");
        bitmap_pmm_free_pages(bdl_frame, 1);
        return false;
    }

    memset(state->bdl, 0, sizeof(ac97_bdl_entry_t) * AC97_BUFFER_ENTRIES);
    debuglog(DEBUG_INFO, "AC97: BDL: phys=0x%08X virt=0x%08X\n",
             state->bdl_phys, (uint32_t)state->bdl);

    return true;
}

static void ac97_program_bdl(ac97_state_t* state) {
    uint32_t period_size = state->dma_buffer_size / AC97_BUFFER_ENTRIES;
    
    for (int i = 0; i < AC97_BUFFER_ENTRIES; i++) {
        state->bdl[i].phys_addr = state->dma_buffer_phys + i * period_size;
        state->bdl[i].length = (uint16_t)period_size;
        state->bdl[i].flags = 0x8000;
    }

    ac97_write_nabm32(state, AC97_NABM_OFFSET_PO_BDBAR, state->bdl_phys);
    debuglog(DEBUG_INFO, "AC97: BDL programmed at 0x%08X\n", state->bdl_phys);
}

static void ac97_reset_channel(ac97_state_t* state) {
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, AC97_PO_CR_RESET);
    for (volatile int i = 0; i < 1000; i++) { }
    ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, 0);
    for (volatile int i = 0; i < 1000; i++) { }
    debuglog(DEBUG_INFO, "AC97: Channel reset\n");
}

static bool ac97_init(SoundDriver* driver) {
    if (!driver || !driver->state) {
        return false;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;

    debuglog(DEBUG_INFO, "AC97: Starting init...\n");

    if (!ac97_init_codec(state)) {
        debuglog(DEBUG_ERROR, "AC97: Codec init failed\n");
        return false;
    }

    if (!ac97_allocate_dma_buffers(state)) {
        debuglog(DEBUG_ERROR, "AC97: DMA buffers failed\n");
        return false;
    }

    state->irq = pci_config_read8(state->pci.segment, state->pci.bus, 
                                  state->pci.device, state->pci.function, 0x3C);
    debuglog(DEBUG_INFO, "AC97: IRQ %u\n", state->irq);

    if (state->irq != 0xFF && state->irq != 0) {
        interrupt_set_handler_legacy(0x20 + state->irq, ac97_interrupt_handler);
        debuglog(DEBUG_INFO, "AC97: IRQ handler registered\n");
    }

    ac97_program_bdl(state);
    ac97_reset_channel(state);

    state->current_sample_rate = 48000;
    ac97_write_nam(state, AC97_NAM_OFFSET_FRONT_DAC_RATE, state->current_sample_rate);

    uint32_t ctrl = ac97_read_nabm32(state, AC97_NABM_OFFSET_GLOBAL_CTRL);
    ctrl |= AC97_GLOBAL_CTRL_SRIE;
    ac97_write_nabm32(state, AC97_NABM_OFFSET_GLOBAL_CTRL, ctrl);

    state->initialized = true;
    debuglog(DEBUG_INFO, "AC97: Driver ready\n");
    return true;
}

static bool ac97_play_pcm(SoundDriver* driver, const uint8_t* data, uint32_t length, const SoundFormat* format) {
    if (!driver || !driver->state || !data || !format) {
        return false;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }

    if (length > state->dma_buffer_size) {
        length = state->dma_buffer_size;
    }

    memcpy(state->dma_buffer, data, length);

    if (format->sample_rate != state->current_sample_rate) {
        state->current_sample_rate = format->sample_rate;
        ac97_write_nam(state, AC97_NAM_OFFSET_FRONT_DAC_RATE, format->sample_rate);
    }

    uint8_t cr = ac97_read_nabm_byte(state, AC97_NABM_OFFSET_PO_CR);
    if (!(cr & AC97_PO_CR_RUN)) {
        ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, 0);
        ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CIV, 0);
        ac97_write_nabm32(state, AC97_NABM_OFFSET_PO_SR, 0x1C);
        ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_LVI, AC97_BUFFER_ENTRIES - 1);
        ac97_write_nabm_byte(state, AC97_NABM_OFFSET_PO_CR, 
                            AC97_PO_CR_RUN | AC97_PO_CR_IOCE);
        g_ac97_streaming = true;
        debuglog(DEBUG_INFO, "AC97: Play started\n");
    }

    return true;
}

static void ac97_set_volume(SoundDriver* driver, uint8_t volume) {
    if (!driver || !driver->state) {
        return;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    if (!state->initialized) {
        return;
    }
    uint16_t v = (uint16_t)((255 - volume) >> 3);
    uint16_t reg = (v << 8) | v;
    ac97_write_nam(state, AC97_NAM_OFFSET_PCM_VOL, reg);
    ac97_write_nam(state, AC97_NAM_OFFSET_MASTER_VOL, reg);
}

static void ac97_beep(SoundDriver* driver, uint32_t frequency_hz, uint32_t duration_ms) {
    (void)driver;
    (void)frequency_hz;
    (void)duration_ms;
}

static bool ac97_get_capabilities(SoundDriver* driver, DeviceCapabilities* caps) {
    if (!driver || !driver->state || !caps) {
        return false;
    }
    ac97_state_t* state = (ac97_state_t*)driver->state;
    if (!state->initialized) {
        return false;
    }
    
    memset(caps, 0, sizeof(DeviceCapabilities));
    caps->supported_formats[0] = PCM_S16;
    caps->supported_formats[1] = PCM_U8;
    caps->supported_formats[2] = 0;
    caps->max_channels = 2;
    caps->stereo_supported = true;
    caps->little_endian = true;
    caps->native_sample_rates[0] = 48000;
    caps->native_sample_rates[1] = 44100;
    caps->native_sample_rates[2] = 22050;
    caps->native_sample_rates[3] = 11025;
    caps->native_sample_rates[4] = 0;
    caps->max_buffer_size = state->dma_buffer_size;
    
    return true;
}

static SoundDriver g_ac97_driver = {
    .name = "AC'97",
    .type = SOUND_DEVICE_AC97,
    .detect = ac97_detect,
    .init = ac97_init,
    .play_pcm = ac97_play_pcm,
    .get_capabilities = ac97_get_capabilities,
    .set_volume = ac97_set_volume,
    .beep = ac97_beep,
    .shutdown = NULL,
    .state = 0,
    .volume = 255
};

SoundDriver* sound_ac97_driver(void) {
    return &g_ac97_driver;
}
