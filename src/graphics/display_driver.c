#include "../include/graphics/display_driver.h"
#include "../include/graphics/hardware_detect.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/debuglog.h"

// Global driver registry
static display_driver_t* registered_drivers = NULL;

graphics_result_t register_display_driver(display_driver_t* driver) {
    if (!driver || !driver->ops) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Registering display driver: %s\n", 
            driver->ops->name ? driver->ops->name : "unnamed");
    
    // Add to linked list of registered drivers
    driver->next = registered_drivers;
    registered_drivers = driver;
    driver->is_loaded = true;
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t unregister_display_driver(display_driver_t* driver) {
    if (!driver) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Unregistering display driver: %s\n",
            driver->ops->name ? driver->ops->name : "unnamed");
    
    // Remove from linked list
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
    
    return GRAPHICS_SUCCESS;
}

display_driver_t* find_driver_for_device(graphics_device_t* device) {
    if (!device) {
        return NULL;
    }
    
    const char* recommended_driver = get_recommended_driver(device->vendor_id, device->device_id);
    
    debuglog(DEBUG_INFO, "Looking for driver for device %s (recommended: %s)\n",
            device->name, recommended_driver ? recommended_driver : "none");
    
    // First, try to find the recommended driver
    if (recommended_driver) {
        display_driver_t* current = registered_drivers;
        while (current) {
            if (current->ops->name && strcmp(current->ops->name, recommended_driver) == 0) {
                debuglog(DEBUG_INFO, "Found recommended driver: %s\n", current->ops->name);
                return current;
            }
            current = current->next;
        }
    }
    
    // If no specific driver found, look for a generic driver based on device type
    const char* generic_driver = NULL;
    switch (device->type) {
        case GRAPHICS_DEVICE_VGA:
            generic_driver = "vga_text";
            break;
        case GRAPHICS_DEVICE_VESA:
            generic_driver = "vesa";
            break;
        case GRAPHICS_DEVICE_BOCHS_VBE:
            generic_driver = "bochs_bga";
            break;
        case GRAPHICS_DEVICE_VMWARE_SVGA:
            generic_driver = "vmware_svga";
            break;
        default:
            generic_driver = "vesa"; // VESA is most compatible
            break;
    }
    
    if (generic_driver) {
        display_driver_t* current = registered_drivers;
        while (current) {
            if (current->ops->name && strcmp(current->ops->name, generic_driver) == 0) {
                debuglog(DEBUG_INFO, "Found generic driver: %s\n", current->ops->name);
                return current;
            }
            current = current->next;
        }
    }
    
    // Last resort: find any driver that supports text mode (fallback)
    display_driver_t* current = registered_drivers;
    while (current) {
        if (current->flags & DRIVER_FLAG_SUPPORTS_TEXT_MODE) {
            debuglog(DEBUG_INFO, "Using fallback text driver: %s\n", 
                    current->ops->name ? current->ops->name : "unnamed");
            return current;
        }
        current = current->next;
    }
    
    debuglog(DEBUG_WARN, "No suitable driver found for device %s\n", device->name);
    return NULL;
}

graphics_result_t load_driver_for_device(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    display_driver_t* driver = find_driver_for_device(device);
    if (!driver) {
        debuglog(DEBUG_WARN, "No driver found for device %s\n", device->name);
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    // Associate driver with device
    device->driver = driver;
    
    debuglog(DEBUG_INFO, "Loaded driver %s for device %s\n",
            driver->ops->name ? driver->ops->name : "unnamed", device->name);
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t try_load_driver(const char* driver_name, graphics_device_t* device) {
    if (!driver_name || !device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    display_driver_t* current = registered_drivers;
    while (current) {
        if (current->ops->name && strcmp(current->ops->name, driver_name) == 0) {
            device->driver = current;
            debuglog(DEBUG_INFO, "Loaded specific driver %s for device %s\n",
                    driver_name, device->name);
            return GRAPHICS_SUCCESS;
        }
        current = current->next;
    }
    
    debuglog(DEBUG_WARN, "Driver %s not found\n", driver_name);
    return GRAPHICS_ERROR_NOT_SUPPORTED;
}

graphics_result_t fallback_to_vesa_driver(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Attempting VESA driver fallback for device %s\n", device->name);
    return try_load_driver("vesa", device);
}

graphics_result_t fallback_to_text_driver(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    debuglog(DEBUG_INFO, "Attempting text mode fallback for device %s\n", device->name);
    
    // Try VGA text mode first
    graphics_result_t result = try_load_driver("vga_text", device);
    if (result != GRAPHICS_SUCCESS) {
        // If that fails, try any text mode driver
        display_driver_t* current = registered_drivers;
        while (current) {
            if (current->flags & DRIVER_FLAG_SUPPORTS_TEXT_MODE) {
                device->driver = current;
                debuglog(DEBUG_INFO, "Using text mode fallback driver %s\n",
                        current->ops->name ? current->ops->name : "unnamed");
                return GRAPHICS_SUCCESS;
            }
            current = current->next;
        }
        
        debuglog(DEBUG_ERROR, "No text mode fallback driver available\n");
        return GRAPHICS_ERROR_NOT_SUPPORTED;
    }
    
    return result;
}