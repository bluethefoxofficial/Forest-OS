#!/bin/bash
# =============================================================================
# FOREST OS DISTRIBUTION BUILD SCRIPT
# =============================================================================
# Comprehensive build system for creating Forest OS distributions with
# support for multiple architectures, boot modes, and packaging formats
# =============================================================================

set -euo pipefail

# =============================================================================
# SCRIPT CONFIGURATION
# =============================================================================

SCRIPT_VERSION="2.0.0"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_START_TIME=$(date +%s)
BUILD_TIMESTAMP=$(date +"%Y%m%d_%H%M%S")

# Color output functions
print_color() {
    local color="$1"
    local text="$2"
    case "$color" in
        red) echo -e "\033[31m$text\033[0m" ;;
        green) echo -e "\033[32m$text\033[0m" ;;
        yellow) echo -e "\033[33m$text\033[0m" ;;
        blue) echo -e "\033[34m$text\033[0m" ;;
        purple) echo -e "\033[35m$text\033[0m" ;;
        cyan) echo -e "\033[36m$text\033[0m" ;;
        *) echo "$text" ;;
    esac
}

print_header() {
    print_color cyan "==============================================================================="
    print_color cyan "$1"
    print_color cyan "==============================================================================="
}

print_step() {
    print_color green "[STEP] $1"
}

print_info() {
    print_color blue "[INFO] $1"
}

print_warning() {
    print_color yellow "[WARN] $1"
}

print_error() {
    print_color red "[ERROR] $1"
}

# =============================================================================
# CONFIGURATION VARIABLES
# =============================================================================

# Default configuration
DEFAULT_ARCHITECTURES="32 64"
DEFAULT_BOOT_MODES="bios uefi"
DEFAULT_BUILD_TYPES="debug release"
DEFAULT_DIST_FORMATS="iso img tar"

# Build configuration
ARCHITECTURES="${ARCHITECTURES:-$DEFAULT_ARCHITECTURES}"
BOOT_MODES="${BOOT_MODES:-$DEFAULT_BOOT_MODES}"
BUILD_TYPES="${BUILD_TYPES:-$DEFAULT_BUILD_TYPES}"
DIST_FORMATS="${DIST_FORMATS:-$DEFAULT_DIST_FORMATS}"

# Directory configuration
DIST_DIR="${DIST_DIR:-$SCRIPT_DIR/dist}"
BUILD_DIR="${BUILD_DIR:-$SCRIPT_DIR/build}"
RELEASE_DIR="${RELEASE_DIR:-$DIST_DIR/releases}"
PACKAGE_DIR="${PACKAGE_DIR:-$DIST_DIR/packages}"
LOGS_DIR="${LOGS_DIR:-$DIST_DIR/logs}"

# Build options
PARALLEL_JOBS="${PARALLEL_JOBS:-$(nproc)}"
VERBOSE="${VERBOSE:-false}"
CLEAN_BEFORE_BUILD="${CLEAN_BEFORE_BUILD:-true}"
GENERATE_CHECKSUMS="${GENERATE_CHECKSUMS:-true}"
COMPRESS_IMAGES="${COMPRESS_IMAGES:-true}"

# Advanced options
ENABLE_TESTING="${ENABLE_TESTING:-false}"
GENERATE_DOCS="${GENERATE_DOCS:-false}"
SIGN_RELEASES="${SIGN_RELEASES:-false}"
UPLOAD_RELEASES="${UPLOAD_RELEASES:-false}"

# =============================================================================
# UTILITY FUNCTIONS
# =============================================================================

