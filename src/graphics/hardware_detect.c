#include "../include/graphics/hardware_detect.h"
#include "../include/graphics/graphics_manager.h"
#include "../include/pci.h"
#include "../include/memory.h"
#include "../include/string.h"
#include "../include/libc/stdio.h"
#include "../include/io_ports.h"
#include "../include/debuglog.h"

// Global hardware state
graphics_hw_state_t graphics_hw_state = {
    .primary_device = NULL,
    .devices = NULL,
    .num_devices = 0,
    .detection_complete = false,
    .fallback_active = false
};

// Graphics device database - known devices and their recommended drivers
const graphics_device_db_entry_t graphics_device_db[] = {
    // Intel devices
    {PCI_VENDOR_INTEL, 0x0042, 0, 0, GRAPHICS_DEVICE_INTEL_HD, "Intel HD Graphics", "intel_hd", 0},
    {PCI_VENDOR_INTEL, 0x0046, 0, 0, GRAPHICS_DEVICE_INTEL_HD, "Intel HD Graphics", "intel_hd", 0},
    {PCI_VENDOR_INTEL, 0x0102, 0, 0, GRAPHICS_DEVICE_INTEL_HD, "Intel HD Graphics 2000", "intel_hd", 0},
    {PCI_VENDOR_INTEL, 0x0112, 0, 0, GRAPHICS_DEVICE_INTEL_HD, "Intel HD Graphics 3000", "intel_hd", 0},
    
    // NVIDIA devices
    {PCI_VENDOR_NVIDIA, 0x0640, 0, 0, GRAPHICS_DEVICE_NVIDIA, "NVIDIA GeForce 9500 GT", "nvidia", 0},
    {PCI_VENDOR_NVIDIA, 0x0641, 0, 0, GRAPHICS_DEVICE_NVIDIA, "NVIDIA GeForce 9400 GT", "nvidia", 0},
    
    // AMD/ATI devices
    {PCI_VENDOR_AMD, 0x6798, 0, 0, GRAPHICS_DEVICE_AMD, "AMD Radeon HD 7900", "amd", 0},
    {PCI_VENDOR_AMD, 0x6799, 0, 0, GRAPHICS_DEVICE_AMD, "AMD Radeon HD 7970", "amd", 0},
    
    // Virtualization devices
    {PCI_VENDOR_VMWARE, PCI_DEVICE_VMWARE_SVGA_II, 0, 0, GRAPHICS_DEVICE_VMWARE_SVGA, "VMware SVGA II", "vmware_svga", 0},
    {PCI_VENDOR_BOCHS, PCI_DEVICE_BOCHS_VGA, 0, 0, GRAPHICS_DEVICE_BOCHS_VBE, "Bochs BGA", "bochs_bga", 0},
    {PCI_VENDOR_VIRTUALBOX, 0xBEEF, 0, 0, GRAPHICS_DEVICE_VESA, "VirtualBox Graphics", "vesa", 0},
};

const size_t graphics_device_db_size = sizeof(graphics_device_db) / sizeof(graphics_device_db[0]);

