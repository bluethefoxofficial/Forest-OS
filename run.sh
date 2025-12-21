#!/bin/bash

# Forest-OS QEMU Runner Script
# Automatically sets up QEMU with proper graphics configuration for framebuffer TTY

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default configuration
DEFAULT_MEMORY="1024"
DEFAULT_TIMEOUT="3600"
DEFAULT_ISO=""
DEFAULT_MODE="graphics"

# Function to print colored output
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
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
    echo "Forest-OS QEMU Runner"
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message"
    echo "  -i, --iso FILE       Specify ISO file (default: auto-detect latest)"
    echo "  -m, --memory MB      Set memory size in MB (default: ${DEFAULT_MEMORY})"
    echo "  -t, --timeout SEC    Set timeout in seconds (default: ${DEFAULT_TIMEOUT})"
    echo "  -d, --debug          Enable debug mode with serial output"
    echo "  -g, --graphics       Run with graphics display (default)"
    echo "  -n, --nographic      Run without graphics (serial only)"
    echo "  -s, --serial FILE    Save serial output to file"
    echo "  --gdb                Enable GDB debugging (port 1234)"
    echo "  --monitor            Enable QEMU monitor on stdio"
    echo "  --build              Build before running"
    echo ""
    echo "Examples:"
    echo "  $0                           # Run with default settings"
    echo "  $0 --build                   # Build then run"
    echo "  $0 --debug --serial boot.log # Debug mode with log"
    echo "  $0 --gdb --nographic        # GDB debugging, no graphics"
    echo "  $0 -m 1024 -t 120           # 1GB RAM, 2 min timeout"
}

# Function to find latest ISO
find_latest_iso() {
    local iso_file=""
    
    # Look for ISO files matching our naming pattern
    if ls forest_nightly_*.iso >/dev/null 2>&1; then
        iso_file=$(ls -t forest_nightly_*.iso | head -n1)
    elif [ -f "forest.iso" ]; then
        iso_file="forest.iso"
    elif ls *.iso >/dev/null 2>&1; then
        iso_file=$(ls -t *.iso | head -n1)
    fi
    
    echo "$iso_file"
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
    
    print_info "Starting Forest-OS in QEMU..."
    print_info "Configuration:"
    print_info "  ISO: $iso_file"
    print_info "  Memory: ${memory}MB"
    print_info "  Mode: $mode"
    print_info "  Timeout: ${timeout_val}s"
    
    # Build QEMU command
    local qemu_cmd="timeout ${timeout_val} qemu-system-i386"
    
    # Basic system configuration
    qemu_cmd="$qemu_cmd -m ${memory}M"
    qemu_cmd="$qemu_cmd -cdrom $iso_file"
    
    # CPU and acceleration
    qemu_cmd="$qemu_cmd -cpu qemu32"
    if [ -r /dev/kvm ]; then
        qemu_cmd="$qemu_cmd -enable-kvm"
        print_info "  KVM acceleration: enabled"
    else
        print_warning "  KVM acceleration: disabled (not available)"
    fi
    
    # Network configuration  
    qemu_cmd="$qemu_cmd -netdev user,id=net0 -device rtl8139,netdev=net0"
    
    # Graphics configuration for framebuffer TTY
    if [ "$mode" = "graphics" ]; then
        # Use standard VGA for Bochs BGA support (our framebuffer TTY)
        qemu_cmd="$qemu_cmd -vga std"
        print_info "  Graphics: Bochs BGA (framebuffer TTY)"
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
    qemu_cmd="$qemu_cmd -boot d"
    
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
debug="false"
serial_file=""
enable_gdb="false"
enable_monitor="false"
should_build="false"

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            show_usage
            exit 0
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
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Main execution
main() {
    print_info "Forest-OS QEMU Runner v1.0"
    print_info "Framebuffer TTY Edition"
    echo ""
    
    # Build if requested
    if [ "$should_build" = "true" ]; then
        if ! build_os; then
            exit 1
        fi
        echo ""
    fi
    
    # Find ISO file if not specified
    if [ -z "$iso_file" ]; then
        iso_file=$(find_latest_iso)
        if [ -z "$iso_file" ]; then
            print_error "No ISO file found. Build the OS first with: make"
            exit 1
        fi
    fi
    
    # Check if ISO exists
    if ! check_iso "$iso_file"; then
        exit 1
    fi
    
    echo ""
    
    # Run QEMU
    run_qemu "$iso_file" "$memory" "$timeout_val" "$mode" "$debug" "$serial_file" "$enable_gdb" "$enable_monitor"
    exit $?
}

# Run main function
main "$@"