check_dependencies() {
    print_step "Checking build dependencies..."
    
    local missing_deps=()
    local deps=(
        "make"
        "gcc"
        "nasm"
        "grub-mkrescue"
        "xorriso"
        "tar"
        "gzip"
        "sha256sum"
    )
    
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            missing_deps+=("$dep")
        fi
    done
    
    # Check for optional dependencies
    if command -v qemu-system-i386 &> /dev/null; then
        print_info "QEMU found - testing will be available"
    else
        print_warning "QEMU not found - testing will be skipped"
        ENABLE_TESTING=false
    fi
    
    if command -v ovmf &> /dev/null || [ -f "/usr/share/ovmf/OVMF.fd" ]; then
        print_info "OVMF found - UEFI testing will be available"
    else
        print_warning "OVMF not found - UEFI testing may not work"
    fi
    
    if [ ${#missing_deps[@]} -ne 0 ]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_error "Please install missing dependencies and try again"
        exit 1
    fi
    
    print_info "All required dependencies found"
}

check_toolchain() {
    print_step "Checking Forest OS toolchain..."
    
    local toolchain_dir="$SCRIPT_DIR/forestos-toolchain"
    local toolchain_bin="$toolchain_dir/install/bin"
    
    if [ ! -d "$toolchain_dir" ]; then
        print_error "Forest OS toolchain not found at: $toolchain_dir"
        print_error "Please build the toolchain first"
        exit 1
    fi
    
    if [ ! -x "$toolchain_bin/i686-forestos-gcc" ]; then
        print_error "Forest OS compiler not found: $toolchain_bin/i686-forestos-gcc"
        exit 1
    fi
    
    local gcc_version=$("$toolchain_bin/i686-forestos-gcc" --version | head -n1 | awk '{print $NF}')
    print_info "Using Forest OS GCC version: $gcc_version"
}

setup_directories() {
    print_step "Setting up build directories..."
    
    for dir in "$DIST_DIR" "$BUILD_DIR" "$RELEASE_DIR" "$PACKAGE_DIR" "$LOGS_DIR"; do
        mkdir -p "$dir"
        print_info "Created directory: $dir"
    done
}

cleanup_previous_builds() {
    if [ "$CLEAN_BEFORE_BUILD" = "true" ]; then
        print_step "Cleaning previous builds..."
        make clean-all
        rm -rf "$BUILD_DIR"/*
        print_info "Previous builds cleaned"
    fi
}

# =============================================================================
# BUILD FUNCTIONS
# =============================================================================

build_configuration() {
    local arch="$1"
    local boot_mode="$2"
    local build_type="$3"
    local log_file="$LOGS_DIR/build_${arch}bit_${boot_mode}_${build_type}.log"
    
    print_step "Building ${arch}-bit ${boot_mode} ${build_type}..."
    
    local build_cmd="make ARCH=$arch BOOT_MODE=$boot_mode BUILD_TYPE=$build_type all -j$PARALLEL_JOBS"
    
    if [ "$VERBOSE" = "true" ]; then
        print_info "Running: $build_cmd"
        $build_cmd 2>&1 | tee "$log_file"
    else
        if $build_cmd > "$log_file" 2>&1; then
            print_info "Build successful: ${arch}-bit ${boot_mode} ${build_type}"
        else
            print_error "Build failed: ${arch}-bit ${boot_mode} ${build_type}"
            print_error "Check log file: $log_file"
            return 1
        fi
    fi
}

build_all_configurations() {
    print_header "BUILDING ALL CONFIGURATIONS"
    
    local total_builds=0
    local successful_builds=0
    local failed_builds=()
    
    for arch in $ARCHITECTURES; do
        for boot_mode in $BOOT_MODES; do
            for build_type in $BUILD_TYPES; do
                ((total_builds++))
                
                if build_configuration "$arch" "$boot_mode" "$build_type"; then
                    ((successful_builds++))
                else
                    failed_builds+=("${arch}-bit ${boot_mode} ${build_type}")
                fi
            done
        done
    done
    
    print_info "Build summary: $successful_builds/$total_builds successful"
    
    if [ ${#failed_builds[@]} -gt 0 ]; then
        print_warning "Failed builds:"
        for build in "${failed_builds[@]}"; do
            print_warning "  - $build"
        done
    fi
    
    if [ $successful_builds -eq 0 ]; then
        print_error "No builds succeeded - aborting"
        exit 1
    fi
}

# =============================================================================
# PACKAGING FUNCTIONS
# =============================================================================

create_iso_packages() {
    if [[ "$DIST_FORMATS" == *"iso"* ]]; then
        print_step "Creating ISO packages..."
        
        for arch in $ARCHITECTURES; do
            for build_type in $BUILD_TYPES; do
                local iso_path="$DIST_DIR/forestos_${arch}bit_bios_${build_type}_*.iso"
                if compgen -G "$iso_path" > /dev/null; then
                    for iso in $iso_path; do
                        local filename=$(basename "$iso")
                        local target="$PACKAGE_DIR/$filename"
                        
                        cp "$iso" "$target"
                        print_info "Packaged ISO: $filename"
                        
                        if [ "$COMPRESS_IMAGES" = "true" ]; then
                            gzip -9 "$target"
                            print_info "Compressed: $filename.gz"
                        fi
                    done
                fi
            done
        done
    fi
}

create_img_packages() {
    if [[ "$DIST_FORMATS" == *"img"* ]]; then
        print_step "Creating disk image packages..."
        
        for arch in $ARCHITECTURES; do
            for build_type in $BUILD_TYPES; do
                local img_path="$DIST_DIR/forestos_${arch}bit_uefi_${build_type}_*.img"
                if compgen -G "$img_path" > /dev/null; then
                    for img in $img_path; do
                        local filename=$(basename "$img")
                        local target="$PACKAGE_DIR/$filename"
                        
                        cp "$img" "$target"
                        print_info "Packaged IMG: $filename"
                        
                        if [ "$COMPRESS_IMAGES" = "true" ]; then
                            gzip -9 "$target"
                            print_info "Compressed: $filename.gz"
                        fi
                    done
                fi
            done
        done
    fi
}

create_source_package() {
    if [[ "$DIST_FORMATS" == *"tar"* ]]; then
        print_step "Creating source package..."
        
        local source_archive="$PACKAGE_DIR/forestos-source-$BUILD_TIMESTAMP.tar.gz"
        local temp_dir=$(mktemp -d)
        local source_dir="$temp_dir/forestos-source"
        
        # Create source directory structure
        mkdir -p "$source_dir"
        
        # Copy source files
        cp -r src/ "$source_dir/"
        cp -r userspace/ "$source_dir/"
        cp -r libs/ "$source_dir/"
        cp -r docs/ "$source_dir/"
        cp -r Grub/ "$source_dir/"
        cp Makefile "$source_dir/"
        cp build-dist.sh "$source_dir/"
        cp README.md "$source_dir/"
        
        # Create archive
        tar -czf "$source_archive" -C "$temp_dir" forestos-source/
        rm -rf "$temp_dir"
        
        print_info "Created source package: $(basename "$source_archive")"
    fi
}

create_complete_package() {
    print_step "Creating complete distribution package..."
    
    local complete_archive="$PACKAGE_DIR/forestos-complete-$BUILD_TIMESTAMP.tar.gz"
    local temp_dir=$(mktemp -d)
    local complete_dir="$temp_dir/forestos-complete"
    
    mkdir -p "$complete_dir"
    
    # Copy all built artifacts
    if [ -d "$BUILD_DIR" ]; then
        cp -r "$BUILD_DIR"/* "$complete_dir/"
    fi
    
    # Copy documentation
    if [ -d "docs/" ]; then
        cp -r docs/ "$complete_dir/"
    fi
    
    # Copy build scripts
    cp Makefile build-dist.sh "$complete_dir/"
    
    # Create README for the package
    cat > "$complete_dir/README.txt" << EOF
Forest OS Complete Distribution Package
======================================

Generated: $(date)
Build Timestamp: $BUILD_TIMESTAMP

This package contains pre-built Forest OS binaries for all supported
architectures and boot modes.

Architecture Support:
- 32-bit (i686)
- 64-bit (x86_64)

Boot Modes:
- BIOS (Legacy)
- UEFI

Build Types:
- Debug (with symbols)
- Release (optimized)

Contents:
- Pre-built kernel binaries
- ISO images for BIOS boot
- Disk images for UEFI boot
- Documentation
- Build scripts

For more information, see the included documentation.
EOF
    
    tar -czf "$complete_archive" -C "$temp_dir" forestos-complete/
    rm -rf "$temp_dir"
    
    print_info "Created complete package: $(basename "$complete_archive")"
}

# =============================================================================
# VERIFICATION AND TESTING
# =============================================================================

generate_checksums() {
    if [ "$GENERATE_CHECKSUMS" = "true" ]; then
        print_step "Generating checksums..."
        
        local checksum_file="$PACKAGE_DIR/SHA256SUMS"
        
        cd "$PACKAGE_DIR"
        sha256sum *.tar.gz *.iso* *.img* 2>/dev/null | sort > SHA256SUMS
        cd - > /dev/null
        
        print_info "Generated checksums: SHA256SUMS"
    fi
}

test_builds() {
    if [ "$ENABLE_TESTING" = "true" ]; then
        print_step "Testing builds..."
        
        # Test BIOS boot
        for arch in $ARCHITECTURES; do
            local iso_path="$PACKAGE_DIR/forestos_${arch}bit_bios_debug_*.iso"
            if compgen -G "$iso_path" > /dev/null; then
                local iso=$(echo $iso_path | head -n1)
                print_info "Testing BIOS boot: $(basename "$iso")"
                
                timeout 30 qemu-system-${arch/32/i386} \
                    -cdrom "$iso" -serial stdio -no-shutdown \
                    -display none -no-reboot &
                
                local qemu_pid=$!
                sleep 10
                kill $qemu_pid 2>/dev/null || true
                print_info "BIOS test completed"
            fi
        done
    fi
}

create_release_notes() {
    print_step "Creating release notes..."
    
    local release_notes="$RELEASE_DIR/RELEASE-NOTES-$BUILD_TIMESTAMP.md"
    
    cat > "$release_notes" << EOF
# Forest OS Release Notes

**Build Date:** $(date)  
**Build Version:** $BUILD_TIMESTAMP  
**Script Version:** $SCRIPT_VERSION

## Build Configuration

### Supported Architectures
$(for arch in $ARCHITECTURES; do echo "- ${arch}-bit"; done)

### Boot Modes
$(for mode in $BOOT_MODES; do echo "- ${mode^^}"; done)

### Build Types
$(for type in $BUILD_TYPES; do echo "- ${type^}"; done)

## Package Contents

### Binary Packages
$(ls "$PACKAGE_DIR"/*.{iso,img}* 2>/dev/null | sed 's|.*/|- |' || echo "- None generated")

### Archive Packages
$(ls "$PACKAGE_DIR"/*.tar.gz 2>/dev/null | sed 's|.*/|- |' || echo "- None generated")

## Build Summary

- **Build Start:** $(date -d "@$BUILD_START_TIME")
- **Build Duration:** $(($(date +%s) - BUILD_START_TIME)) seconds
- **Parallel Jobs:** $PARALLEL_JOBS
- **Clean Build:** $CLEAN_BEFORE_BUILD

## Installation

### BIOS Systems
Flash any \`.iso\` file to a CD/DVD or USB drive using standard tools.

### UEFI Systems
Write any \`.img\` file to a USB drive:
\`\`\`bash
dd if=forestos_64bit_uefi_release_*.img of=/dev/sdX bs=1M
\`\`\`

## Known Issues

Please check the project repository for current known issues and workarounds.

## Support

For support and bug reports, visit the Forest OS repository.
EOF

    print_info "Created release notes: $(basename "$release_notes")"
}

# =============================================================================
# MAIN EXECUTION FLOW
# =============================================================================

show_banner() {
    print_header "FOREST OS DISTRIBUTION BUILD SYSTEM v$SCRIPT_VERSION"
    echo
    print_info "Build Timestamp: $BUILD_TIMESTAMP"
    print_info "Parallel Jobs: $PARALLEL_JOBS"
    print_info "Architectures: $ARCHITECTURES"
    print_info "Boot Modes: $BOOT_MODES"
    print_info "Build Types: $BUILD_TYPES"
    print_info "Distribution Formats: $DIST_FORMATS"
    echo
}

show_usage() {
    cat << EOF
Usage: $0 [OPTIONS]

OPTIONS:
  -h, --help              Show this help message
  -v, --verbose           Enable verbose output
  -c, --clean             Clean before building (default: true)
  -j, --jobs N            Set parallel jobs (default: $(nproc))
  -a, --arch LIST         Architectures to build (default: 32 64)
  -b, --boot LIST         Boot modes to build (default: bios uefi)
  -t, --type LIST         Build types (default: debug release)
  -f, --format LIST       Distribution formats (default: iso img tar)
  --no-test               Skip testing
  --no-checksums          Skip checksum generation
  --no-compress           Skip image compression

ENVIRONMENT VARIABLES:
  ARCHITECTURES           Space-separated list of architectures (32 64)
  BOOT_MODES             Space-separated list of boot modes (bios uefi)
  BUILD_TYPES            Space-separated list of build types (debug release optimize)
  DIST_FORMATS           Space-separated list of formats (iso img tar)
  PARALLEL_JOBS          Number of parallel build jobs
  VERBOSE                Enable verbose output (true/false)
  CLEAN_BEFORE_BUILD     Clean before building (true/false)
  GENERATE_CHECKSUMS     Generate checksum files (true/false)
  COMPRESS_IMAGES        Compress disk images (true/false)
  ENABLE_TESTING         Run build tests (true/false)

EXAMPLES:
  $0                                    # Build all configurations
  $0 -a "64" -b "uefi" -t "release"    # Build only 64-bit UEFI release
  $0 --verbose --no-test               # Verbose build without testing
  
EOF
}

parse_arguments() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            -v|--verbose)
                VERBOSE=true
                shift
                ;;
            -c|--clean)
                CLEAN_BEFORE_BUILD=true
                shift
                ;;
            -j|--jobs)
                PARALLEL_JOBS="$2"
                shift 2
                ;;
            -a|--arch)
                ARCHITECTURES="$2"
                shift 2
                ;;
            -b|--boot)
                BOOT_MODES="$2"
                shift 2
                ;;
            -t|--type)
                BUILD_TYPES="$2"
                shift 2
                ;;
            -f|--format)
                DIST_FORMATS="$2"
                shift 2
                ;;
            --no-test)
                ENABLE_TESTING=false
                shift
                ;;
            --no-checksums)
                GENERATE_CHECKSUMS=false
                shift
                ;;
            --no-compress)
                COMPRESS_IMAGES=false
                shift
                ;;
            *)
                print_error "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

main() {
    # Parse command line arguments
    parse_arguments "$@"
    
    # Show banner
    show_banner
    
    # Perform initial checks
    check_dependencies
    check_toolchain
    setup_directories
    
    # Clean previous builds if requested
    cleanup_previous_builds
    
    # Build all configurations
    build_all_configurations
    
    # Create distribution packages
    print_header "CREATING DISTRIBUTION PACKAGES"
    create_iso_packages
    create_img_packages
    create_source_package
    create_complete_package
    
    # Generate verification data
    generate_checksums
    
    # Test builds
    test_builds
    
    # Create release documentation
    create_release_notes
    
    # Final summary
    local build_duration=$(($(date +%s) - BUILD_START_TIME))
    print_header "BUILD COMPLETE"
    print_info "Total build time: ${build_duration} seconds"
    print_info "Distribution packages: $PACKAGE_DIR"
    print_info "Release notes: $RELEASE_DIR"
    print_info "Build logs: $LOGS_DIR"
    
    # Show package summary
    if [ -d "$PACKAGE_DIR" ] && [ "$(ls -A "$PACKAGE_DIR" 2>/dev/null)" ]; then
        print_info "Generated packages:"
        ls -la "$PACKAGE_DIR"/ | tail -n +2 | awk '{print "  " $9 " (" $5 " bytes)"}'
    fi
    
    print_color green "Forest OS distribution build completed successfully!"
}

# Execute main function with all arguments
main "$@"