graphics_result_t detect_graphics_hardware(void) {
    debuglog(DEBUG_INFO, "Starting graphics hardware detection...\n");
    
    // Reset detection state
    graphics_hw_state.detection_complete = false;
    graphics_hw_state.fallback_active = false;
    
    // First try to detect PCI graphics devices
    graphics_result_t result = probe_pci_graphics_devices();
    if (result != GRAPHICS_SUCCESS) {
        debuglog(DEBUG_WARN, "PCI graphics detection failed, checking for legacy VGA\n");
        
        // Fall back to legacy VGA detection
        if (is_vga_present()) {
            graphics_device_t* vga_device = kmalloc(sizeof(graphics_device_t));
            if (vga_device) {
                result = setup_legacy_vga_device(vga_device);
                if (result == GRAPHICS_SUCCESS) {
                    graphics_hw_state.primary_device = vga_device;
                    graphics_hw_state.devices = vga_device;
                    graphics_hw_state.num_devices = 1;
                    graphics_hw_state.fallback_active = true;
                    debuglog(DEBUG_INFO, "Using legacy VGA as fallback\n");
                } else {
                    kfree(vga_device);
                }
            }
        }
    }
    
    // If we still have no devices, we're in trouble
    if (graphics_hw_state.num_devices == 0) {
        debuglog(DEBUG_ERROR, "No graphics hardware detected!\n");
        return GRAPHICS_ERROR_HARDWARE_FAULT;
    }
    
    // Set the first detected device as primary if not set
    if (!graphics_hw_state.primary_device && graphics_hw_state.devices) {
        graphics_hw_state.primary_device = graphics_hw_state.devices;
        debuglog(DEBUG_INFO, "Set primary graphics device: %s\n", 
                graphics_hw_state.primary_device->name);
    }
    
    graphics_hw_state.detection_complete = true;
    debuglog(DEBUG_INFO, "Graphics hardware detection complete. Found %u devices\n", 
            graphics_hw_state.num_devices);
    
    return GRAPHICS_SUCCESS;
}

graphics_result_t probe_pci_graphics_devices(void) {
    uint32_t device_count = 0;
    graphics_device_t* device_list = NULL;
    
    // Scan all PCI buses, slots, and functions
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config_dword(bus, slot, func, 0);
                
                // Skip if no device present
                if ((vendor_device & 0xFFFF) == 0xFFFF) {
                    continue;
                }
                
                uint16_t vendor_id = vendor_device & 0xFFFF;
                uint16_t device_id = (vendor_device >> 16) & 0xFFFF;
                
                // Check if this is a display controller
                uint32_t class_code = pci_read_config_dword(bus, slot, func, 8);
                uint8_t class = (class_code >> 24) & 0xFF;
                uint8_t subclass = (class_code >> 16) & 0xFF;
                
                if (class == PCI_CLASS_DISPLAY) {
                    debuglog(DEBUG_INFO, "Found graphics device: %04x:%04x at %02x:%02x.%x\n",
                            vendor_id, device_id, bus, slot, func);
                    
                    // Allocate new device structure
                    graphics_device_t* new_device = kmalloc(sizeof(graphics_device_t));
                    if (!new_device) {
                        debuglog(DEBUG_ERROR, "Failed to allocate memory for graphics device\n");
                        continue;
                    }
                    
                    // Set up the device
                    graphics_result_t result = setup_graphics_device(new_device, bus, slot, func);
                    if (result != GRAPHICS_SUCCESS) {
                        debuglog(DEBUG_WARN, "Failed to setup graphics device %04x:%04x\n",
                                vendor_id, device_id);
                        kfree(new_device);
                        continue;
                    }
                    
                    // Add to device list (simple linked list for now)
                    if (!device_list) {
                        device_list = new_device;
                        graphics_hw_state.devices = device_list;
                    } else {
                        // Find end of list and append
                        graphics_device_t* current = device_list;
                        while (current && current != new_device) {
                            if (current + 1 == NULL) break;
                            current++;
                        }
                        // For simplicity, we'll use a different approach
                        // This is a minimal implementation - in practice you'd use proper linked lists
                    }
                    
                    device_count++;
                    
                    // Set as primary if it's the first device
                    if (!graphics_hw_state.primary_device) {
                        graphics_hw_state.primary_device = new_device;
                    }
                }
            }
        }
    }
    
    graphics_hw_state.num_devices = device_count;
    
    if (device_count > 0) {
        debuglog(DEBUG_INFO, "Detected %u PCI graphics devices\n", device_count);
        return GRAPHICS_SUCCESS;
    }
    
    return GRAPHICS_ERROR_HARDWARE_FAULT;
}

