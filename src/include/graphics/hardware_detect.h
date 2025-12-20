#ifndef HARDWARE_DETECT_H
#define HARDWARE_DETECT_H

#include "graphics_types.h"
#include "display_driver.h"

// PCI vendor and device IDs for graphics hardware
#define PCI_VENDOR_INTEL       0x8086
#define PCI_VENDOR_NVIDIA      0x10DE
#define PCI_VENDOR_AMD         0x1002
#define PCI_VENDOR_ATI         0x1002  // Same as AMD
#define PCI_VENDOR_VMWARE      0x15AD
#define PCI_VENDOR_VIRTUALBOX  0x80EE
#define PCI_VENDOR_BOCHS       0x1234

// Common graphics device IDs
#define PCI_DEVICE_INTEL_HD_GRAPHICS    0x0042
#define PCI_DEVICE_VMWARE_SVGA_II       0x0405
#define PCI_DEVICE_BOCHS_VGA            0x1111

// PCI class codes for display controllers
#define PCI_CLASS_DISPLAY               0x03
#define PCI_SUBCLASS_VGA                0x00
#define PCI_SUBCLASS_XGA                0x01
#define PCI_SUBCLASS_3D                 0x02
#define PCI_SUBCLASS_OTHER              0x80

// Graphics device database entry
typedef struct graphics_device_db_entry {
    uint16_t vendor_id;
    uint16_t device_id;
    uint16_t subsystem_vendor;
    uint16_t subsystem_device;
    graphics_device_type_t type;
    const char* name;
    const char* driver_name;
    uint32_t flags;
} graphics_device_db_entry_t;

// Hardware detection functions
graphics_result_t detect_graphics_hardware(void);
graphics_result_t enumerate_graphics_devices(graphics_device_t** devices, uint32_t* count);
graphics_result_t probe_pci_graphics_devices(void);

// Device identification
graphics_device_type_t identify_graphics_device(uint16_t vendor_id, uint16_t device_id);
const char* get_device_name(uint16_t vendor_id, uint16_t device_id);
const char* get_recommended_driver(uint16_t vendor_id, uint16_t device_id);

// PCI configuration space access
uint32_t pci_read_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_read_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_config_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_write_config_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_write_config_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

// Device setup
graphics_result_t setup_graphics_device(graphics_device_t* device, 
                                       uint8_t bus, uint8_t slot, uint8_t func);
graphics_result_t map_device_memory(graphics_device_t* device);
graphics_result_t enable_device(graphics_device_t* device);

// Legacy VGA detection
bool is_vga_present(void);
graphics_result_t setup_legacy_vga_device(graphics_device_t* device);

// Driver loading and matching
graphics_result_t load_driver_for_device(graphics_device_t* device);
graphics_result_t try_load_driver(const char* driver_name, graphics_device_t* device);
graphics_result_t fallback_to_vesa_driver(graphics_device_t* device);
graphics_result_t fallback_to_text_driver(graphics_device_t* device);

// Known graphics devices database
extern const graphics_device_db_entry_t graphics_device_db[];
extern const size_t graphics_device_db_size;

// Hardware detection state
typedef struct {
    graphics_device_t* primary_device;
    graphics_device_t* devices;
    uint32_t num_devices;
    bool detection_complete;
    bool fallback_active;
} graphics_hw_state_t;

extern graphics_hw_state_t graphics_hw_state;

#endif // HARDWARE_DETECT_H