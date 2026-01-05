#!/bin/bash
# =============================================================================
# FOREST OS CONFIGURATION TUI (conf.sh)
# =============================================================================
# Interactive Terminal User Interface for Forest OS build configuration
# Using dialog command for Linux kernel menuconfig-style interface
# =============================================================================

set -euo pipefail

# =============================================================================
# SCRIPT CONFIGURATION
# =============================================================================

SCRIPT_VERSION="2.0.0"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="$SCRIPT_DIR/.forestos_config"
BUILD_CONFIG_FILE="$SCRIPT_DIR/build-config.mk"
DEFAULT_CONFIG_FILE="$SCRIPT_DIR/forestos_defconfig"

# Temporary files for dialog
TEMPFILE=$(mktemp)
MENUFILE=$(mktemp)
trap 'rm -f "$TEMPFILE" "$MENUFILE"' EXIT

# =============================================================================
# CONFIGURATION DATABASE
# =============================================================================

# Configuration options format: type:default:dependencies:description:help
declare -A CONFIG_DB
declare -A CONFIG_VALUES

# General Setup Options
CONFIG_DB[BUILD_ARCH]="choice:32:none:Target Architecture:Choose the target architecture for Forest OS"
CONFIG_DB[BUILD_BOOT_MODE]="choice:bios:none:Boot Mode:Select the boot firmware interface"
CONFIG_DB[BUILD_TYPE]="choice:debug:none:Build Type:Select compilation optimization and debug level"
CONFIG_DB[BUILD_PARALLEL_JOBS]="int:0:none:Parallel Build Jobs:Number of parallel make jobs (0=auto)"

# Processor Options
CONFIG_DB[ENABLE_SMP]="bool:y:none:SMP Support:Enable support for multiple CPU cores"
CONFIG_DB[ENABLE_ACPI]="bool:y:none:ACPI Support:Enable ACPI power management and device enumeration"
CONFIG_DB[ENABLE_APIC]="bool:y:ENABLE_SMP:APIC Support:Enable Advanced Programmable Interrupt Controller"
CONFIG_DB[ENABLE_IOAPIC]="bool:y:ENABLE_APIC:I/O APIC Support:Enable I/O Advanced Programmable Interrupt Controller"
CONFIG_DB[ENABLE_HPET]="bool:n:none:HPET Timer:Enable High Precision Event Timer"
CONFIG_DB[ENABLE_MSI]="bool:n:ENABLE_APIC:MSI Interrupts:Enable Message Signaled Interrupts"

# Memory Management Options
CONFIG_DB[ENABLE_PAGING]="bool:y:none:Virtual Memory Paging:Enable virtual memory management"
CONFIG_DB[ENABLE_PAE]="bool:n:BUILD_ARCH=32:PAE (32-bit only):Enable Physical Address Extension for 32-bit builds"
CONFIG_DB[ENABLE_SWAP]="bool:n:ENABLE_PAGING:Swap Support:Enable virtual memory swapping to disk"
CONFIG_DB[ENABLE_MEMORY_PROTECTION]="bool:y:ENABLE_PAGING:Memory Protection:Enable memory access protection mechanisms"
CONFIG_DB[ENABLE_SLAB]="bool:y:none:SLAB Allocator:Enable efficient kernel object allocation"
CONFIG_DB[ENABLE_MEMORY_DEBUG]="bool:n:none:Memory Debugging:Enable memory leak and corruption detection"

# File System Options
CONFIG_DB[ENABLE_VFS]="bool:y:none:Virtual File System:Enable the Virtual File System layer"
CONFIG_DB[ENABLE_EXT2]="bool:y:ENABLE_VFS:EXT2 File System:Enable EXT2 file system support"
CONFIG_DB[ENABLE_FAT32]="bool:y:ENABLE_VFS:FAT32 File System:Enable FAT32 file system support"
CONFIG_DB[ENABLE_ISO9660]="bool:y:ENABLE_VFS:ISO9660 File System:Enable ISO9660 (CD-ROM) file system"
CONFIG_DB[ENABLE_PROCFS]="bool:y:ENABLE_VFS:ProcFS:Enable /proc file system for kernel information"
CONFIG_DB[ENABLE_TMPFS]="bool:y:ENABLE_VFS:TmpFS:Enable temporary file system in memory"

