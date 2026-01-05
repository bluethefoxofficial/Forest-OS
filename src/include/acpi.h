#ifndef ACPI_H
#define ACPI_H

#include "types.h"
#include <stdbool.h>

#include <uacpi/uacpi.h>

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

/* MADT (Multiple APIC Description Table) structures */
typedef struct {
    acpi_sdt_header_t header;
    uint32 local_apic_addr;
    uint32 flags;
} __attribute__((packed)) acpi_madt_header_t;

typedef struct {
    uint8 type;
    uint8 length;
} __attribute__((packed)) acpi_madt_entry_header_t;

/* MADT entry type 0: Local APIC */
#define ACPI_MADT_LAPIC_FLAG_ENABLED 0x01
typedef struct {
    acpi_madt_entry_header_t header;
    uint8 acpi_processor_id;
    uint8 apic_id;
    uint32 flags;
} __attribute__((packed)) acpi_madt_lapic_t;

/* MADT entry type 9: x2APIC */
typedef struct {
    acpi_madt_entry_header_t header;
    uint16 reserved;
    uint32 x2apic_id;
    uint32 flags;
    uint32 acpi_processor_uid;
} __attribute__((packed)) acpi_madt_x2apic_t;

/* MADT table pointer */
const acpi_madt_header_t* acpi_get_madt(void);

bool acpi_init(void);
const acpi_rsdp_t* acpi_get_rsdp(void);
const acpi_rsdp_t* acpi_find_rsdp(void);
const acpi_mcfg_table_t* acpi_get_mcfg(void);

bool uacpi_init(void);
bool acpi_shutdown(void);
bool acpi_reboot(void);


#endif
