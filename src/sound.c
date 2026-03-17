#include "include/sound.h"
#include "include/vfs.h"
#include "include/screen.h"
#include "include/libc/string.h"
#include "include/timer.h"
#include "include/audio_wav.h"
#include "include/memory.h"
#include "include/thread.h"

// Forward declaration for PCM device
// Stub implementations since sound_pcm_device.c is excluded
int sound_pcm_init(void) {
    // PCM device not available
    return -1;
}

void sound_pcm_cleanup(void) {
    // Nothing to cleanup
}

// Kernel heap allocators
extern void* kmalloc(size_t size);

// Device-specific conversion functions
extern uint32_t convert_to_sb16_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                                   uint8_t* dst, uint32_t dst_max_frames, PcmFormat target_format,
                                   uint16_t target_channels, uint32_t target_rate);
extern uint32_t convert_to_ac97_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                                    int16_t* dst, uint32_t dst_max_frames, uint32_t* out_sample_rate);
extern uint32_t convert_to_hda_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                                  int16_t* dst, uint32_t dst_max_frames);
extern uint32_t convert_to_ensoniq_pcm(const void* src, uint32_t src_frames, const PcmDesc* src_desc,
                                      int16_t* dst, uint32_t dst_max_frames);

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

typedef SoundDriver* (*sound_driver_factory_t)(void);

// Sound request structure for the sound thread
typedef struct sound_request {
    char path[256];                    // Path to the sound file
    struct completion completion;      // Completion for synchronization
    bool success;                      // Result of the operation
} sound_request_t;

#define SOUND_QUEUE_SIZE 16
static sound_request_t g_sound_queue[SOUND_QUEUE_SIZE];
static volatile int g_sound_queue_head = 0;
static volatile int g_sound_queue_tail = 0;
static struct semaphore g_sound_queue_sem;
static struct thread* g_sound_thread = NULL;
static volatile bool g_sound_thread_running = false;

static SoundDriver* g_active_driver = 0;

// Forward declarations
static bool sound_play_wav_sync(const char* path);
static void sound_thread_init(void);
static void sound_thread_shutdown(void);

// Sound thread main function
static void* sound_thread_main(void* arg) {
    (void)arg;

    print("[SOUND] Sound processing thread started\n");

    while (g_sound_thread_running) {
        // Wait for sound requests
        if (semaphore_down_timeout(&g_sound_queue_sem, 100) == 0) {
            // Process the next request
            sound_request_t* request = &g_sound_queue[g_sound_queue_head];

            // Process the WAV file
            request->success = sound_play_wav_sync(request->path);

            // Signal completion
            complete(&request->completion);

            // Move to next request
            g_sound_queue_head = (g_sound_queue_head + 1) % SOUND_QUEUE_SIZE;
        } else {
            // Timeout - yield to other threads
            thread_yield();
        }
    }

    print("[SOUND] Sound processing thread exiting\n");
    return NULL;
}

static SoundDriver* fallback_pc_driver(void) {
    SoundDriver* driver = sound_pc_speaker_driver();
    if (!driver) {
        return 0;
    }
    if (driver->detect) {
        driver->detect(driver);
    }
    if (driver->init) {
        driver->init(driver);
    }
    return driver;
}



static void log_driver_failure(const char* driver_name, const char* reason) {
    print("[SOUND] ");
    print(driver_name ? driver_name : "Unknown");
    print(" unavailable: ");
    print(reason ? reason : "no reason given");
    print("\n");
}