# Graphics Options
CONFIG_DB[ENABLE_GRAPHICS]="bool:y:none:Graphics Support:Enable graphics and display subsystem"
CONFIG_DB[ENABLE_VESA]="bool:y:ENABLE_GRAPHICS:VESA Display:Enable VESA graphics mode support"
CONFIG_DB[ENABLE_FRAMEBUFFER]="bool:y:ENABLE_GRAPHICS:Framebuffer:Enable framebuffer graphics interface"
CONFIG_DB[ENABLE_VGA_TEXT]="bool:y:none:VGA Text Mode:Enable VGA text mode support"
CONFIG_DB[ENABLE_CONSOLE]="bool:y:none:Console Support:Enable kernel console output"

# Networking Options
CONFIG_DB[ENABLE_NETWORKING]="bool:n:none:Networking Stack:Enable TCP/IP networking stack"
CONFIG_DB[ENABLE_ETHERNET]="bool:n:ENABLE_NETWORKING:Ethernet Support:Enable Ethernet network interface support"
CONFIG_DB[ENABLE_TCP]="bool:n:ENABLE_NETWORKING:TCP Protocol:Enable TCP transport protocol"
CONFIG_DB[ENABLE_UDP]="bool:n:ENABLE_NETWORKING:UDP Protocol:Enable UDP transport protocol"
CONFIG_DB[ENABLE_DHCP]="bool:n:ENABLE_NETWORKING:DHCP Client:Enable DHCP network configuration"

# Security Options
CONFIG_DB[ENABLE_SMEP_SMAP]="bool:y:none:SMEP/SMAP:Enable Supervisor Mode Execution/Access Prevention"
CONFIG_DB[ENABLE_NX_BIT]="bool:y:none:NX Bit:Enable No-Execute bit support"
CONFIG_DB[ENABLE_ASLR]="bool:n:none:ASLR:Enable Address Space Layout Randomization"
CONFIG_DB[ENABLE_STACK_PROTECTION]="bool:y:none:Stack Protection:Enable stack canaries and protection"
CONFIG_DB[ENABLE_GUARD_PAGES]="bool:y:none:Kernel Guard Pages:Enable guard pages for kernel stacks"
CONFIG_DB[ENABLE_ROOT_AUTOLOGIN]="bool:n:none:Root Autologin:Automatically log in as root at boot"

# Debug Options
CONFIG_DB[ENABLE_DEBUG_SYMBOLS]="bool:y:none:Debug Symbols:Include debug information in kernel"
CONFIG_DB[ENABLE_KERNEL_DEBUG]="bool:y:none:Kernel Debugging:Enable kernel debugging facilities"
CONFIG_DB[ENABLE_SERIAL_DEBUG]="bool:y:none:Serial Debug:Enable debug output to serial port"
CONFIG_DB[ENABLE_PANIC_BACKTRACES]="bool:y:none:Panic Backtraces:Enable stack traces on kernel panic"
CONFIG_DB[ENABLE_ASSERTIONS]="bool:y:none:Kernel Assertions:Enable runtime assertion checking"

# Hardware Support Options
CONFIG_DB[ENABLE_USB]="bool:n:none:USB Support:Enable Universal Serial Bus support"
CONFIG_DB[ENABLE_PS2]="bool:y:none:PS/2 Support:Enable PS/2 keyboard and mouse support"
CONFIG_DB[ENABLE_SERIAL]="bool:y:none:Serial Port:Enable serial port support"
CONFIG_DB[ENABLE_PARALLEL]="bool:n:none:Parallel Port:Enable parallel port support"
CONFIG_DB[ENABLE_PCI]="bool:y:none:PCI Bus:Enable PCI bus support"

