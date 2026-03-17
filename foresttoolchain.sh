#!/bin/bash
# =============================================================================
# FOREST OS TOOLCHAIN BUILDER
# =============================================================================
# Builds the i686-forestos and (optionally) x86_64-forestos cross-compilers
# required to compile Forest OS.
#
# Sources used:
#   binutils 2.39  (already partially present in forestos-toolchain/src/)
#   gcc      12.2.0 (already partially present in forestos-toolchain/src/)
#
# The script will download the full tarballs if the configure scripts are
# missing, then configure, build, and install into:
#   forestos-toolchain/install/
#
# Usage:
#   ./foresttoolchain.sh [OPTIONS]
#
# Options:
#   --arch 32|64|both   Target architecture(s) to build (default: 32)
#   --jobs N            Parallel make jobs (default: nproc)
#   --skip-deps         Skip dependency check (use if you know they're installed)
#   --clean             Remove existing build dirs before building
#   --help              Show this help
# =============================================================================

# ---------------------------------------------------------------------------
# Ensure PATH includes standard system directories (must be before 'set')
# ---------------------------------------------------------------------------
export PATH="/usr/bin:/bin:/usr/local/bin:/usr/sbin:/sbin:${PATH}"

set -euo pipefail

# ---------------------------------------------------------------------------
# Colour helpers
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

info()    { echo -e "${BLUE}[INFO]${NC}    $*"; }
success() { echo -e "${GREEN}[OK]${NC}      $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}    $*"; }
error()   { echo -e "${RED}[ERROR]${NC}   $*" >&2; }
step()    { echo -e "${CYAN}[STEP]${NC}    $*"; }

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TOOLCHAIN_DIR="${SCRIPT_DIR}/forestos-toolchain"
SRC_DIR="${TOOLCHAIN_DIR}/src"
INSTALL_DIR="${TOOLCHAIN_DIR}/install"
SYSROOT_DIR="${TOOLCHAIN_DIR}/sysroot"
BUILD_BINUTILS_DIR="${TOOLCHAIN_DIR}/build-binutils"
BUILD_GCC_DIR="${TOOLCHAIN_DIR}/build-gcc"

# ---------------------------------------------------------------------------
# Versions & download URLs
# ---------------------------------------------------------------------------
# Use binutils 2.43 for GCC 15 compatibility (2.39 has issues with gprofng)
BINUTILS_VERSION="2.43"
GCC_VERSION="12.2.0"

BINUTILS_TARBALL="binutils-${BINUTILS_VERSION}.tar.xz"
GCC_TARBALL="gcc-${GCC_VERSION}.tar.xz"

BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/${BINUTILS_TARBALL}"
GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${GCC_VERSION}/${GCC_TARBALL}"

BINUTILS_SRC="${SRC_DIR}/binutils-${BINUTILS_VERSION}"
GCC_SRC="${SRC_DIR}/gcc-${GCC_VERSION}"

# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------
BUILD_ARCH="32"
MAKE_JOBS="$(nproc 2>/dev/null || echo 4)"
SKIP_DEPS=false
DO_CLEAN=false

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --arch)
            BUILD_ARCH="$2"; shift 2 ;;
        --jobs)
            MAKE_JOBS="$2"; shift 2 ;;
        --skip-deps)
            SKIP_DEPS=true; shift ;;
        --clean)
            DO_CLEAN=true; shift ;;
        --help|-h)
            sed -n '2,40p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *)
            error "Unknown option: $1"
            echo "Use --help for usage."
            exit 1 ;;
    esac
done

# Validate arch
case "$BUILD_ARCH" in
    32|64|both) ;;
    *)
        error "Invalid --arch value '$BUILD_ARCH'. Use 32, 64, or both."
        exit 1 ;;
esac

