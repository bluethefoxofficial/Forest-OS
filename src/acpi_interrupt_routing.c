#include "acpi_interrupt_routing.h"
#include "interrupt_management.h"
#include "io_apic.h"
#include "local_apic.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MAX_ACPI_TABLES 64
#define MAX_IRQ_OVERRIDES 32
#define MAX_NMI_SOURCES 16
#define MAX_LOCAL_APIC_ENTRIES 256
#define MAX_IO_APIC_ENTRIES 64

typedef struct {
    uint8_t signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    uint8_t oem_id[6];
    uint8_t oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed)) acpi_table_header_t;

typedef struct {
    acpi_table_header_t header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_header_t;

typedef struct {
    uint8_t type;
    uint8_t length;
} __attribute__((packed)) acpi_madt_entry_header_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t processor_id;
    uint8_t local_apic_id;
    uint32_t flags;
} __attribute__((packed)) acpi_local_apic_entry_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t io_apic_id;
    uint8_t reserved;
    uint32_t io_apic_address;
    uint32_t global_system_interrupt_base;
} __attribute__((packed)) acpi_io_apic_entry_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t bus;
    uint8_t source;
    uint32_t global_system_interrupt;
    uint16_t flags;
} __attribute__((packed)) acpi_interrupt_override_entry_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint16_t flags;
    uint32_t global_system_interrupt;
} __attribute__((packed)) acpi_nmi_source_entry_t;

typedef struct {
    acpi_madt_entry_header_t header;
    uint8_t processor_id;
    uint16_t flags;
    uint8_t local_apic_nmi_pin;
} __attribute__((packed)) acpi_local_apic_nmi_entry_t;

typedef struct {
    uint8_t bus;
    uint8_t source_irq;
    uint32_t global_irq;
    uint16_t flags;
    bool active_low;
    bool level_triggered;
} interrupt_override_t;

typedef struct {
    uint32_t global_irq;
    uint16_t flags;
    bool active_low;
    bool level_triggered;
} nmi_source_t;

typedef struct {
    uint8_t processor_id;
    uint8_t local_apic_id;
    bool enabled;
} local_apic_info_t;

typedef struct {
    uint8_t io_apic_id;
    uint32_t address;
    uint32_t gsi_base;
    uint32_t gsi_count;
} io_apic_info_t;

typedef struct {
    acpi_madt_header_t *madt;
    
    local_apic_info_t local_apics[MAX_LOCAL_APIC_ENTRIES];
    size_t local_apic_count;
    
    io_apic_info_t io_apics[MAX_IO_APIC_ENTRIES];
    size_t io_apic_count;
    
    interrupt_override_t overrides[MAX_IRQ_OVERRIDES];
    size_t override_count;
    
    nmi_source_t nmi_sources[MAX_NMI_SOURCES];
    size_t nmi_source_count;
    
    uint32_t local_apic_base_address;
    bool legacy_pic_present;
    bool initialized;
} acpi_interrupt_context_t;

static acpi_interrupt_context_t acpi_ctx = {0};

static acpi_table_header_t* find_acpi_table(const char* signature) {
    extern uint64_t *acpi_root_pointer;
    
    if (!acpi_root_pointer) {
        return NULL;
    }
    
    acpi_table_header_t *rsdt = (acpi_table_header_t*)acpi_root_pointer;
    uint32_t *table_pointers = (uint32_t*)(rsdt + 1);
    uint32_t table_count = (rsdt->length - sizeof(acpi_table_header_t)) / sizeof(uint32_t);
    
    for (uint32_t i = 0; i < table_count; i++) {
        acpi_table_header_t *table = (acpi_table_header_t*)(uintptr_t)table_pointers[i];
        if (memcmp(table->signature, signature, 4) == 0) {
            return table;
        }
    }
    
    return NULL;
}

static bool validate_acpi_checksum(acpi_table_header_t *table) {
    uint8_t *data = (uint8_t*)table;
    uint8_t checksum = 0;
    
    for (uint32_t i = 0; i < table->length; i++) {
        checksum += data[i];
    }
    
    return checksum == 0;
}