# Audio Options
CONFIG_DB[ENABLE_AUDIO]="bool:n:none:Audio Support:Enable audio subsystem"
CONFIG_DB[ENABLE_AC97]="bool:n:ENABLE_AUDIO:AC97 Audio:Enable AC97 audio codec support"
CONFIG_DB[ENABLE_HDA]="bool:n:ENABLE_AUDIO:Intel HDA:Enable Intel High Definition Audio support"

# =============================================================================
# CHOICE OPTIONS
# =============================================================================

declare -A CHOICE_OPTIONS

CHOICE_OPTIONS[BUILD_ARCH]="32 \"32-bit (i686)\" 64 \"64-bit (x86_64)\""
CHOICE_OPTIONS[BUILD_BOOT_MODE]="bios \"BIOS/Legacy Boot\" uefi \"UEFI Boot\""
CHOICE_OPTIONS[BUILD_TYPE]="debug \"Debug Build\" release \"Release Build\" optimize \"Optimized Build\""

# =============================================================================
# UTILITY FUNCTIONS
# =============================================================================

# Show error dialog and exit
show_error() {
    local message="$1"
    dialog --title "Error" --msgbox "$message" 10 50
    exit 1
}

# Show info dialog
show_info() {
    local title="$1"
    local message="$2"
    dialog --title "$title" --msgbox "$message" 15 70
}

# Check dialog availability
check_dialog() {
    if ! command -v dialog >/dev/null 2>&1; then
        echo "Error: dialog command not found. Please install dialog package."
        echo "Ubuntu/Debian: sudo apt-get install dialog"
        echo "Fedora/RHEL: sudo dnf install dialog"
        echo "Arch: sudo pacman -S dialog"
        exit 1
    fi
}

# =============================================================================
# CONFIGURATION MANAGEMENT
# =============================================================================

# Load default configuration
load_defaults() {
    for config_name in "${!CONFIG_DB[@]}"; do
        local config_info="${CONFIG_DB[$config_name]}"
        local default_value=$(echo "$config_info" | cut -d: -f2)
        CONFIG_VALUES[$config_name]="$default_value"
    done
}

# Load configuration from file
load_config() {
    local config_file="${1:-$CONFIG_FILE}"
    
    if [[ ! -f "$config_file" ]]; then
        return 1
    fi
    
    while IFS='=' read -r key value; do
        if [[ $key =~ ^CONFIG_ ]]; then
            local config_name="${key#CONFIG_}"
            if [[ -n "${CONFIG_DB[$config_name]:-}" ]]; then
                CONFIG_VALUES[$config_name]="$value"
            fi
        fi
    done < "$config_file"
    
    return 0
}

# Save configuration to file
save_config() {
    local config_file="${1:-$CONFIG_FILE}"
    
    {
        echo "#"
        echo "# Forest OS Configuration"
        echo "# Generated by conf.sh on $(date)"
        echo "#"
        echo
        
        for config_name in $(printf '%s\n' "${!CONFIG_VALUES[@]}" | sort); do
            local config_info="${CONFIG_DB[$config_name]}"
            local description=$(echo "$config_info" | cut -d: -f4)
            local value="${CONFIG_VALUES[$config_name]}"
            
            echo "# $description"
            echo "CONFIG_$config_name=$value"
            echo
        done
    } > "$config_file"
}

