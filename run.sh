#!/bin/bash

# Forest-OS QEMU Runner Script
# Optimized for testing Canopy desktop environment with proper GUI display

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
DEFAULT_MEMORY="512"
DEFAULT_TIMEOUT="0"
DEFAULT_ISO=""
DEFAULT_MODE="graphics"
DEFAULT_ARCH="64"
DEFAULT_SOUND="sb16"

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

# Simple usage for Canopy testing
show_canopy_usage() {
    echo "Forest OS Canopy Test Runner"
    echo "Quick usage for testing Canopy desktop environment:"
    echo ""
    echo "  ./run.sh                    # Test Canopy with default settings"
    echo "  ./run.sh --uefi            # Test UEFI version"
    echo "  ./run.sh --bios            # Test BIOS version"
    echo "  ./run.sh --debug           # Enable debug output"
    echo ""
    echo "Full usage:"
    echo "  ./run.sh [OPTIONS]"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to show usage
show_usage() {
    echo "Forest OS QEMU Runner - Canopy Testing Edition"
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Quick Options:"
    echo "  -h, --help           Show this help message"
    echo "      --uefi            Use UEFI boot (default)"
    echo "      --bios            Use BIOS boot"
    echo "  -a, --arch 32|64     Set architecture (default: ${DEFAULT_ARCH})"
    echo "  -m, --memory MB      Set memory size in MB (default: ${DEFAULT_MEMORY})"
    echo "  -d, --debug          Enable debug mode with serial output"
    echo ""
    echo "Advanced Options:"
    echo "  -i, --iso FILE       Specify ISO file"
    echo "  -t, --timeout SEC    Set timeout in seconds (default: ${DEFAULT_TIMEOUT})"
    echo "  -g, --graphics       Run with graphics display (default)"
    echo "  -n, --nographic      Run without graphics (serial only)"
    echo "  -s, --serial FILE    Save serial output to file"
    echo "  --gdb                Enable GDB debugging (port 1234)"
    echo "  --monitor            Enable QEMU monitor on stdio"
    echo "  --build              Build before running"
    echo "  --sound DEVICE       Set sound device (default: ${DEFAULT_SOUND})"
    echo "  --dry-run             Show QEMU command without executing"
    echo ""
    echo "Examples for Canopy testing:"
    echo "  $0                           # Test Canopy with 64-bit UEFI"
    echo "  $0 --bios                    # Test Canopy with 64-bit BIOS"
    echo "  $0 -a 32                     # Test 32-bit version"
    echo "  $0 -m 1024                    # Test with 1GB RAM"
    echo "  $0 --debug                    # Test with debug output"
    echo "  $0 --uefi --dry-run           # Show UEFI command only"
}

# Function to find latest ISO/image
find_latest_image() {
    local arch="$1"
    local boot_mode="$2"
    local image_file=""
    
    if [ "$boot_mode" = "uefi" ]; then
        # Look for UEFI disk images
        if ls dist/forestos_${arch}bit_uefi_*.img >/dev/null 2>&1; then
            image_file=$(ls -t dist/forestos_${arch}bit_uefi_*.img | head -n1)
        fi
        echo "$image_file"
    else
        # Look for BIOS ISO files
        if ls dist/forestos_${arch}bit_bios_*.iso >/dev/null 2>&1; then
            image_file=$(ls -t dist/forestos_${arch}bit_bios_*.iso | head -n1)
        elif ls dist/forestos_${arch}bit_*.iso >/dev/null 2>&1; then
            image_file=$(ls -t dist/forestos_${arch}bit_*.iso | head -n1)
        # Fallback to old naming conventions
        elif ls forest_nightly_*.iso >/dev/null 2>&1; then
            image_file=$(ls -t forest_nightly_*.iso | head -n1)
        elif [ -f "forest.iso" ]; then
            image_file="forest.iso"
        elif ls dist/*.iso >/dev/null 2>&1; then
            image_file=$(ls -t dist/*.iso | head -n1)
        elif ls *.iso >/dev/null 2>&1; then
            image_file=$(ls -t *.iso | head -n1)
        fi
        echo "$image_file"
    fi
}

# Function to build the OS
build_os() {
    print_info "Building Forest-OS..."
    if make; then
        print_success "Build completed successfully"
        return 0
    else
        print_error "Build failed"
        return 1
    fi
}

# Function to check if ISO exists
check_iso() {
    local iso_file="$1"
    
    if [ ! -f "$iso_file" ]; then
        print_error "ISO file not found: $iso_file"
        print_info "Available ISO files:"
        ls -la *.iso 2>/dev/null || echo "  No ISO files found"
        return 1
    fi
    
    print_success "Using ISO: $iso_file"
    return 0
}

# Function to validate sound device
validate_sound_device() {
    local sound_device="$1"
    
    case "$sound_device" in
        "none"|"sb16"|"ac97"|"hda"|"es1370"|"adlib"|"gus")
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# Function to configure sound device
configure_sound() {
    local sound_device="$1"
    local sound_cmd=""
    
    case "$sound_device" in
        "none")
            sound_cmd=""
            ;;
        "sb16")
            sound_cmd="-device sb16"
            ;;
        "ac97")
            sound_cmd="-device ac97"
            ;;
        "hda")
            sound_cmd="-device intel-hda -device hda-duplex"
            ;;
        "es1370")
            sound_cmd="-device es1370"
            ;;
        "adlib")
            sound_cmd="-device adlib"
            ;;
        "gus")
            sound_cmd="-device gus"
            ;;
        *)
            echo "Unknown sound device: $sound_device" >&2
            echo "Available sound devices: sb16, ac97, hda, es1370, adlib, gus, none" >&2
            echo "Forest OS native support: sb16, ac97, hda, es1370" >&2
            return 1
            ;;
    esac
    
    echo "$sound_cmd"
}

# Function to run QEMU
run_qemu() {
    local iso_file="$1"
    local memory="$2"
    local timeout_val="$3"
    local mode="$4"
    local debug="$5"
    local serial_file="$6"
    local enable_gdb="$7"
    local enable_monitor="$8"
    local arch="$9"
    local sound_device="${10}"
    local dry_run="${11}"

    print_info "Starting Forest-OS in QEMU..."
    print_info "Configuration:"
    print_info "  Architecture: ${arch}-bit"
    print_info "  ISO: $iso_file"
    print_info "  Memory: ${memory}MB"
    print_info "  Mode: $mode"
    print_info "  Timeout: ${timeout_val}s"
    print_info "  Sound: $sound_device"

    # Select QEMU binary and CPU based on architecture
    local qemu_bin
    local cpu_type
    if [ "$arch" = "64" ]; then
        qemu_bin="qemu-system-x86_64"
        cpu_type="qemu64"
    else
        qemu_bin="qemu-system-i386"
        cpu_type="qemu32"
    fi

    # Build QEMU command (no timeout for GUI testing)
    if [ "$timeout_val" -gt 0 ]; then
        qemu_cmd="timeout ${timeout_val} ${qemu_bin}"
    else
        qemu_cmd="$qemu_bin"
    fi

    # Basic system configuration
    qemu_cmd="$qemu_cmd -m ${memory}M"
    
    if [ "$BOOT_MODE" = "uefi" ]; then
        # UEFI boot configuration
        qemu_cmd="$qemu_cmd -bios /usr/share/ovmf/OVMF.fd -drive format=raw,file=$iso_file"
        print_info "  Boot: UEFI with OVMF firmware"
    else
        # BIOS boot configuration
        qemu_cmd="$qemu_cmd -cdrom $iso_file"
        print_info "  Boot: BIOS from CDROM"
    fi
    
    # CPU and acceleration
    qemu_cmd="$qemu_cmd -cpu ${cpu_type}"
    if [ -r /dev/kvm ]; then
        qemu_cmd="$qemu_cmd -enable-kvm"
        print_info "  KVM acceleration: enabled"
    else
        print_warning "  KVM acceleration: disabled (not available)"
    fi
    
    # Input devices for Canopy
    qemu_cmd="$qemu_cmd -device usb-ehci"
    print_info "  Input: USB mouse and keyboard (for Canopy)"
    
    # Network configuration (for future internet connectivity)
    qemu_cmd="$qemu_cmd -netdev user,id=net0 -device rtl8139,netdev=net0"
    print_info "  Network: User-mode networking enabled"
    
    # Sound configuration
    local sound_config
    sound_config=$(configure_sound "$sound_device")
    if [ $? -ne 0 ]; then
        return 1
    fi
    if [ -n "$sound_config" ]; then
        qemu_cmd="$qemu_cmd $sound_config"
    fi
    
    # Graphics configuration for Canopy desktop environment
    if [ "$mode" = "graphics" ]; then
        # Use modern graphics with proper GUI display
        qemu_cmd="$qemu_cmd -device VGA,vgamem_mb=128"
        print_info "  Graphics: GTK display with OpenGL (for Canopy DE)"
        print_info "  Device: VGA with 128MB VRAM (for framebuffer)"
    else
        qemu_cmd="$qemu_cmd -nographic"
        print_info "  Graphics: disabled (serial console only)"
    fi
    
    # Serial configuration
    if [ -n "$serial_file" ]; then
        qemu_cmd="$qemu_cmd -serial file:$serial_file"
        print_info "  Serial output: $serial_file"
    elif [ "$debug" = "true" ] || [ "$mode" = "nographic" ]; then
        if [ "$mode" = "graphics" ]; then
            qemu_cmd="$qemu_cmd -serial stdio"
        fi
        print_info "  Serial output: stdio"
    fi
    
    # Debug configuration
    if [ "$enable_gdb" = "true" ]; then
        qemu_cmd="$qemu_cmd -gdb tcp::1234 -S"
        print_info "  GDB: enabled on port 1234 (paused at start)"
        print_info "  Connect with: gdb -ex 'target remote localhost:1234'"
    fi
    
    # Monitor configuration
    if [ "$enable_monitor" = "true" ]; then
        qemu_cmd="$qemu_cmd -monitor stdio"
        print_info "  QEMU Monitor: enabled on stdio"
    fi
    
    # Boot configuration
    if [ "$BOOT_MODE" = "uefi" ]; then
        # UEFI handles boot order automatically
        print_info "  Boot: EFI system"
    else
        qemu_cmd="$qemu_cmd -boot d"
        print_info "  Boot: From CD-ROM"
    fi
    
    if [ "$dry_run" = "true" ]; then
        print_info "Dry-run mode: QEMU command would be:"
        print_info "$qemu_cmd"
        echo ""
        print_success "Dry-run completed - command displayed above"
        return 0
    fi
    
    print_info "Starting QEMU..."
    print_info "Command: $qemu_cmd"
    echo ""
    
    # Execute QEMU
    if eval "$qemu_cmd"; then
        print_success "QEMU session completed successfully"
        return 0
    else
        local exit_code=$?
        if [ $exit_code -eq 124 ]; then
            print_warning "QEMU session timed out after ${timeout_val} seconds"
        else
            print_error "QEMU session ended with error code: $exit_code"
        fi
        return $exit_code
    fi
}

# Parse command line arguments
memory="$DEFAULT_MEMORY"
timeout_val="$DEFAULT_TIMEOUT"
iso_file="$DEFAULT_ISO"
mode="$DEFAULT_MODE"
arch="$DEFAULT_ARCH"
debug="false"
serial_file=""
enable_gdb="false"
enable_monitor="false"
should_build="false"
sound_device="$DEFAULT_SOUND"
dry_run="false"
BOOT_MODE="uefi"  # Default to UEFI

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
            ;;
        --uefi)
            BOOT_MODE="uefi"
            shift
            ;;
        --bios)
            BOOT_MODE="bios"
            shift
            ;;
        -a|--arch)
            arch="$2"
            if [ "$arch" != "32" ] && [ "$arch" != "64" ]; then
                print_error "Invalid architecture: $arch (must be 32 or 64)"
                exit 1
            fi
            shift 2
            ;;
        -i|--iso)
            iso_file="$2"
            shift 2
            ;;
        -m|--memory)
            memory="$2"
            shift 2
            ;;
        -t|--timeout)
            timeout_val="$2"
            shift 2
            ;;
        -d|--debug)
            debug="true"
            shift
            ;;
        -g|--graphics)
            mode="graphics"
            shift
            ;;
        -n|--nographic)
            mode="nographic"
            shift
            ;;
        -s|--serial)
            serial_file="$2"
            shift 2
            ;;
        --gdb)
            enable_gdb="true"
            shift
            ;;
        --monitor)
            enable_monitor="true"
            shift
            ;;
        --build)
            should_build="true"
            shift
            ;;
        --sound)
            sound_device="$2"
            if ! validate_sound_device "$sound_device"; then
                print_error "Invalid sound device: $sound_device"
                print_info "Available sound devices: sb16, ac97, hda, es1370, adlib, gus, none"
                print_info "Forest OS native support: sb16, ac97, hda, es1370"
                exit 1
            fi
            shift 2
            ;;
        --dry-run)
            dry_run="true"
            shift
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Main execution
main() {
    # Quick options for Canopy testing
    uefi_mode="false"
    bios_mode="false"
    
    # Check for quick options first
    while [[ $# -gt 0 ]]; do
        case $1 in
            --uefi)
                uefi_mode="true"
                BOOT_MODE="uefi"
                shift
                ;;
            --bios)
                bios_mode="true"
                BOOT_MODE="bios"
                shift
                ;;
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                break
                ;;
        esac
    done
    
    # Set defaults if no mode specified
    if [ "$uefi_mode" = "false" ] && [ "$bios_mode" = "false" ]; then
        uefi_mode="true"
        BOOT_MODE="uefi"
    fi
    
    print_info "Forest OS QEMU Runner - Canopy Testing Edition"
    print_info "Mode: $BOOT_MODE (${arch}-bit)"
    echo ""

    # Build if requested
    if [ "$should_build" = "true" ]; then
        if ! build_os; then
            exit 1
        fi
        echo ""
    fi

    # Find image file if not specified
    if [ -z "$iso_file" ]; then
        iso_file=$(find_latest_image "$arch" "$BOOT_MODE")
        if [ -z "$iso_file" ]; then
            if [ "$BOOT_MODE" = "uefi" ]; then
                print_warning "No ${arch}-bit UEFI image found."
                # Fallback to BIOS ISO for UEFI mode
                print_info "Looking for BIOS ISO as fallback..."
                iso_file=$(find_latest_image "$arch" "bios")
                if [ -z "$iso_file" ]; then
                    # Try other architecture if current arch not found
                    local fallback_arch="32"
                    if [ "$arch" = "32" ]; then
                        fallback_arch="64"
                    fi
                    print_info "No ${arch}-bit BIOS images found, trying ${fallback_arch}-bit..."
                    iso_file=$(find_latest_image "$fallback_arch" "bios")
                    if [ -z "$iso_file" ]; then
                        print_error "No BIOS or UEFI images found for any architecture."
                        print_info "Build UEFI with: make ARCH=$arch BOOT_MODE=uefi BUILD_TYPE=release img"
                        print_info "Build BIOS with: make ARCH=$arch BOOT_MODE=bios BUILD_TYPE=release iso"
                        print_info "Available files in dist/:"
                        ls -la dist/*.{iso,img} 2>/dev/null || echo "  No images found"
                        exit 1
                    else
                        print_info "Falling back to ${fallback_arch}-bit BIOS ISO: $iso_file"
                        print_warning "Note: Running in BIOS mode with ${fallback_arch}-bit architecture"
                        BOOT_MODE="bios"
                        arch="$fallback_arch"
                    fi
                else
                    # Check if the found BIOS ISO matches the requested architecture
                    if echo "$iso_file" | grep -q "_${arch}bit_"; then
                        print_info "Falling back to BIOS ISO: $iso_file"
                        print_warning "Note: Running in BIOS mode since no UEFI image available"
                        BOOT_MODE="bios"
                    else
                        # Extract actual architecture from the ISO filename
                        local actual_arch=$(echo "$iso_file" | sed -n 's/.*forestos_\([0-9]*\)bit_.*/\1/p')
                        if [ -n "$actual_arch" ] && [ "$actual_arch" != "$arch" ]; then
                            print_info "Falling back to ${actual_arch}-bit BIOS ISO: $iso_file"
                            print_warning "Note: Running in BIOS mode with ${actual_arch}-bit architecture (no ${arch}-bit images found)"
                            BOOT_MODE="bios"
                            arch="$actual_arch"
                        else
                            print_info "Falling back to BIOS ISO: $iso_file"
                            print_warning "Note: Running in BIOS mode since no UEFI image available"
                            BOOT_MODE="bios"
                        fi
                    fi
                fi
            else
                print_error "No ${arch}-bit BIOS image found."
                print_info "Build with: make ARCH=$arch BOOT_MODE=bios BUILD_TYPE=release iso"
                print_info "Available files in dist/:"
                ls -la dist/*.{iso,img} 2>/dev/null || echo "  No images found"
                exit 1
            fi
        fi
    fi
    
    # Check if image exists
    if [ "$BOOT_MODE" = "uefi" ]; then
        if [ ! -f "$iso_file" ]; then
            print_error "UEFI image file not found: $iso_file"
            exit 1
        fi
        print_success "Using UEFI image: $iso_file"
    else
        if ! check_iso "$iso_file"; then
            exit 1
        fi
    fi

    echo ""

    # Run QEMU (or show command if dry-run)
    run_qemu "$iso_file" "$memory" "$timeout_val" "$mode" "$debug" "$serial_file" "$enable_gdb" "$enable_monitor" "$arch" "$sound_device" "$dry_run"
    exit $?
}

# Run main function
main "$@"