bool sound_system_init(void) {
    if (g_active_driver) {
        return true;
    }

    sound_driver_factory_t factories[] = {
        sound_hda_driver,
        sound_ac97_driver,
        sound_ensoniq_driver,
        sound_sb16_driver,
        sound_sbpro_driver,
        sound_opl3_driver,
        sound_usb_sound_driver,
        sound_pc_speaker_driver,
        sound_universal_driver  // Fallback universal driver
    };

    const uint32 factory_count = sizeof(factories) / sizeof(factories[0]);

    for (uint32 i = 0; i < factory_count; i++) {
        // Safely call driver factory with error checking
        SoundDriver* driver = 0;
        if (factories[i]) {
            print("[SOUND] Trying driver factory #");
            print_dec(i);
            print("...\n");
            
            // Call factory function with safety
            driver = factories[i]();
            
            print("[SOUND] Factory #");
            print_dec(i);
            print(" returned: ");
            print_hex((uint32)driver);
            print("\n");
        }
        
        if (!driver) {
            print("[SOUND] No driver from factory #");
            print_dec(i);
            print("\n");
            continue;
        }
        bool detected = true;
        if (driver->detect) {
            print("[SOUND] Running detection for driver: ");
            print(driver->name ? driver->name : "unknown");
            print("\n");
            
            // Call detect with error handling
            detected = driver->detect(driver);
            
            print("[SOUND] Detection result for ");
            print(driver->name ? driver->name : "unknown");
            print(": ");
            print(detected ? "SUCCESS" : "NOT DETECTED");
            print("\n");
        }
        if (!detected) {
            continue;
        }
        if (!driver->init || !driver->init(driver)) {
            log_driver_failure(driver->name, "init failed");
            continue;
        }

        g_active_driver = driver;
        print("[SOUND] Active driver: ");
        print(driver->name);
        print("\n");
        
        // Initialize PCM device now that we have a driver
        sound_pcm_init();
        
        return true;
    }

    print("[SOUND] No usable sound devices detected.\n");
    return false;
}

void sound_shutdown(void) {
    // Shut down sound thread first
    sound_thread_shutdown();
    
    // Clean up PCM device
    sound_pcm_cleanup();

    if (g_active_driver && g_active_driver->shutdown) {
        g_active_driver->shutdown(g_active_driver);
    }
    g_active_driver = 0;
}

const SoundDriver* sound_active_driver(void) {
    return g_active_driver;
}

