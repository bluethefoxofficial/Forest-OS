/**
 * Forest OS - Display Driver Registry (V2 Compatible)
 * 
 * This file manages display driver registration and device matching.
 * It bridges the old driver system with the new V2 architecture.
 */

#include "../include/graphics/display_driver.h"
#include "../include/graphics/graphics_driver_v2.h"
#include "../include/graphics/hardware_detect.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/debuglog.h"

/* ============================================================================
 * Legacy Driver Registry (for backward compatibility)
 * ============================================================================ */

static display_driver_t* registered_drivers = NULL;
static uint32_t driver_count = 0;

graphics_result_t register_display_driver(display_driver_t* driver) {
    if (!driver || !driver->ops) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "[DISPLAY] Registering legacy driver: %s\n", 
            driver->ops->name ? driver->ops->name : "unnamed");
    
    /* Add to linked list */
    driver->next = registered_drivers;
    registered_drivers = driver;
    driver->is_loaded = true;
    driver_count++;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t unregister_display_driver(display_driver_t* driver) {
    if (!driver) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "[DISPLAY] Unregistering driver: %s\n",
            driver->ops->name ? driver->ops->name : "unnamed");
    
    /* Remove from linked list */
    if (registered_drivers == driver) {
        registered_drivers = driver->next;
    } else {
        display_driver_t* current = registered_drivers;
        while (current && current->next != driver) {
            current = current->next;
        }
        if (current) {
            current->next = driver->next;
        }
    }
    
    driver->is_loaded = false;
    driver->next = NULL;
    driver_count--;
    
    return GRAPHICS_SUCCESS;
}

/* ============================================================================
 * Driver Selection Logic
 * ============================================================================ */

/**
 * Get the recommended driver name for a device based on vendor/device ID
 */
const char* get_recommended_driver(uint16_t vendor_id, uint16_t device_id) {
    /* Bochs/QEMU BGA */
    if (vendor_id == 0x1234 && device_id == 0x1111) {
        return "bochs-bga";
    }
    
    /* VMware SVGA */
    if (vendor_id == 0x15AD && (device_id == 0x0405 || device_id == 0x0710)) {
        return "vmware-svga";
    }
    
    /* VirtualBox */
    if (vendor_id == 0x80EE && (device_id == 0xBEEF || device_id == 0xCAFE)) {
        return "bochs-bga";  /* VBox uses BGA-compatible interface */
    }
    
    /* Intel HD Graphics */
    if (vendor_id == 0x8086) {
        return "intel-hd";
    }
    
    /* AMD/ATI */
    if (vendor_id == 0x1002) {
        return "amd-ati";
    }
    
    /* NVIDIA */
    if (vendor_id == 0x10DE) {
        return "nvidia";
    }
    
    /* Default to VESA */
    return "vesa-vbe";
}

/**
 * Find a driver for a device
 */
display_driver_t* find_driver_for_device(graphics_device_t* device) {
    if (!device) {
        return NULL;
    }
    
    const char* recommended = get_recommended_driver(device->vendor_id, device->device_id);
    
    debuglog(DEBUG_INFO, "[DISPLAY] Looking for driver for %s (recommended: %s)\n",
            device->name, recommended ? recommended : "none");
    
    /* Search registered drivers for recommended one */
    if (recommended) {
        display_driver_t* current = registered_drivers;
        while (current) {
            if (current->ops->name && strcmp(current->ops->name, recommended) == 0) {
                debuglog(DEBUG_INFO, "[DISPLAY] Found recommended driver: %s\n", current->ops->name);
                return current;
            }
            current = current->next;
        }
    }
    
    /* Fall back based on device type */
    const char* fallback_name = NULL;
    switch (device->type) {
        case GRAPHICS_DEVICE_VGA:
            fallback_name = "vga-text";
            break;
        case GRAPHICS_DEVICE_VESA:
            fallback_name = "vesa-vbe";
            break;
        case GRAPHICS_DEVICE_BOCHS_VBE:
            fallback_name = "bochs-bga";
            break;
        case GRAPHICS_DEVICE_VMWARE_SVGA:
            fallback_name = "vmware-svga";
            break;
        case GRAPHICS_DEVICE_INTEL_HD:
            fallback_name = "intel-hd";
            break;
        case GRAPHICS_DEVICE_AMD:
            fallback_name = "amd-ati";
            break;
        case GRAPHICS_DEVICE_NVIDIA:
            fallback_name = "nvidia";
            break;
        default:
            fallback_name = "vesa-vbe";
            break;
    }
    
    if (fallback_name) {
        display_driver_t* current = registered_drivers;
        while (current) {
            if (current->ops->name && strcmp(current->ops->name, fallback_name) == 0) {
                debuglog(DEBUG_INFO, "[DISPLAY] Found fallback driver: %s\n", current->ops->name);
                return current;
            }
            current = current->next;
        }
    }
    
    /* Last resort: return first available driver */
    if (registered_drivers) {
        debuglog(DEBUG_INFO, "[DISPLAY] Using first available driver: %s\n",
                registered_drivers->ops->name ? registered_drivers->ops->name : "unnamed");
        return registered_drivers;
    }
    
    debuglog(DEBUG_WARN, "[DISPLAY] No suitable driver found for device %s\n", device->name);
    return NULL;
}

