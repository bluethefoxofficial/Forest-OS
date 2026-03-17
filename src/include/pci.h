#ifndef PCI_H
#define PCI_H

#include "types.h"
#include <stdbool.h>

#define PCI_MAX_SEGMENTS      16

#define PCI_MAX_BUS        256
#define PCI_MAX_DEVICE     32
#define PCI_MAX_FUNCTION   8

#define PCI_BAR_COUNT      6

#define PCI_CONFIG_SPACE_SIZE     256
#define PCIE_EXTENDED_CONFIG_SPACE_SIZE 4096

#define PCI_CLASS_MULTIMEDIA      0x04
#define PCI_SUBCLASS_AUDIO        0x01
#define PCI_SUBCLASS_HD_AUDIO     0x03

#define PCI_VENDOR_INTEL          0x8086
#define PCI_VENDOR_ENSONIQ        0x1274
#define PCI_VENDOR_CREATIVE       0x1102

#define PCI_DEVICE_ES1371         0x1371

#define PCIE_CAPABILITY_ID        0x10
#define PCIE_CAPABILITY_OFFSET    0x00
#define PCIE_DEVICE_CAP_OFFSET    0x04
#define PCIE_DEVICE_STATUS_OFFSET 0x08
#define PCIE_LINK_CAP_OFFSET      0x0C
#define PCIE_LINK_STATUS_OFFSET   0x10

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
    
    bool is_pcie;
    uint8 pcie_cap_offset;
    uint8 pcie_device_port_type;
    uint8 pcie_link_speed;
    uint8 pcie_link_width;
} pci_device_t;

typedef bool (*pci_enum_callback_t)(const pci_device_t* device, void* context);

bool pci_init(void);
uint32 pci_config_read32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);
uint16 pci_config_read16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);
uint8  pci_config_read8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);

void pci_config_write32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 value);
void pci_config_write16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint16 value);
void pci_config_write8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 value);

uint32 pcie_config_read32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);
uint16 pcie_config_read16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);
uint8  pcie_config_read8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset);

void pcie_config_write32(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint32 value);
void pcie_config_write16(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint16 value);
void pcie_config_write8(uint16 segment, uint8 bus, uint8 device, uint8 function, uint16 offset, uint8 value);

bool pcie_is_enumerated_device_pcie(const pci_device_t* device);
uint8 pcie_find_capability_offset(uint16 segment, uint8 bus, uint8 device, uint8 function, uint8 cap_id);

#define PCIE_SPEED_GEN1  0x1
#define PCIE_SPEED_GEN2  0x2
#define PCIE_SPEED_GEN3  0x3
#define PCIE_SPEED_GEN4  0x4
#define PCIE_SPEED_GEN5  0x5

#define PCIE_PORT_TYPE_ENDPOINT               0x0
#define PCIE_PORT_TYPE_LEGACY_ENDPOINT        0x1
#define PCIE_PORT_TYPE_ROOT_PORT              0x4
#define PCIE_PORT_TYPE_UPSTREAM_PORT          0x5
#define PCIE_PORT_TYPE_DOWNSTREAM_PORT        0x6
#define PCIE_PORT_TYPE_PCI_TO_PCIE_BRIDGE     0x7
#define PCIE_PORT_TYPE_PCIE_TO_PCI_BRIDGE     0x8

#define PCIE_CAP_ID_MSI       0x05
#define PCIE_CAP_ID_MSIX      0x11
#define PCIE_CAP_ID_VENDOR    0x09
#define PCIE_CAP_ID_POWER     0x10
#define PCIE_CAP_ID_AER       0x15

void pci_enumerate(pci_enum_callback_t callback, void* context);
bool pci_find_by_class(uint8 class_code, uint8 subclass, pci_device_t* out_device);
bool pci_find_by_vendor_device(uint16 vendor_id, uint16 device_id, pci_device_t* out_device);

#endif