static void parse_madt_entries(acpi_madt_header_t *madt) {
    uint8_t *entry_ptr = (uint8_t*)(madt + 1);
    uint8_t *table_end = (uint8_t*)madt + madt->header.length;
    
    acpi_ctx.local_apic_count = 0;
    acpi_ctx.io_apic_count = 0;
    acpi_ctx.override_count = 0;
    acpi_ctx.nmi_source_count = 0;
    
    while (entry_ptr < table_end) {
        acpi_madt_entry_header_t *entry_header = (acpi_madt_entry_header_t*)entry_ptr;
        
        if (entry_header->length == 0) {
            break;
        }
        
        switch (entry_header->type) {
            case 0: { // Local APIC
                acpi_local_apic_entry_t *local_apic = (acpi_local_apic_entry_t*)entry_ptr;
                if (acpi_ctx.local_apic_count < MAX_LOCAL_APIC_ENTRIES) {
                    acpi_ctx.local_apics[acpi_ctx.local_apic_count] = (local_apic_info_t){
                        .processor_id = local_apic->processor_id,
                        .local_apic_id = local_apic->local_apic_id,
                        .enabled = (local_apic->flags & 1) != 0
                    };
                    acpi_ctx.local_apic_count++;
                }
                break;
            }
            
            case 1: { // I/O APIC
                acpi_io_apic_entry_t *io_apic = (acpi_io_apic_entry_t*)entry_ptr;
                if (acpi_ctx.io_apic_count < MAX_IO_APIC_ENTRIES) {
                    uint32_t gsi_count = io_apic_get_max_redirect_entry(io_apic->io_apic_address) + 1;
                    acpi_ctx.io_apics[acpi_ctx.io_apic_count] = (io_apic_info_t){
                        .io_apic_id = io_apic->io_apic_id,
                        .address = io_apic->io_apic_address,
                        .gsi_base = io_apic->global_system_interrupt_base,
                        .gsi_count = gsi_count
                    };
                    acpi_ctx.io_apic_count++;
                }
                break;
            }
            
            case 2: { // Interrupt Source Override
                acpi_interrupt_override_entry_t *override = (acpi_interrupt_override_entry_t*)entry_ptr;
                if (acpi_ctx.override_count < MAX_IRQ_OVERRIDES) {
                    acpi_ctx.overrides[acpi_ctx.override_count] = (interrupt_override_t){
                        .bus = override->bus,
                        .source_irq = override->source,
                        .global_irq = override->global_system_interrupt,
                        .flags = override->flags,
                        .active_low = (override->flags & 2) != 0,
                        .level_triggered = (override->flags & 8) != 0
                    };
                    acpi_ctx.override_count++;
                }
                break;
            }
            
            case 3: { // NMI Source
                acpi_nmi_source_entry_t *nmi = (acpi_nmi_source_entry_t*)entry_ptr;
                if (acpi_ctx.nmi_source_count < MAX_NMI_SOURCES) {
                    acpi_ctx.nmi_sources[acpi_ctx.nmi_source_count] = (nmi_source_t){
                        .global_irq = nmi->global_system_interrupt,
                        .flags = nmi->flags,
                        .active_low = (nmi->flags & 2) != 0,
                        .level_triggered = (nmi->flags & 8) != 0
                    };
                    acpi_ctx.nmi_source_count++;
                }
                break;
            }
            
            case 4: { // Local APIC NMI
                acpi_local_apic_nmi_entry_t *local_nmi = (acpi_local_apic_nmi_entry_t*)entry_ptr;
                local_apic_configure_nmi(local_nmi->processor_id, 
                                       local_nmi->local_apic_nmi_pin,
                                       (local_nmi->flags & 2) != 0,
                                       (local_nmi->flags & 8) != 0);
                break;
            }
        }
        
        entry_ptr += entry_header->length;
    }
}