/**
 * Load a driver for a device
 */
graphics_result_t load_driver_for_device(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    display_driver_t* driver = find_driver_for_device(device);
    if (!driver) {
        debuglog(DEBUG_WARN, "[DISPLAY] No driver found for device %s\n", device->name);
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    device->driver = driver;
    
    debuglog(DEBUG_INFO, "[DISPLAY] Loaded driver %s for device %s\n",
            driver->ops->name ? driver->ops->name : "unnamed", device->name);
    
    return GRAPHICS_SUCCESS;
}

/**
 * Try to load a specific driver by name
 */
graphics_result_t try_load_driver(const char* driver_name, graphics_device_t* device) {
    if (!driver_name || !device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    display_driver_t* current = registered_drivers;
    while (current) {
        if (current->ops->name && strcmp(current->ops->name, driver_name) == 0) {
            device->driver = current;
            debuglog(DEBUG_INFO, "[DISPLAY] Loaded driver %s for device %s\n",
                    driver_name, device->name);
            return GRAPHICS_SUCCESS;
        }
        current = current->next;
    }
    
    debuglog(DEBUG_WARN, "[DISPLAY] Driver %s not found\n", driver_name);
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

/**
 * Fall back to VESA driver
 */
graphics_result_t fallback_to_vesa_driver(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "[DISPLAY] Attempting VESA fallback for device %s\n", device->name);
    return try_load_driver("vesa-vbe", device);
}

/**
 * Fall back to text mode driver
 */
graphics_result_t fallback_to_text_driver(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "[DISPLAY] Attempting text mode fallback for device %s\n", device->name);
    
    /* Try VGA text mode first */
    graphics_result_t result = try_load_driver("vga-text", device);
    if (result == GRAPHICS_SUCCESS) {
        return result;
    }
    
    /* Find any text mode capable driver */
    display_driver_t* current = registered_drivers;
    while (current) {
        if (current->flags & DRIVER_FLAG_SUPPORTS_TEXT_MODE) {
            device->driver = current;
            debuglog(DEBUG_INFO, "[DISPLAY] Using text mode fallback: %s\n",
                    current->ops->name ? current->ops->name : "unnamed");
            return GRAPHICS_SUCCESS;
        }
        current = current->next;
    }
    
    debuglog(DEBUG_ERROR, "[DISPLAY] No text mode fallback available\n");
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

/* ============================================================================
 * Driver Enumeration
 * ============================================================================ */

/**
 * Get the number of registered drivers
 */
uint32_t get_registered_driver_count(void) {
    return driver_count;
}

/**
 * Get a registered driver by index
 */
display_driver_t* get_registered_driver(uint32_t index) {
    display_driver_t* current = registered_drivers;
    uint32_t i = 0;
    
    while (current && i < index) {
        current = current->next;
        i++;
    }
    
    return current;
}

/**
 * Find a registered driver by name
 */
display_driver_t* find_driver_by_name(const char* name) {
    if (!name) {
        return NULL;
    }
    
    display_driver_t* current = registered_drivers;
    while (current) {
        if (current->ops->name && strcmp(current->ops->name, name) == 0) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

/**
 * Print all registered drivers
 */
void print_registered_drivers(void) {
    debuglog(DEBUG_INFO, "=== Registered Display Drivers ===\n");
    
    display_driver_t* current = registered_drivers;
    uint32_t index = 0;
    
    while (current) {
        debuglog(DEBUG_INFO, "  [%u] %s (loaded: %s, flags: 0x%08x)\n",
                index,
                current->ops->name ? current->ops->name : "unnamed",
                current->is_loaded ? "yes" : "no",
                current->flags);
        current = current->next;
        index++;
    }
    
    debuglog(DEBUG_INFO, "Total: %u drivers\n", driver_count);
}

/* ============================================================================
 * Driver Initialization Helpers
 * ============================================================================ */

/**
 * Auto-detect and initialize the best driver for the system
 * This is called during graphics subsystem initialization
 */
graphics_result_t auto_detect_and_load_driver(void) {
    debuglog(DEBUG_INFO, "[DISPLAY] Auto-detecting graphics hardware...\n");
    
    /* The V2 system handles all driver probing and selection automatically */
    /* This function now just validates that the V2 system is working */
    
    extern bool gfx_is_initialized(void);
    if (gfx_is_initialized()) {
        debuglog(DEBUG_INFO, "[DISPLAY] V2 graphics system is active\n");
        return GRAPHICS_SUCCESS;
    }
    
    debuglog(DEBUG_WARN, "[DISPLAY] V2 graphics system not initialized\n");
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

/**
 * Initialize all available drivers
 * This registers drivers with the system for later selection
 */
graphics_result_t initialize_all_drivers(void) {
    debuglog(DEBUG_INFO, "[DISPLAY] Initializing driver registry...\n");
    
    /* The V2 system initializes drivers in gfx_init() */
    /* This is kept for backward compatibility */
    
    return GRAPHICS_SUCCESS;
}