# Generate build configuration
generate_build_config() {
    local arch="${CONFIG_VALUES[BUILD_ARCH]}"
    local boot_mode="${CONFIG_VALUES[BUILD_BOOT_MODE]}"
    local build_type="${CONFIG_VALUES[BUILD_TYPE]}"
    
    # Convert boolean configs to yes/no and collect feature flags
    local feature_flags=""
    local config_vars=""
    
    for config_name in "${!CONFIG_VALUES[@]}"; do
        local value="${CONFIG_VALUES[$config_name]}"
        local config_info="${CONFIG_DB[$config_name]}"
        local config_type=$(echo "$config_info" | cut -d: -f1)
        
        if [[ "$config_type" == "bool" ]]; then
            if [[ "$value" == "y" ]]; then
                config_vars="$config_vars\nENABLE_${config_name#ENABLE_} := yes"
                feature_flags="$feature_flags -D$config_name"
            else
                config_vars="$config_vars\nENABLE_${config_name#ENABLE_} := no"
            fi
        elif [[ "$config_type" == "choice" || "$config_type" == "int" ]]; then
            config_vars="$config_vars\n${config_name#BUILD_} := $value"
        fi
    done
    
    {
        echo "# ============================================================================="
        echo "# FOREST OS BUILD CONFIGURATION (Generated by conf.sh)"
        echo "# ============================================================================="
        echo
        echo "# Build Configuration"
        echo "ARCH := $arch"
        echo "BOOT_MODE := $boot_mode"
        echo "BUILD_TYPE := $build_type"
        echo
        echo "# Feature Flags"
        echo -e "$config_vars"
        echo
        echo "# Compiler Feature Flags"
        echo "FEATURE_FLAGS := $feature_flags"
        echo
        echo "# Build Options"
        if [[ "$build_type" == "debug" ]]; then
            echo "OPTIMIZATION_LEVEL := 0"
            echo "DEBUG_FLAGS := -g -DDEBUG"
        elif [[ "$build_type" == "release" ]]; then
            echo "OPTIMIZATION_LEVEL := 2"
            echo "DEBUG_FLAGS := -DNDEBUG"
        else # optimize
            echo "OPTIMIZATION_LEVEL := 3"
            echo "DEBUG_FLAGS := -DNDEBUG"
        fi
        echo
        echo "# Generated on $(date)"
        echo "# Configuration file: $(basename "$CONFIG_FILE")"
    } > "$BUILD_CONFIG_FILE"
}

# =============================================================================
# MENU FUNCTIONS
# =============================================================================

# Show main menu
show_main_menu() {
    local menu_text="Forest OS Configuration System v$SCRIPT_VERSION

Configure build options and features for Forest OS.
Use arrow keys to navigate, Enter to select.
"

    dialog --title "Forest OS Configuration" --menu "$menu_text" 20 70 12 \
        1 "General Setup" \
        2 "Processor Configuration" \
        3 "Memory Management" \
        4 "File Systems" \
        5 "Graphics Support" \
        6 "Networking" \
        7 "Security Features" \
        8 "Debug Options" \
        9 "Hardware Support" \
        10 "Audio Support" \
        11 "Save Configuration" \
        12 "Load Configuration" \
        13 "Generate Build Config" \
        14 "Exit" 2>"$TEMPFILE"
    
    return $?
}

# Show submenu for a category
show_category_menu() {
    local category="$1"
    local title="$2"
    shift 2
    local options=("$@")
    
    local menu_items=""
    local count=1
    
    for option in "${options[@]}"; do
        local config_info="${CONFIG_DB[$option]}"
        local config_type=$(echo "$config_info" | cut -d: -f1)
        local description=$(echo "$config_info" | cut -d: -f4)
        local value="${CONFIG_VALUES[$option]}"
        local display_value=""
        
        case "$config_type" in
            bool)
                if [[ "$value" == "y" ]]; then
                    display_value="[*]"
                else
                    display_value="[ ]"
                fi
                ;;
            choice|int)
                display_value="($value)"
                ;;
        esac
        
        menu_items="$menu_items $count \"$display_value $description\""
        ((count++))
    done
    
    eval "dialog --title \"$title\" --menu \"Select option to configure:\" 20 70 12 $menu_items" 2>"$TEMPFILE"
    
    return $?
}