acpi_interrupt_error_t acpi_interrupt_init(void) {
    memset(&acpi_ctx, 0, sizeof(acpi_ctx));
    
    acpi_table_header_t *madt_header = find_acpi_table("APIC");
    if (!madt_header) {
        return ACPI_INT_ERROR_NO_MADT;
    }
    
    if (!validate_acpi_checksum(madt_header)) {
        return ACPI_INT_ERROR_INVALID_CHECKSUM;
    }
    
    acpi_ctx.madt = (acpi_madt_header_t*)madt_header;
    acpi_ctx.local_apic_base_address = acpi_ctx.madt->local_apic_address;
    acpi_ctx.legacy_pic_present = (acpi_ctx.madt->flags & 1) != 0;
    
    parse_madt_entries(acpi_ctx.madt);
    
    if (acpi_ctx.local_apic_count == 0) {
        return ACPI_INT_ERROR_NO_LOCAL_APIC;
    }
    
    if (acpi_ctx.io_apic_count == 0) {
        return ACPI_INT_ERROR_NO_IO_APIC;
    }
    
    acpi_ctx.initialized = true;
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_get_irq_routing(
    uint8_t bus, uint8_t device, uint8_t pin,
    acpi_irq_routing_info_t *routing_info) {
    
    if (!acpi_ctx.initialized || !routing_info) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    uint8_t legacy_irq = (device * 4 + pin - 1) % 8;
    if (legacy_irq >= 8) legacy_irq = legacy_irq % 8 + 8;
    
    routing_info->legacy_irq = legacy_irq;
    routing_info->global_irq = legacy_irq;
    routing_info->io_apic_id = 0;
    routing_info->io_apic_pin = legacy_irq;
    routing_info->active_low = false;
    routing_info->level_triggered = false;
    routing_info->has_override = false;
    
    for (size_t i = 0; i < acpi_ctx.override_count; i++) {
        interrupt_override_t *override = &acpi_ctx.overrides[i];
        if (override->bus == bus && override->source_irq == legacy_irq) {
            routing_info->global_irq = override->global_irq;
            routing_info->active_low = override->active_low;
            routing_info->level_triggered = override->level_triggered;
            routing_info->has_override = true;
            break;
        }
    }
    
    for (size_t i = 0; i < acpi_ctx.io_apic_count; i++) {
        io_apic_info_t *io_apic = &acpi_ctx.io_apics[i];
        if (routing_info->global_irq >= io_apic->gsi_base && 
            routing_info->global_irq < io_apic->gsi_base + io_apic->gsi_count) {
            routing_info->io_apic_id = io_apic->io_apic_id;
            routing_info->io_apic_pin = routing_info->global_irq - io_apic->gsi_base;
            break;
        }
    }
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_configure_device(
    uint8_t bus, uint8_t device, uint8_t pin,
    acpi_interrupt_handler_t handler, void *context) {
    
    if (!handler) {
        return ACPI_INT_ERROR_INVALID_PARAMS;
    }
    
    acpi_irq_routing_info_t routing;
    acpi_interrupt_error_t result = acpi_interrupt_get_irq_routing(bus, device, pin, &routing);
    if (result != ACPI_INT_SUCCESS) {
        return result;
    }
    
    io_apic_configure_entry(routing.io_apic_id, routing.io_apic_pin,
                          routing.global_irq + 32, // Vector offset
                          routing.active_low, routing.level_triggered);
    
    interrupt_register_handler(routing.global_irq + 32, handler, context);
    
    io_apic_enable_entry(routing.io_apic_id, routing.io_apic_pin);
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_get_local_apic_info(
    acpi_local_apic_list_t *apic_list) {
    
    if (!acpi_ctx.initialized || !apic_list) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    apic_list->count = acpi_ctx.local_apic_count;
    apic_list->base_address = acpi_ctx.local_apic_base_address;
    
    for (size_t i = 0; i < acpi_ctx.local_apic_count && i < MAX_LOCAL_APIC_ENTRIES; i++) {
        apic_list->apics[i].processor_id = acpi_ctx.local_apics[i].processor_id;
        apic_list->apics[i].local_apic_id = acpi_ctx.local_apics[i].local_apic_id;
        apic_list->apics[i].enabled = acpi_ctx.local_apics[i].enabled;
    }
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_get_io_apic_info(
    acpi_io_apic_list_t *apic_list) {
    
    if (!acpi_ctx.initialized || !apic_list) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    apic_list->count = acpi_ctx.io_apic_count;
    
    for (size_t i = 0; i < acpi_ctx.io_apic_count && i < MAX_IO_APIC_ENTRIES; i++) {
        apic_list->apics[i].io_apic_id = acpi_ctx.io_apics[i].io_apic_id;
        apic_list->apics[i].address = acpi_ctx.io_apics[i].address;
        apic_list->apics[i].gsi_base = acpi_ctx.io_apics[i].gsi_base;
        apic_list->apics[i].gsi_count = acpi_ctx.io_apics[i].gsi_count;
    }
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_find_gsi_for_irq(uint8_t legacy_irq, uint32_t *gsi) {
    if (!acpi_ctx.initialized || !gsi) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    *gsi = legacy_irq;
    
    for (size_t i = 0; i < acpi_ctx.override_count; i++) {
        if (acpi_ctx.overrides[i].source_irq == legacy_irq) {
            *gsi = acpi_ctx.overrides[i].global_irq;
            return ACPI_INT_SUCCESS;
        }
    }
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_configure_nmi_sources(void) {
    if (!acpi_ctx.initialized) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    for (size_t i = 0; i < acpi_ctx.nmi_source_count; i++) {
        nmi_source_t *nmi = &acpi_ctx.nmi_sources[i];
        
        for (size_t j = 0; j < acpi_ctx.io_apic_count; j++) {
            io_apic_info_t *io_apic = &acpi_ctx.io_apics[j];
            if (nmi->global_irq >= io_apic->gsi_base && 
                nmi->global_irq < io_apic->gsi_base + io_apic->gsi_count) {
                
                uint8_t pin = nmi->global_irq - io_apic->gsi_base;
                io_apic_configure_nmi(io_apic->io_apic_id, pin, 
                                    nmi->active_low, nmi->level_triggered);
                break;
            }
        }
    }
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_disable_legacy_pic(void) {
    if (!acpi_ctx.initialized) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    if (!acpi_ctx.legacy_pic_present) {
        return ACPI_INT_SUCCESS;
    }
    
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    
    outb(0x21, 0xF0);
    outb(0xA1, 0xF8);
    
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    
    return ACPI_INT_SUCCESS;
}

acpi_interrupt_error_t acpi_interrupt_validate_configuration(
    acpi_validation_result_t *validation) {
    
    if (!acpi_ctx.initialized || !validation) {
        return ACPI_INT_ERROR_NOT_INITIALIZED;
    }
    
    memset(validation, 0, sizeof(acpi_validation_result_t));
    validation->is_valid = true;
    
    if (acpi_ctx.local_apic_count == 0) {
        validation->is_valid = false;
        strcat(validation->error_message, "No Local APIC entries found. ");
    }
    
    if (acpi_ctx.io_apic_count == 0) {
        validation->is_valid = false;
        strcat(validation->error_message, "No I/O APIC entries found. ");
    }
    
    if (acpi_ctx.local_apic_base_address == 0) {
        validation->is_valid = false;
        strcat(validation->error_message, "Invalid Local APIC base address. ");
    }
    
    uint32_t total_gsi_coverage = 0;
    for (size_t i = 0; i < acpi_ctx.io_apic_count; i++) {
        total_gsi_coverage += acpi_ctx.io_apics[i].gsi_count;
    }
    
    if (total_gsi_coverage < 24) {
        validation->is_valid = false;
        strcat(validation->error_message, "Insufficient GSI coverage. ");
    }
    
    for (size_t i = 0; i < acpi_ctx.override_count; i++) {
        bool gsi_valid = false;
        for (size_t j = 0; j < acpi_ctx.io_apic_count; j++) {
            io_apic_info_t *io_apic = &acpi_ctx.io_apics[j];
            if (acpi_ctx.overrides[i].global_irq >= io_apic->gsi_base &&
                acpi_ctx.overrides[i].global_irq < io_apic->gsi_base + io_apic->gsi_count) {
                gsi_valid = true;
                break;
            }
        }
        if (!gsi_valid) {
            validation->is_valid = false;
            strcat(validation->error_message, "Invalid GSI in interrupt override. ");
            break;
        }
    }
    
    validation->local_apic_count = acpi_ctx.local_apic_count;
    validation->io_apic_count = acpi_ctx.io_apic_count;
    validation->override_count = acpi_ctx.override_count;
    validation->nmi_source_count = acpi_ctx.nmi_source_count;
    
    return ACPI_INT_SUCCESS;
}

bool acpi_interrupt_is_initialized(void) {
    return acpi_ctx.initialized;
}

bool acpi_interrupt_has_legacy_pic(void) {
    return acpi_ctx.legacy_pic_present;
}

uint32_t acpi_interrupt_get_local_apic_base(void) {
    return acpi_ctx.local_apic_base_address;
}