// Synchronous WAV playback (internal function)
static bool sound_play_wav_sync(const char* path) {
    if (!path) {
        return false;
    }

    if (!g_active_driver && !sound_system_init()) {
        return false;
    }

    if (!g_active_driver || !g_active_driver->play_pcm || !g_active_driver->get_capabilities) {
        return false;
    }

    print("[SOUND] Playing WAV: ");
    print(path);
    print("\n");

    // Get device capabilities
    DeviceCapabilities caps;
    if (!g_active_driver->get_capabilities ||
        !g_active_driver->get_capabilities(g_active_driver, &caps) ||
        caps.supported_formats[0] == 0 || caps.max_buffer_size == 0) {
        print("[SOUND] Device does not support PCM playback\n");
        return false;
    }

    // Choose target format based on capabilities
    PcmFormat target_format = caps.supported_formats[0]; // Use first supported format
    if (target_format != PCM_U8 && target_format != PCM_S16) {
        target_format = PCM_S16; // Default to 16-bit signed
    }

    // Open file for streaming
    vfs_node_t* file = vfs_open(path, 0);
    if (!file) {
        print("[SOUND] Failed to open file: ");
        print(path);
        print("\n");
        return false;
    }

    uint32 file_size = file->length;
    if (file_size < 44) { // Minimum WAV header size
        print("[SOUND] File too small for WAV: ");
        print(path);
        print("\n");
        vfs_close(file);
        return false;
    }

    // Read WAV header (large enough to cover INFO/bext/fact chunks)
    uint8 header[4096];
    uint32 header_size = MIN(file_size, sizeof(header));
    uint32 bytes_read = vfs_read(file, 0, header_size, header);
    if (bytes_read < 44) {
        print("[SOUND] Failed to read WAV header\n");
        vfs_close(file);
        return false;
    }

    // Parse header, expanding the read window if fmt/data are beyond the initial read
    wav_info_t wav_info;
    wav_error_t wav_error = wav_parse_header(header, bytes_read, &wav_info);
    while ((wav_error == WAV_ERROR_NO_DATA_CHUNK || wav_error == WAV_ERROR_NO_FMT_CHUNK) &&
           header_size < file_size && header_size < sizeof(header)) {
        uint32 new_size = MIN(file_size, header_size + 512);
        if (new_size == header_size) {
            break;
        }
        header_size = new_size;
        bytes_read = vfs_read(file, 0, header_size, header);
        wav_error = wav_parse_header(header, bytes_read, &wav_info);
    }
    if (wav_error != WAV_OK) {
        print("[SOUND] WAV parse error: ");
        print(wav_error_string(wav_error));
        print(" - ");
        print(path);
        print("\n");
        vfs_close(file);
        return false;
    }

    // Validate WAV format is supported
    if (wav_info.format_code != WAV_FORMAT_PCM &&
        wav_info.format_code != WAV_FORMAT_IEEE_FLOAT &&
        wav_info.format_code != WAV_FORMAT_ALAW &&
        wav_info.format_code != WAV_FORMAT_MULAW) {
        print("[SOUND] Unsupported WAV format: 0x");
        print_hex(wav_info.format_code);
        print(" - ");
        print(path);
        print("\n");
        vfs_close(file);
        return false;
    }

    if (wav_info.channels < 1 || wav_info.channels > 2) {
        print("[SOUND] Unsupported channel count: ");
        print_dec(wav_info.channels);
        print(" - ");
        print(path);
        print("\n");
        vfs_close(file);
        return false;
    }

    if (wav_info.sample_rate < 8000 || wav_info.sample_rate > 48000) {
        print("[SOUND] Unsupported sample rate: ");
        print_dec(wav_info.sample_rate);
        print(" Hz - ");
        print(path);
        print("\n");
        vfs_close(file);
        return false;
    }

    // Calculate data offset and size
    uint32 data_offset = wav_info.data_offset;
    if (wav_info.data_ptr && data_offset == 0) {
        // Fallback for legacy fields: pointer is inside header buffer
        data_offset = (uint32)(wav_info.data_ptr - header);
    }
    uint32 data_size = wav_info.data_size;
    if (data_offset >= file_size) {
        print("[SOUND] WAV data offset outside file\n");
        vfs_close(file);
        return false;
    }
    if (data_offset + data_size > file_size) {
        data_size = file_size - data_offset;
    }
    uint32 remaining_data = data_size;

    // Allocate buffers for streaming conversion
    const uint32 raw_chunk_size = 16384;              // Raw bytes read per iteration
    const uint32 canonical_buffer_size = raw_chunk_size * 2; // Worst-case expansion (8-bit -> 16-bit)
    uint32 device_buffer_size = caps.max_buffer_size;
    uint32 total_buffer_size = raw_chunk_size + canonical_buffer_size + device_buffer_size;

    uint8* buffer = kmalloc(total_buffer_size);
    if (!buffer) {
        print("[SOUND] Failed to allocate conversion buffer\n");
        vfs_close(file);
        return false;
    }

    uint8* raw_buffer = buffer;
    uint8* canonical_buffer = raw_buffer + raw_chunk_size;
    uint8* device_buffer = canonical_buffer + canonical_buffer_size;

    bool success = true;
    uint32 file_offset = data_offset;

    // Stream and convert data in chunks
    while (remaining_data > 0 && success) {
        uint32 chunk_size = MIN(remaining_data, raw_chunk_size);

        // Read chunk from file
        bytes_read = vfs_read(file, file_offset, chunk_size, raw_buffer);
        if (bytes_read == 0) {
            break; // EOF
        }

        // Step 1: Decode to canonical PCM (S16)
        PcmDesc canonical_desc;
        uint32 canonical_size;
        wav_error_t decode_err = wav_decode_to_canonical(&wav_info, raw_buffer, bytes_read,
                                                        canonical_buffer, canonical_buffer_size,
                                                        &canonical_size, &canonical_desc);
        if (decode_err != WAV_OK) {
            print("[SOUND] WAV decode error: ");
            print(wav_error_string(decode_err));
            print("\n");
            success = false;
            break;
        }

        uint32 canonical_frames = canonical_size / (sizeof(int16_t) * canonical_desc.channels);

        // Step 3: Apply device-specific conversion and create SoundFormat
        SoundFormat format;
        uint32 device_frames = 0;
        uint32_t target_rate = 22050;
        uint16_t target_channels = 1;
        PcmFormat target_format = PCM_U8;

        if (g_active_driver->type == SOUND_DEVICE_SOUND_BLASTER16) {
            // Choose best SB16 format based on capabilities and input
            target_format = PCM_U8; // Default to 8-bit
            target_channels = 1;
            target_rate = 22050;

            // Prefer 16-bit if supported and input is suitable
            if ((caps.supported_formats[1] == PCM_S16) &&
                (canonical_desc.sample_rate <= 44100) &&
                (canonical_desc.channels <= caps.max_channels)) {
                target_format = PCM_S16;
                target_channels = (caps.stereo_supported && canonical_desc.channels == 2) ? 2 : 1;
                target_rate = (canonical_desc.sample_rate <= 22050) ? 22050 :
                             (canonical_desc.sample_rate <= 44100) ? 44100 : 22050;
            }

            uint32 bytes_per_sample = (target_format == PCM_S16) ? 2u : 1u;
            uint32 frames_capacity = device_buffer_size / (bytes_per_sample * target_channels);

            device_frames = convert_to_sb16_pcm(canonical_buffer, canonical_frames, &canonical_desc,
                                              device_buffer, frames_capacity,
                                              target_format, target_channels, target_rate);

            format.sample_rate = target_rate;
            format.channels = target_channels;
            format.bits_per_sample = (target_format == PCM_S16) ? 16 : 8;
            format.signed_samples = (target_format == PCM_S16);
        } else if (g_active_driver->type == SOUND_DEVICE_AC97) {
            uint32_t target_rate;
            const uint32 bytes_per_sample = 2;
            const uint32 target_channels = 2;
            uint32 frames_capacity = device_buffer_size / (bytes_per_sample * target_channels);
            device_frames = convert_to_ac97_pcm(canonical_buffer, canonical_frames, &canonical_desc,
                                               (int16_t*)device_buffer, frames_capacity, &target_rate);
            format.sample_rate = target_rate;
            format.channels = target_channels;  // AC97 always stereo
            format.bits_per_sample = 16;
            format.signed_samples = true;
        } else if (g_active_driver->type == SOUND_DEVICE_HDA) {
            const uint32 bytes_per_sample = 2;
            const uint32 target_channels = 2; // HDA path writes stereo samples

            uint32 rate_diff_44 = (canonical_desc.sample_rate > 44100) ? (canonical_desc.sample_rate - 44100) : (44100 - canonical_desc.sample_rate);
            uint32 rate_diff_48 = (canonical_desc.sample_rate > 48000) ? (canonical_desc.sample_rate - 48000) : (48000 - canonical_desc.sample_rate);
            uint32 target_rate = (rate_diff_48 < rate_diff_44) ? 48000 : 44100;

            uint32 frames_capacity = device_buffer_size / (bytes_per_sample * target_channels);
            device_frames = convert_to_hda_pcm(canonical_buffer, canonical_frames, &canonical_desc,
                                              (int16_t*)device_buffer, frames_capacity);
            format.sample_rate = target_rate;
            format.channels = target_channels;
            format.bits_per_sample = 16;
            format.signed_samples = true;
        } else if (g_active_driver->type == SOUND_DEVICE_ENSONIQ_AUDIOPCI) {
            const uint32 bytes_per_sample = 2;
            const uint32 target_channels = 2; // Driver always emits stereo

            uint32 rate_diff_44 = (canonical_desc.sample_rate > 44100) ? (canonical_desc.sample_rate - 44100) : (44100 - canonical_desc.sample_rate);
            uint32 rate_diff_48 = (canonical_desc.sample_rate > 48000) ? (canonical_desc.sample_rate - 48000) : (48000 - canonical_desc.sample_rate);
            uint32 target_rate = (rate_diff_48 < rate_diff_44) ? 48000 : 44100;

            uint32 frames_capacity = device_buffer_size / (bytes_per_sample * target_channels);
            device_frames = convert_to_ensoniq_pcm(canonical_buffer, canonical_frames, &canonical_desc,
                                                   (int16_t*)device_buffer, frames_capacity);
            format.sample_rate = target_rate;
            format.channels = target_channels;
            format.bits_per_sample = 16;
            format.signed_samples = true;
        } else if (g_active_driver->type == SOUND_DEVICE_SOUND_BLASTER_PRO ||
                   g_active_driver->type == SOUND_DEVICE_OPL3 ||
                   g_active_driver->type == SOUND_DEVICE_USB_AUDIO ||
                   g_active_driver->type == SOUND_DEVICE_UNIVERSAL) {
            // Universal conversion for new driver types
            const uint32 bytes_per_sample = (caps.supported_formats[0] == PCM_S16) ? 2 : 1;
            const uint32 target_channels = caps.stereo_supported ? 2 : 1;
            const uint32 target_rate = 22050; // Default to 22kHz

            uint32 frames_capacity = device_buffer_size / (bytes_per_sample * target_channels);
            
            // For now, just copy the canonical buffer (S16) if format matches
            if (caps.supported_formats[0] == PCM_S16 && canonical_desc.channels == target_channels) {
                device_frames = MIN(canonical_frames, frames_capacity);
                memcpy(device_buffer, canonical_buffer, device_frames * sizeof(int16_t) * target_channels);
            } else {
                // TODO: Implement proper format conversion for new driver types
                char msg[128];
                string_format(msg, sizeof(msg), "Sound: No conversion available for driver type %u\n", g_active_driver->type);
                print(msg);
                device_frames = 0;
            }
            
            format.sample_rate = target_rate;
            format.channels = target_channels;
            format.bits_per_sample = (caps.supported_formats[0] == PCM_S16) ? 16 : 8;
            format.signed_samples = (caps.supported_formats[0] == PCM_S16);
        }

        if (device_frames == 0) {
            print("[SOUND] Device conversion failed\n");
            success = false;
            break;
        }

        uint32 device_size = device_frames * (format.bits_per_sample / 8) * format.channels;

        // Play the converted chunk
        if (!g_active_driver->play_pcm(g_active_driver, device_buffer, device_size, &format)) {
            print("[SOUND] Failed to play PCM chunk\n");
            success = false;
            break;
        }

        // Give the device time to finish this buffer before reprogramming
        if (format.sample_rate) {
            uint32 chunk_ms = (device_frames * 1000) / format.sample_rate;
            if (chunk_ms == 0) {
                chunk_ms = 1;
            }
            timer_sleep_ms(chunk_ms);
        }

        file_offset += bytes_read;
        remaining_data -= bytes_read;

        // Minimal delay between chunks - previously nops, now handled by sleep
    }

    if (success) {
        print("[SOUND] WAV playback completed\n");
    }

    kfree(buffer);
    vfs_close(file);
    return success;
}

