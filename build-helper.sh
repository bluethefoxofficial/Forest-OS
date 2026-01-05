#!/bin/bash
# =============================================================================
# FOREST OS BUILD HELPER SCRIPT
# =============================================================================
# Quick helper script for common build operations
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
NC='\033[0m'

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

show_usage() {
    cat << EOF
Forest OS Build Helper

USAGE:
    $0 <command> [options]

COMMANDS:
    quick-32        Quick 32-bit BIOS debug build
    quick-64        Quick 64-bit BIOS debug build
    release-32      32-bit BIOS release build + ISO
    release-64      64-bit BIOS release build + ISO
    uefi-32         32-bit UEFI debug build + IMG
    uefi-64         64-bit UEFI debug build + IMG
    all-debug       All architectures, debug builds
    all-release     All architectures, release builds
    dist            Full distribution build
    test-32         Test 32-bit kernel in QEMU
    test-64         Test 64-bit kernel in QEMU
    clean           Clean all build files
    toolchain       Check toolchain status
    help            Show this help

EXAMPLES:
    $0 quick-32     # Quick 32-bit kernel build
    $0 release-64   # Build and create 64-bit release ISO
    $0 test-32      # Test 32-bit kernel in QEMU
    $0 dist         # Full distribution build

EOF
}

check_toolchain() {
    print_info "Checking Forest OS toolchain..."
    
    if [ ! -x "$SCRIPT_DIR/forestos-toolchain/install/bin/i686-forestos-gcc" ]; then
        print_error "Forest OS toolchain not found!"
        print_info "Please build the toolchain first with the instructions in forestos-toolchain/README.md"
        return 1
    fi
    
    local gcc_version=$("$SCRIPT_DIR/forestos-toolchain/install/bin/i686-forestos-gcc" --version | head -n1)
    print_success "Toolchain found: $gcc_version"
    return 0
}

quick_build() {
    local arch=$1
    local build_type=${2:-debug}
    local boot_mode=${3:-bios}
    
    print_info "Quick ${arch}-bit ${boot_mode} ${build_type} build..."
    
    if ! check_toolchain; then
        return 1
    fi
    
    make ARCH="$arch" BOOT_MODE="$boot_mode" BUILD_TYPE="$build_type" build -j$(nproc)
    print_success "Quick build completed!"
}

release_build() {
    local arch=$1
    local boot_mode=${2:-bios}
    
    print_info "Release ${arch}-bit ${boot_mode} build..."
    
    if ! check_toolchain; then
        return 1
    fi
    
    make ARCH="$arch" BOOT_MODE="$boot_mode" BUILD_TYPE="release" all -j$(nproc)
    
    if [ "$boot_mode" = "uefi" ]; then
        print_success "Release build completed! UEFI image created."
    else
        print_success "Release build completed! ISO created."
    fi
}

test_kernel() {
    local arch=$1
    local build_type=${2:-debug}
    
    print_info "Testing ${arch}-bit kernel in QEMU..."
    
    # Build if needed
    if [ ! -f "build/${arch}bit-bios-${build_type}/boot/kernel.bin" ]; then
        print_info "Kernel not found, building first..."
        quick_build "$arch" "$build_type"
    fi
    
    make ARCH="$arch" BUILD_TYPE="$build_type" run
}

full_distribution() {
    print_info "Starting full distribution build..."
    
    if ! check_toolchain; then
        return 1
    fi
    
    ./build-dist.sh
    print_success "Full distribution build completed!"
}

all_builds() {
    local build_type=$1
    
    print_info "Building all architectures (${build_type})..."
    
    if ! check_toolchain; then
        return 1
    fi
    
    for arch in 32 64; do
        for boot_mode in bios uefi; do
            print_info "Building ${arch}-bit ${boot_mode} ${build_type}..."
            make ARCH="$arch" BOOT_MODE="$boot_mode" BUILD_TYPE="$build_type" all -j$(nproc) || {
                print_warning "Failed to build ${arch}-bit ${boot_mode} ${build_type}"
            }
        done
    done
    
    print_success "All builds completed!"
}

clean_all() {
    print_info "Cleaning all build files..."
    make clean-all
    print_success "Clean completed!"
}

show_toolchain_info() {
    print_info "Forest OS Toolchain Information"
    echo "======================================"
    
    local toolchain_dir="$SCRIPT_DIR/forestos-toolchain"
    
    if [ -d "$toolchain_dir" ]; then
        print_success "Toolchain directory found: $toolchain_dir"
        
        if [ -x "$toolchain_dir/install/bin/i686-forestos-gcc" ]; then
            local gcc_version=$("$toolchain_dir/install/bin/i686-forestos-gcc" --version | head -n1)
            print_success "GCC: $gcc_version"
            
            local binutils_version=$("$toolchain_dir/install/bin/i686-forestos-ld" --version | head -n1)
            print_success "Binutils: $binutils_version"
            
            print_info "Available tools:"
            ls -1 "$toolchain_dir/install/bin/i686-forestos-"* | sed 's|.*/||' | sed 's/^/  - /'
            
        else
            print_error "Toolchain binaries not found in install/bin/"
        fi
        
        if [ -d "$toolchain_dir/sysroot" ]; then
            print_success "Sysroot found: $toolchain_dir/sysroot"
        else
            print_warning "Sysroot not found"
        fi
        
    else
        print_error "Toolchain directory not found: $toolchain_dir"
        print_info "Please build the toolchain first"
    fi
}

# Main command processing
case "${1:-help}" in
    quick-32)
        quick_build 32
        ;;
    quick-64)
        quick_build 64
        ;;
    release-32)
        release_build 32
        ;;
    release-64)
        release_build 64
        ;;
    uefi-32)
        quick_build 32 debug uefi
        ;;
    uefi-64)
        quick_build 64 debug uefi
        ;;
    all-debug)
        all_builds debug
        ;;
    all-release)
        all_builds release
        ;;
    dist)
        full_distribution
        ;;
    test-32)
        test_kernel 32
        ;;
    test-64)
        test_kernel 64
        ;;
    clean)
        clean_all
        ;;
    toolchain)
        show_toolchain_info
        ;;
    help|--help|-h)
        show_usage
        ;;
    *)
        print_error "Unknown command: $1"
        echo
        show_usage
        exit 1
        ;;
esac