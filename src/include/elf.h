#ifndef ELF_H
#define ELF_H

#include <stddef.h>
#include "types.h"

// ELF magic number
#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

// ELF file class
#define ELF_CLASS_NONE 0
#define ELF_CLASS_32   1
#define ELF_CLASS_64   2

// ELF data encoding
#define ELF_DATA_NONE 0
#define ELF_DATA_2LSB 1  // Little endian
#define ELF_DATA_2MSB 2  // Big endian

// ELF file version
#define ELF_VERSION_NONE    0
#define ELF_VERSION_CURRENT 1

// ELF file types
#define ELF_TYPE_NONE   0  // No file type
#define ELF_TYPE_REL    1  // Relocatable file
#define ELF_TYPE_EXEC   2  // Executable file
#define ELF_TYPE_DYN    3  // Shared object file
#define ELF_TYPE_CORE   4  // Core file

// ELF machine types
#define ELF_MACHINE_NONE  0
#define ELF_MACHINE_M32   1
#define ELF_MACHINE_SPARC 2
#define ELF_MACHINE_386   3
#define ELF_MACHINE_68K   4
#define ELF_MACHINE_88K   5
#define ELF_MACHINE_860   7
#define ELF_MACHINE_MIPS  8

// Program header types
#define PT_NULL    0  // Unused entry
#define PT_LOAD    1  // Loadable segment
#define PT_DYNAMIC 2  // Dynamic linking information
#define PT_INTERP  3  // Interpreter information
#define PT_NOTE    4  // Auxiliary information
#define PT_SHLIB   5  // Reserved
#define PT_PHDR    6  // Program header table

// Program header flags
#define PF_X 1  // Executable
#define PF_W 2  // Writable
#define PF_R 4  // Readable

// Section header types
#define SHT_NULL     0   // Inactive section
#define SHT_PROGBITS 1   // Program data
#define SHT_SYMTAB   2   // Symbol table
#define SHT_STRTAB   3   // String table
#define SHT_RELA     4   // Relocation entries with addends
#define SHT_HASH     5   // Symbol hash table
#define SHT_DYNAMIC  6   // Dynamic linking information
#define SHT_NOTE     7   // Notes
#define SHT_NOBITS   8   // Program space with no data (bss)
#define SHT_REL      9   // Relocation entries, no addends
#define SHT_SHLIB    10  // Reserved
#define SHT_DYNSYM   11  // Dynamic linker symbol table

// Section header flags
#define SHF_WRITE     1  // Writable
#define SHF_ALLOC     2  // Occupies memory during execution
#define SHF_EXECINSTR 4  // Executable

// ELF identification array indices
#define EI_MAG0    0  // Magic number byte 0
#define EI_MAG1    1  // Magic number byte 1
#define EI_MAG2    2  // Magic number byte 2
#define EI_MAG3    3  // Magic number byte 3
#define EI_CLASS   4  // File class
#define EI_DATA    5  // Data encoding
#define EI_VERSION 6  // File version
#define EI_PAD     7  // Start of padding bytes
#define EI_NIDENT  16 // Size of e_ident[]

// 32-bit ELF header
typedef struct {
    uint8  e_ident[EI_NIDENT];  // ELF identification
    uint16 e_type;              // File type
    uint16 e_machine;           // Machine type
    uint32 e_version;           // File version
    uint32 e_entry;             // Entry point virtual address
    uint32 e_phoff;             // Program header table file offset
    uint32 e_shoff;             // Section header table file offset
    uint32 e_flags;             // Processor-specific flags
    uint16 e_ehsize;            // ELF header size in bytes
    uint16 e_phentsize;         // Program header table entry size
    uint16 e_phnum;             // Program header table entry count
    uint16 e_shentsize;         // Section header table entry size
    uint16 e_shnum;             // Section header table entry count
    uint16 e_shstrndx;          // Section header string table index
} __attribute__((packed)) elf32_ehdr_t;

// 32-bit Program header
typedef struct {
    uint32 p_type;    // Segment type
    uint32 p_offset;  // Segment file offset
    uint32 p_vaddr;   // Segment virtual address
    uint32 p_paddr;   // Segment physical address
    uint32 p_filesz;  // Segment size in file
    uint32 p_memsz;   // Segment size in memory
    uint32 p_flags;   // Segment flags
    uint32 p_align;   // Segment alignment
} __attribute__((packed)) elf32_phdr_t;

// 32-bit Section header
typedef struct {
    uint32 sh_name;      // Section name (string table index)
    uint32 sh_type;      // Section type
    uint32 sh_flags;     // Section flags
    uint32 sh_addr;      // Section virtual addr at execution
    uint32 sh_offset;    // Section file offset
    uint32 sh_size;      // Section size in bytes
    uint32 sh_link;      // Link to another section
    uint32 sh_info;      // Additional section information
    uint32 sh_addralign; // Section alignment
    uint32 sh_entsize;   // Entry size if section holds table
} __attribute__((packed)) elf32_shdr_t;

// ELF symbol table entry (32-bit)
typedef struct {
    uint32 st_name;  // Symbol name (string table index)
    uint32 st_value; // Symbol value
    uint32 st_size;  // Symbol size
    uint8  st_info;  // Symbol type and binding
    uint8  st_other; // Symbol visibility
    uint16 st_shndx; // Section index
} __attribute__((packed)) elf32_sym_t;

// Relocation entries
typedef struct {
    uint32 r_offset; // Address
    uint32 r_info;   // Relocation type and symbol index
} __attribute__((packed)) elf32_rel_t;

typedef struct {
    uint32 r_offset; // Address
    uint32 r_info;   // Relocation type and symbol index
    int32  r_addend; // Addend
} __attribute__((packed)) elf32_rela_t;

// Dynamic section entry
typedef struct {
    int32  d_tag;    // Dynamic entry type
    uint32 d_val;    // Integer value
} __attribute__((packed)) elf32_dyn_t;

// ELF loading result
typedef struct {
    uint32 entry_point;     // Entry point virtual address
    uint32 base_address;    // Base address where ELF was loaded
    uint32 total_size;      // Total memory size used
    uint32 text_start;      // Text segment start
    uint32 text_size;       // Text segment size
    uint32 data_start;      // Data segment start
    uint32 data_size;       // Data segment size
    uint32 bss_start;       // BSS segment start
    uint32 bss_size;        // BSS segment size
    uint32 page_directory;  // Physical address of the page directory
    bool   valid;           // Whether the ELF was loaded successfully
} elf_load_info_t;

// Function prototypes
int elf_validate_header(const elf32_ehdr_t* header);
int elf_load_executable(const uint8* elf_data, size_t elf_size, elf_load_info_t* load_info);
int elf_load_from_file(const char* filename, elf_load_info_t* load_info);
uint32 elf_get_entry_point(const uint8* elf_data);
bool elf_is_valid(const uint8* elf_data, size_t size);

// ELF utility macros
#define ELF32_R_SYM(info)  ((info) >> 8)
#define ELF32_R_TYPE(info) ((unsigned char)(info))
#define ELF32_R_INFO(sym, type) (((sym) << 8) + (unsigned char)(type))

#define ELF32_ST_BIND(info)   ((info) >> 4)
#define ELF32_ST_TYPE(info)   ((info) & 0xf)
#define ELF32_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))

#endif