# ---------------------------------------------------------------------------
# Dependency check
# ---------------------------------------------------------------------------
check_dependencies() {
    step "Checking build dependencies..."

    local missing=()
    local tools=("gcc" "g++" "make" "flex" "bison" "gawk" "makeinfo" "curl" "tar" "xz")

    for tool in "${tools[@]}"; do
        if ! command -v "$tool" &>/dev/null; then
            missing+=("$tool")
        fi
    done

    # Check for required libraries (header presence is a good proxy)
    # Check both default and multiarch locations for gmp.h
    local lib_headers=(
        "/usr/include/gmp.h"
        "/usr/include/x86_64-linux-gnu/gmp.h"
        "/usr/include/mpfr.h"
        "/usr/include/mpc.h"
    )
    local lib_pkgs=("libgmp-dev" "libgmp-dev" "libmpfr-dev" "libmpc-dev")
    local found_gmp=false
    for i in "${!lib_headers[@]}"; do
        if [[ -f "${lib_headers[$i]}" ]]; then
            # Found gmp.h in one of the locations, mark as found
            if [[ "$i" -eq 0 || "$i" -eq 1 ]]; then
                found_gmp=true
            fi
        fi
    done
    if [[ "$found_gmp" == false ]]; then
        missing+=("libgmp-dev")
    fi

    if [[ ${#missing[@]} -gt 0 ]]; then
        error "Missing dependencies: ${missing[*]}"
        echo ""
        echo "Install them with:"
        echo "  sudo apt update && sudo apt install -y \\"
        echo "    build-essential flex bison gawk texinfo curl xz-utils \\"
        echo "    libgmp-dev libmpfr-dev libmpc-dev libisl-dev zlib1g-dev"
        exit 1
    fi

    success "All dependencies satisfied."
}

# ---------------------------------------------------------------------------
# Download helper
# ---------------------------------------------------------------------------
download_source() {
    local url="$1"
    local tarball="$2"
    local src_dir="$3"
    local name="$4"

    # Check if configure already exists (full source present)
    if [[ -f "${src_dir}/configure" ]]; then
        success "${name} source already present (configure found)."
        return 0
    fi

    mkdir -p "$SRC_DIR"

    local tarball_path="${SRC_DIR}/${tarball}"

    if [[ ! -f "$tarball_path" ]]; then
        step "Downloading ${name} from ${url}..."
        if command -v curl &>/dev/null; then
            curl -L --progress-bar -o "$tarball_path" "$url"
        elif command -v wget &>/dev/null; then
            wget -q --show-progress -O "$tarball_path" "$url"
        else
            error "Neither curl nor wget found. Cannot download ${name}."
            exit 1
        fi
        success "Downloaded ${tarball}."
    else
        info "${tarball} already downloaded."
    fi

    step "Extracting ${tarball}..."
    # Remove incomplete/partial source tree before extracting
    rm -rf "$src_dir"
    tar -xf "$tarball_path" -C "$SRC_DIR"
    success "${name} extracted to ${src_dir}."
}

# ---------------------------------------------------------------------------
# Setup sysroot
# ---------------------------------------------------------------------------
setup_sysroot() {
    step "Setting up sysroot at ${SYSROOT_DIR}..."

    mkdir -p "${SYSROOT_DIR}/usr/include"
    mkdir -p "${SYSROOT_DIR}/usr/lib"
    mkdir -p "${SYSROOT_DIR}/lib"
    mkdir -p "${SYSROOT_DIR}/usr/bin"

    # Copy Forest OS kernel headers into sysroot
    if [[ -d "${SCRIPT_DIR}/src/include" ]]; then
        cp -r "${SCRIPT_DIR}/src/include/." "${SYSROOT_DIR}/usr/include/"
        success "Kernel headers copied to sysroot."
    else
        warn "src/include not found — sysroot will have no kernel headers."
    fi

    success "Sysroot ready."
}

# ---------------------------------------------------------------------------
# Build binutils for a given target
# ---------------------------------------------------------------------------
build_binutils() {
    local target="$1"
    local build_dir="${BUILD_BINUTILS_DIR}/${target}"

    step "Building binutils ${BINUTILS_VERSION} for target=${target}..."

    if [[ "$DO_CLEAN" == true ]]; then
        rm -rf "$build_dir"
    fi

    mkdir -p "$build_dir"
    cd "$build_dir"

    if [[ ! -f "Makefile" ]]; then
        info "Configuring binutils for ${target}..."
        bash -c " \
            export PATH='/usr/bin:/bin:/usr/local/bin:\$PATH'; \
            export CONFIG_SHELL='/bin/bash'; \
            export SED='/usr/bin/sed'; \
            export SORT='/usr/bin/sort'; \
            export GREP='/usr/bin/grep'; \
            ${BINUTILS_SRC}/configure \
                --target='${target}' \
                --prefix='${INSTALL_DIR}' \
                --with-sysroot='${SYSROOT_DIR}' \
                --disable-nls \
                --disable-werror \
                --disable-multilib \
                --enable-64-bit-bfd" \
            2>&1 | tail -5
    else
        info "binutils already configured for ${target}, skipping configure."
    fi

    info "Building binutils (${MAKE_JOBS} jobs)..."
    bash -c "export PATH='/usr/bin:/bin:/usr/local/bin:\$PATH'; make -j'${MAKE_JOBS}'" 2>&1 | tail -3
    bash -c "export PATH='/usr/bin:/bin:/usr/local/bin:\$PATH'; make install" 2>&1 | tail -3

    success "binutils for ${target} installed."
}

# ---------------------------------------------------------------------------
# Build GCC (C and C++ only, no libstdc++) for a given target
# ---------------------------------------------------------------------------
build_gcc() {
    local target="$1"
    local arch_flags="$2"
    local build_dir="${BUILD_GCC_DIR}/${target}"

    step "Building GCC ${GCC_VERSION} for target=${target}..."

    if [[ "$DO_CLEAN" == true ]]; then
        rm -rf "$build_dir"
    fi

    mkdir -p "$build_dir"
    cd "$build_dir"

    # Ensure the newly built binutils are on PATH so GCC configure finds them
    export PATH="${INSTALL_DIR}/bin:${PATH}"

    if [[ ! -f "Makefile" ]]; then
        info "Configuring GCC for ${target}..."
        "${GCC_SRC}/configure" \
            --target="${target}" \
            --prefix="${INSTALL_DIR}" \
            --with-sysroot="${SYSROOT_DIR}" \
            --disable-nls \
            --enable-languages=c,c++ \
            --without-headers \
            --disable-libssp \
            --disable-libgomp \
            --disable-libquadmath \
            --disable-threads \
            --disable-libatomic \
            --disable-libstdcxx-pch \
            --disable-libstdcxx \
            --with-newlib \
            --disable-multilib \
            ${arch_flags} \
            2>&1 | tail -5
    else
        info "GCC already configured for ${target}, skipping configure."
    fi

    info "Building GCC (${MAKE_JOBS} jobs) — this may take several minutes..."
    make -j"${MAKE_JOBS}" all-gcc 2>&1 | tail -3
    make -j"${MAKE_JOBS}" all-target-libgcc 2>&1 | tail -3
    make install-gcc 2>&1 | tail -3
    make install-target-libgcc 2>&1 | tail -3

    success "GCC for ${target} installed."
}

# ---------------------------------------------------------------------------
# Verify a built toolchain
# ---------------------------------------------------------------------------
verify_toolchain() {
    local target="$1"
    local gcc_bin="${INSTALL_DIR}/bin/${target}-gcc"
    local ld_bin="${INSTALL_DIR}/bin/${target}-ld"

    step "Verifying toolchain for ${target}..."

    if [[ ! -x "$gcc_bin" ]]; then
        error "GCC not found at ${gcc_bin}"
        return 1
    fi
    if [[ ! -x "$ld_bin" ]]; then
        error "LD not found at ${ld_bin}"
        return 1
    fi

    # Quick compile test
    local test_src
    test_src="$(mktemp /tmp/forestos_tc_test_XXXXXX.c)"
    echo 'void _start(void) { __asm__("hlt"); }' > "$test_src"

    local test_out="/tmp/forestos_tc_test_out"
    if "$gcc_bin" -ffreestanding -nostdlib -o "$test_out" "$test_src" 2>/dev/null; then
        success "Compile test passed for ${target}."
        rm -f "$test_src" "$test_out"
    else
        warn "Compile test failed for ${target} — toolchain may still be usable."
        rm -f "$test_src"
    fi

    info "  GCC:  $("$gcc_bin" --version | head -1)"
    info "  LD:   $("$ld_bin" --version | head -1)"
}

# ---------------------------------------------------------------------------
# Build one complete toolchain (binutils + gcc) for a target
# ---------------------------------------------------------------------------
build_toolchain_for() {
    local target="$1"
    local arch_flags="$2"

    echo ""
    echo "============================================================"
    echo "  Building toolchain for: ${target}"
    echo "============================================================"

    build_binutils "$target"
    build_gcc      "$target" "$arch_flags"
    verify_toolchain "$target"
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
main() {
    echo ""
    echo -e "${CYAN}============================================================${NC}"
    echo -e "${CYAN}  Forest OS Toolchain Builder${NC}"
    echo -e "${CYAN}============================================================${NC}"
    echo ""
    info "Toolchain directory : ${TOOLCHAIN_DIR}"
    info "Install prefix      : ${INSTALL_DIR}"
    info "Sysroot             : ${SYSROOT_DIR}"
    info "Target arch(s)      : ${BUILD_ARCH}"
    info "Parallel jobs       : ${MAKE_JOBS}"
    echo ""

    # 1. Dependency check
    if [[ "$SKIP_DEPS" == false ]]; then
        check_dependencies
    fi

    # 2. Create install dir
    mkdir -p "${INSTALL_DIR}/bin"

    # 3. Download / verify sources
    download_source "$BINUTILS_URL" "$BINUTILS_TARBALL" "$BINUTILS_SRC" "binutils-${BINUTILS_VERSION}"
    download_source "$GCC_URL"      "$GCC_TARBALL"      "$GCC_SRC"      "gcc-${GCC_VERSION}"

    # 4. Setup sysroot
    setup_sysroot

    # 5. Build requested toolchain(s)
    case "$BUILD_ARCH" in
        32)
            build_toolchain_for "i686-linux-gnu" "--with-arch=i686 --with-tune=generic"
            ;;
        64)
            build_toolchain_for "x86_64-linux-gnu" "--with-arch=x86-64 --with-tune=generic"
            ;;
        both)
            build_toolchain_for "i686-linux-gnu"   "--with-arch=i686 --with-tune=generic"
            build_toolchain_for "x86_64-linux-gnu" "--with-arch=x86-64 --with-tune=generic"
            ;;
    esac

    # 6. Summary
    echo ""
    echo -e "${GREEN}============================================================${NC}"
    echo -e "${GREEN}  Forest OS Toolchain Build Complete!${NC}"
    echo -e "${GREEN}============================================================${NC}"
    echo ""
    success "Toolchain binaries installed to: ${INSTALL_DIR}/bin/"
    echo ""
    info "Available cross-compilers:"
    ls "${INSTALL_DIR}/bin/"*-gcc 2>/dev/null | while read -r f; do
        echo "    $f"
    done
    echo ""
    info "To build Forest OS now, run:"
    echo "    make ARCH=32 BOOT_MODE=bios BUILD_TYPE=debug build"
    echo "    make ARCH=32 BOOT_MODE=bios BUILD_TYPE=debug iso"
    echo ""
    info "Or use the run script:"
    echo "    ./run.sh --bios"
    echo ""
}

main "$@"
