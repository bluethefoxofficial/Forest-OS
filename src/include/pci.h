#ifndef PCI_H
#define PCI_H

#include "types.h"
#include <stdbool.h>

#define PCI_MAX_SEGMENTS      16

#define PCI_MAX_BUS        256
#define PCI_MAX_DEVICE     32
#define PCI_MAX_FUNCTION   8

#define PCI_BAR_COUNT      6

#define PCI_CLASS_MULTIMEDIA      0x04
#define PCI_SUBCLASS_AUDIO        0x01
#define PCI_SUBCLASS_HD_AUDIO     0x03

#define PCI_VENDOR_INTEL          0x8086
#define PCI_VENDOR_ENSONIQ        0x1274
#define PCI_VENDOR_CREATIVE       0x1102

#define PCI_DEVICE_ES1371         0x1371

typedef struct {
    uint16 segment;
    uint8 bus;
    uint8 device;
    uint8 function;
    uint16 vendor_id;
    uint16 device_id;
    uint8 class_code;
    uint8 subclass;
    uint8 prog_if;
    uint8 revision_id;
    uint8 header_type;
    uint32 bar[PCI_BAR_COUNT];
} pci_device_t;

typedef bool (*pci_enum_callback_t)(const pci_device_t* device, void* context);

bool pci_init(void);
uint32 pci_config_read32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);
uint16 pci_config_read16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);
uint8  pci_config_read8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);

void pci_config_write32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 value);
void pci_config_write16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint16 value);
void pci_config_write8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 value);

void pci_enumerate(pci_enum_callback_t callback, void* context);
bool pci_find_by_class(uint8 class_code, uint8 subclass, pci_device_t* out_device);
bool pci_find_by_vendor_device(uint16 vendor_id, uint16 device_id, pci_device_t* out_device);

#endif
