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

/* Generic Address Structure */
typedef struct {
    uint8 address_space;
    uint8 bit_width;
    uint8 bit_offset;
    uint8 access_size;
    uint64 address;
} __attribute__((packed)) acpi_gas_t;

/* FADT (Fixed ACPI Description Table) */
typedef struct {
    acpi_sdt_header_t header;
    uint32 firmware_ctrl;
    uint32 dsdt;
    uint8 reserved;
    uint8 preferred_pm_profile;
    uint16 sci_interrupt;
    uint32 smi_command;
    uint8 acpi_enable;
    uint8 acpi_disable;
    uint8 s4bios_req;
    uint8 pstate_control;
    uint32 pm1a_event_block;
    uint32 pm1b_event_block;
    uint32 pm1a_control_block;
    uint32 pm1b_control_block;
    uint32 pm2_control_block;
    uint32 pm_timer_block;
    uint32 gpe0_block;
    uint32 gpe1_block;
    uint8 pm1_event_length;
    uint8 pm1_control_length;
    uint8 pm2_control_length;
    uint8 pm_timer_length;
    uint8 gpe0_length;
    uint8 gpe1_length;
    uint8 gpe1_base;
    uint8 c_state_control;
    uint16 worst_c2_latency;
    uint16 worst_c3_latency;
    uint16 flush_size;
    uint16 flush_stride;
    uint8 duty_offset;
    uint8 duty_width;
    uint8 day_alarm;
    uint8 month_alarm;
    uint8 century;
    uint16 boot_arch_flags;
    uint8 reserved2;
    uint32 flags;
    acpi_gas_t reset_reg;
    uint8 reset_value;
    uint8 reserved3[3];
    uint64 x_firmware_ctrl;
    uint64 x_dsdt;
    acpi_gas_t x_pm1a_event_block;
    acpi_gas_t x_pm1b_event_block;
    acpi_gas_t x_pm1a_control_block;
    acpi_gas_t x_pm1b_control_block;
    acpi_gas_t x_pm2_control_block;
    acpi_gas_t x_pm_timer_block;
    acpi_gas_t x_gpe0_block;
    acpi_gas_t x_gpe1_block;
} __attribute__((packed)) acpi_fadt_t;

/* MADT table pointer */
const acpi_madt_header_t* acpi_get_madt(void);

bool acpi_init(void);
const acpi_rsdp_t* acpi_get_rsdp(void);
const acpi_rsdp_t* acpi_find_rsdp(void);
const acpi_mcfg_table_t* acpi_get_mcfg(void);
const acpi_fadt_t* acpi_get_fadt(void);
const acpi_sdt_header_t* acpi_map_table(uint64 phys_addr);
const char* acpi_get_last_error(void);

bool uacpi_init(void);
bool acpi_shutdown(void);
bool acpi_reboot(void);


#endif