# Configure boolean option
configure_bool_option() {
    local config_name="$1"
    local config_info="${CONFIG_DB[$config_name]}"
    local description=$(echo "$config_info" | cut -d: -f4)
    local help_text=$(echo "$config_info" | cut -d: -f5)
    local current_value="${CONFIG_VALUES[$config_name]}"
    
    local initial=""
    if [[ "$current_value" == "y" ]]; then
        initial="--defaultno"
    fi
    
    if dialog --title "Configure: $description" $initial --yesno "$help_text\n\nEnable this option?" 10 60; then
        CONFIG_VALUES[$config_name]="y"
    else
        CONFIG_VALUES[$config_name]="n"
    fi
}

# Configure choice option
configure_choice_option() {
    local config_name="$1"
    local config_info="${CONFIG_DB[$config_name]}"
    local description=$(echo "$config_info" | cut -d: -f4)
    local help_text=$(echo "$config_info" | cut -d: -f5)
    local current_value="${CONFIG_VALUES[$config_name]}"
    local choices="${CHOICE_OPTIONS[$config_name]}"
    
    eval "dialog --title \"Configure: $description\" --menu \"$help_text\" 15 60 8 $choices" 2>"$TEMPFILE"
    
    if [[ $? -eq 0 ]]; then
        local new_value=$(cat "$TEMPFILE")
        CONFIG_VALUES[$config_name]="$new_value"
    fi
}

# Configure integer option
configure_int_option() {
    local config_name="$1"
    local config_info="${CONFIG_DB[$config_name]}"
    local description=$(echo "$config_info" | cut -d: -f4)
    local help_text=$(echo "$config_info" | cut -d: -f5)
    local current_value="${CONFIG_VALUES[$config_name]}"
    
    dialog --title "Configure: $description" --inputbox "$help_text\n\nCurrent value: $current_value" 12 60 "$current_value" 2>"$TEMPFILE"
    
    if [[ $? -eq 0 ]]; then
        local new_value=$(cat "$TEMPFILE")
        if [[ "$new_value" =~ ^[0-9]+$ ]]; then
            CONFIG_VALUES[$config_name]="$new_value"
        else
            show_error "Invalid value. Please enter a number."
        fi
    fi
}

# Show configuration for each category
configure_general() {
    local options=(BUILD_ARCH BUILD_BOOT_MODE BUILD_TYPE BUILD_PARALLEL_JOBS)
    local choice
    
    while true; do
        if ! show_category_menu "general" "General Setup" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        local config_type=$(echo "${CONFIG_DB[$option]}" | cut -d: -f1)
        
        case "$config_type" in
            bool) configure_bool_option "$option" ;;
            choice) configure_choice_option "$option" ;;
            int) configure_int_option "$option" ;;
        esac
    done
}