// Thread initialization
static void sound_thread_init(void) {
    if (g_sound_thread != NULL) {
        return; // Already initialized
    }

    // Initialize semaphore
    semaphore_init(&g_sound_queue_sem, 0);

    // Create sound processing thread
    g_sound_thread_running = true;
    g_sound_thread = thread_create("sound_thread", sound_thread_main, NULL);

    if (g_sound_thread) {
        thread_start(g_sound_thread);
        print("[SOUND] Sound thread initialized\n");
    } else {
        print("[SOUND] Failed to create sound thread\n");
        g_sound_thread_running = false;
    }
}

// Thread shutdown
static void sound_thread_shutdown(void) {
    if (g_sound_thread == NULL) {
        return;
    }

    // Signal thread to stop
    g_sound_thread_running = false;

    // Wait for thread to finish (simplified - in real implementation would join)
    void* retval;
    thread_join(g_sound_thread, &retval);

    // Clean up
    thread_destroy(g_sound_thread);
    g_sound_thread = NULL;

    print("[SOUND] Sound thread shut down\n");
}

// Asynchronous WAV playback (public API)
bool sound_play_wav(const char* path) {
    if (!path) {
        return false;
    }

    // Initialize sound thread if needed
    sound_thread_init();

    // Check if queue is full
    int next_tail = (g_sound_queue_tail + 1) % SOUND_QUEUE_SIZE;
    if (next_tail == g_sound_queue_head) {
        print("[SOUND] Sound queue full, dropping request: ");
        print(path);
        print("\n");
        return false;
    }

    // Initialize sound system if needed
    if (!g_active_driver && !sound_system_init()) {
        return false;
    }

    // Add request to queue
    sound_request_t* request = &g_sound_queue[g_sound_queue_tail];
    memset(request, 0, sizeof(sound_request_t));
    strncpy(request->path, path, sizeof(request->path) - 1);
    init_completion(&request->completion);

    // Move tail
    g_sound_queue_tail = next_tail;

    // Signal the sound thread
    semaphore_up(&g_sound_queue_sem);

    print("[SOUND] Queued WAV playback: ");
    print(path);
    print("\n");

    // Fire-and-forget: playback is handled by the sound worker thread.
    // Returning immediately avoids blocking UI threads on long WAV files.
    return true;
}

void sound_beep(uint32 frequency_hz, uint32 duration_ms) {
    if (!g_active_driver || !g_active_driver->beep) {
        SoundDriver* fallback = fallback_pc_driver();
        if (fallback && fallback->beep) {
            fallback->beep(fallback, frequency_hz, duration_ms);
        }
        return;
    }
    g_active_driver->beep(g_active_driver, frequency_hz, duration_ms);
}

void sound_set_volume(uint8 volume) {
    if (!g_active_driver) {
        return;
    }
    g_active_driver->volume = volume;
    if (g_active_driver->set_volume) {
        g_active_driver->set_volume(g_active_driver, volume);
    }
}
