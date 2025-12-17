#ifndef ACPI_H
#define ACPI_H

#include "types.h"
#include <stdbool.h>

#define ACPI_RSDP_SIGNATURE "RSD PTR "
#define ACPI_MCFG_SIGNATURE "MCFG"

typedef struct {
    char  signature[8];
    uint8 checksum;
    char  oem_id[6];
    uint8 revision;
    uint32 rsdt_address;
} __attribute__((packed)) acpi_rsdp_v1_t;

typedef struct {
    acpi_rsdp_v1_t v1;
    uint32 length;
    uint64 xsdt_address;
    uint8 extended_checksum;
    uint8 reserved[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char signature[4];
    uint32 length;
    uint8 revision;
    uint8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32 oem_revision;
    uint32 creator_id;
    uint32 creator_revision;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t header;
    uint64 reserved;
} __attribute__((packed)) acpi_mcfg_table_t;

typedef struct {
    uint64 base_address;
    uint16 segment_group;
    uint8 start_bus;
    uint8 end_bus;
    uint32 reserved;
} __attribute__((packed)) acpi_mcfg_entry_t;

bool acpi_init(void);
const acpi_rsdp_t* acpi_get_rsdp(void);
const acpi_mcfg_table_t* acpi_get_mcfg(void);

#endif