graphics_result_t setup_graphics_device(graphics_device_t* device, 
                                       uint8_t bus, uint8_t slot, uint8_t func) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Read PCI configuration
    uint32_t vendor_device = pci_read_config_dword(bus, slot, func, 0);
    device->vendor_id = vendor_device & 0xFFFF;
    device->device_id = (vendor_device >> 16) & 0xFFFF;
    device->revision = pci_read_config_byte(bus, slot, func, 8) & 0xFF;
    
    device->bus = bus;
    device->slot = slot;
    device->function = func;
    
    // Identify device type and get name
    device->type = identify_graphics_device(device->vendor_id, device->device_id);
    const char* device_name = get_device_name(device->vendor_id, device->device_id);
    
    if (device_name) {
        strncpy(device->name, device_name, sizeof(device->name) - 1);
        device->name[sizeof(device->name) - 1] = '\0';
    } else {
        snprintf(device->name, sizeof(device->name), "Unknown Graphics (%04x:%04x)", 
                device->vendor_id, device->device_id);
    }
    
    // Read memory regions
    for (int bar = 0; bar < 6; bar++) {
        uint32_t bar_value = pci_read_config_dword(bus, slot, func, 0x10 + (bar * 4));
        
        if (bar_value & 1) {
            // I/O space - skip for graphics devices
            continue;
        }
        
        // Memory space
        if (bar == 0) {
            // Usually framebuffer
            device->framebuffer_base = bar_value & 0xFFFFFFF0;
            // Read size by writing all 1s and reading back
            pci_write_config_dword(bus, slot, func, 0x10 + (bar * 4), 0xFFFFFFFF);
            uint32_t size_mask = pci_read_config_dword(bus, slot, func, 0x10 + (bar * 4));
            pci_write_config_dword(bus, slot, func, 0x10 + (bar * 4), bar_value);
            device->framebuffer_size = ~(size_mask & 0xFFFFFFF0) + 1;
        } else if (bar == 1) {
            // Usually MMIO registers
            device->mmio_base = bar_value & 0xFFFFFFF0;
            pci_write_config_dword(bus, slot, func, 0x10 + (bar * 4), 0xFFFFFFFF);
            uint32_t size_mask = pci_read_config_dword(bus, slot, func, 0x10 + (bar * 4));
            pci_write_config_dword(bus, slot, func, 0x10 + (bar * 4), bar_value);
            device->mmio_size = ~(size_mask & 0xFFFFFFF0) + 1;
        }
    }
    
    // Initialize device state
    device->current_fb = NULL;
    device->is_active = false;
    device->driver = NULL;
    
    // Set default capabilities (will be updated by driver)
    memset(&device->caps, 0, sizeof(device->caps));
    device->caps.supports_2d_accel = false;
    device->caps.supports_3d_accel = false;
    device->caps.supports_hw_cursor = false;
    device->caps.max_resolution_x = 1024;
    device->caps.max_resolution_y = 768;
    
    return map_device_memory(device);
}

graphics_result_t map_device_memory(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // For now, just enable memory access
    // In a full implementation, you'd map the framebuffer and MMIO regions
    return enable_device(device);
}

graphics_result_t enable_device(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    // Enable memory and I/O access, and bus mastering
    uint16_t command = pci_read_config_word(device->bus, device->slot, device->function, 4);
    command |= 0x07; // Memory space, I/O space, bus master
    pci_write_config_word(device->bus, device->slot, device->function, 4, command);
    
    debuglog(DEBUG_INFO, "Enabled graphics device %s\n", device->name);
    return GRAPHICS_SUCCESS;
}