configure_processor() {
    local options=(ENABLE_SMP ENABLE_ACPI ENABLE_APIC ENABLE_IOAPIC ENABLE_HPET ENABLE_MSI)
    local choice
    
    while true; do
        if ! show_category_menu "processor" "Processor Configuration" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_memory() {
    local options=(ENABLE_PAGING ENABLE_PAE ENABLE_SWAP ENABLE_MEMORY_PROTECTION ENABLE_SLAB ENABLE_MEMORY_DEBUG)
    local choice
    
    while true; do
        if ! show_category_menu "memory" "Memory Management" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_filesystems() {
    local options=(ENABLE_VFS ENABLE_EXT2 ENABLE_FAT32 ENABLE_ISO9660 ENABLE_PROCFS ENABLE_TMPFS)
    local choice
    
    while true; do
        if ! show_category_menu "filesystems" "File Systems" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_graphics() {
    local options=(ENABLE_GRAPHICS ENABLE_VESA ENABLE_FRAMEBUFFER ENABLE_VGA_TEXT ENABLE_CONSOLE)
    local choice
    
    while true; do
        if ! show_category_menu "graphics" "Graphics Support" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_networking() {
    local options=(ENABLE_NETWORKING ENABLE_ETHERNET ENABLE_TCP ENABLE_UDP ENABLE_DHCP)
    local choice
    
    while true; do
        if ! show_category_menu "networking" "Networking" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_security() {
    local options=(ENABLE_SMEP_SMAP ENABLE_NX_BIT ENABLE_ASLR ENABLE_STACK_PROTECTION ENABLE_GUARD_PAGES ENABLE_ROOT_AUTOLOGIN)
    local choice
    
    while true; do
        if ! show_category_menu "security" "Security Features" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_debug() {
    local options=(ENABLE_DEBUG_SYMBOLS ENABLE_KERNEL_DEBUG ENABLE_SERIAL_DEBUG ENABLE_PANIC_BACKTRACES ENABLE_ASSERTIONS)
    local choice
    
    while true; do
        if ! show_category_menu "debug" "Debug Options" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_hardware() {
    local options=(ENABLE_USB ENABLE_PS2 ENABLE_SERIAL ENABLE_PARALLEL ENABLE_PCI)
    local choice
    
    while true; do
        if ! show_category_menu "hardware" "Hardware Support" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

configure_audio() {
    local options=(ENABLE_AUDIO ENABLE_AC97 ENABLE_HDA)
    local choice
    
    while true; do
        if ! show_category_menu "audio" "Audio Support" "${options[@]}"; then
            return
        fi
        
        choice=$(cat "$TEMPFILE")
        if [[ -z "$choice" ]]; then
            return
        fi
        
        local option="${options[$((choice-1))]}"
        configure_bool_option "$option"
    done
}

# =============================================================================
# MAIN PROGRAM
# =============================================================================

# Check requirements
check_dialog

# Load configuration or defaults
if ! load_config "$CONFIG_FILE"; then
    load_defaults
fi

# Handle command line arguments
case "${1:-}" in
    --help|-h)
        echo "Forest OS Configuration System v$SCRIPT_VERSION"
        echo
        echo "USAGE: $0 [options]"
        echo
        echo "OPTIONS:"
        echo "  --help, -h        Show this help"
        echo "  --defaults        Load default configuration"
        echo "  --generate        Generate build configuration"
        echo "  --save [file]     Save configuration to file"
        echo "  --load [file]     Load configuration from file"
        echo
        exit 0
        ;;
    --defaults)
        load_defaults
        save_config
        generate_build_config
        echo "Default configuration loaded and saved"
        exit 0
        ;;
    --generate)
        generate_build_config
        echo "Build configuration generated: $BUILD_CONFIG_FILE"
        exit 0
        ;;
    --save)
        save_config "${2:-$CONFIG_FILE}"
        echo "Configuration saved to ${2:-$CONFIG_FILE}"
        exit 0
        ;;
    --load)
        if load_config "${2:-$CONFIG_FILE}"; then
            echo "Configuration loaded from ${2:-$CONFIG_FILE}"
        else
            echo "Failed to load configuration from ${2:-$CONFIG_FILE}"
            exit 1
        fi
        exit 0
        ;;
esac

# Main menu loop
while true; do
    if ! show_main_menu; then
        break
    fi
    
    choice=$(cat "$TEMPFILE")
    case "$choice" in
        1) configure_general ;;
        2) configure_processor ;;
        3) configure_memory ;;
        4) configure_filesystems ;;
        5) configure_graphics ;;
        6) configure_networking ;;
        7) configure_security ;;
        8) configure_debug ;;
        9) configure_hardware ;;
        10) configure_audio ;;
        11) 
            save_config
            show_info "Configuration Saved" "Configuration has been saved to:\n$CONFIG_FILE"
            ;;
        12) 
            if dialog --title "Load Configuration" --fselect "$SCRIPT_DIR/" 15 60 2>"$TEMPFILE"; then
                load_file=$(cat "$TEMPFILE")
                if load_config "$load_file"; then
                    show_info "Configuration Loaded" "Configuration loaded from:\n$load_file"
                else
                    show_error "Failed to load configuration from:\n$load_file"
                fi
            fi
            ;;
        13) 
            generate_build_config
            show_info "Build Config Generated" "Build configuration generated:\n$BUILD_CONFIG_FILE\n\nYou can now run 'make' to build Forest OS."
            ;;
        14) break ;;
        *) ;;
    esac
done

clear