graphics_device_type_t identify_graphics_device(uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0; i < graphics_device_db_size; i++) {
        if (graphics_device_db[i].vendor_id == vendor_id && 
            graphics_device_db[i].device_id == device_id) {
            return graphics_device_db[i].type;
        }
    }
    
    // Check by vendor for generic identification
    switch (vendor_id) {
        case PCI_VENDOR_INTEL:
            return GRAPHICS_DEVICE_INTEL_HD;
        case PCI_VENDOR_NVIDIA:
            return GRAPHICS_DEVICE_NVIDIA;
        case PCI_VENDOR_AMD:
            return GRAPHICS_DEVICE_AMD;
        case PCI_VENDOR_VMWARE:
            return GRAPHICS_DEVICE_VMWARE_SVGA;
        case PCI_VENDOR_BOCHS:
            return GRAPHICS_DEVICE_BOCHS_VBE;
        default:
            return GRAPHICS_DEVICE_UNKNOWN;
    }
}

const char* get_device_name(uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0; i < graphics_device_db_size; i++) {
        if (graphics_device_db[i].vendor_id == vendor_id && 
            graphics_device_db[i].device_id == device_id) {
            return graphics_device_db[i].name;
        }
    }
    return NULL;
}

const char* get_recommended_driver(uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0; i < graphics_device_db_size; i++) {
        if (graphics_device_db[i].vendor_id == vendor_id && 
            graphics_device_db[i].device_id == device_id) {
            return graphics_device_db[i].driver_name;
        }
    }
    
    // Fallback recommendations by vendor
    switch (vendor_id) {
        case PCI_VENDOR_VMWARE:
            return "vmware_svga";
        case PCI_VENDOR_BOCHS:
            return "bochs_bga";
        default:
            return "vesa"; // VESA is the most compatible fallback
    }
}

bool is_vga_present(void) {
    // Check for VGA by reading the VGA status register
    // This is a basic check - port 0x3DA is the VGA status register
    uint8_t status = inportb(0x3DA);
    
    // If we can read something meaningful, VGA is likely present
    // VGA status register has specific bit patterns
    return (status != 0xFF && status != 0x00);
}

graphics_result_t setup_legacy_vga_device(graphics_device_t* device) {
    if (!device) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    memset(device, 0, sizeof(graphics_device_t));
    
    device->vendor_id = 0x0000;
    device->device_id = 0x0000;
    device->type = GRAPHICS_DEVICE_VGA;
    strncpy(device->name, "Legacy VGA", sizeof(device->name) - 1);
    
    // VGA memory is always at 0xA0000
    device->framebuffer_base = 0xA0000;
    device->framebuffer_size = 0x20000; // 128KB
    
    // Set basic capabilities
    device->caps.supports_2d_accel = false;
    device->caps.supports_3d_accel = false;
    device->caps.supports_hw_cursor = false;
    device->caps.max_resolution_x = 80;  // Text mode columns
    device->caps.max_resolution_y = 25;  // Text mode rows
    
    debuglog(DEBUG_INFO, "Set up legacy VGA device\n");
    return GRAPHICS_SUCCESS;
}

graphics_result_t enumerate_graphics_devices(graphics_device_t** devices, uint32_t* count) {
    if (!devices || !count) {
        return GRAPHICS_ERROR_INVALID_PARAMETER;
    }
    
    if (!graphics_hw_state.detection_complete) {
        graphics_result_t result = detect_graphics_hardware();
        if (result != GRAPHICS_SUCCESS) {
            return result;
        }
    }
    
    *devices = graphics_hw_state.devices;
    *count = graphics_hw_state.num_devices;
    
    return GRAPHICS_SUCCESS;
}

// ---------------------------------------------------------------------------
// PCI configuration access helpers
// ---------------------------------------------------------------------------
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_config_read32(0, bus, slot, func, offset);
}

uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_config_read16(0, bus, slot, func, offset);
}

uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return pci_config_read8(0, bus, slot, func, offset);
}

void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    pci_config_write32(0, bus, slot, func, offset, value);
}

void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    pci_config_write16(0, bus, slot, func, offset, value);
}

void pci_write_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    pci_config_write8(0, bus, slot, func, offset, value);
}
