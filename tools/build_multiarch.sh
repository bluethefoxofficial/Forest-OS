#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage: build_multiarch.sh [--arch x86|x86_64|both]

Build Forest OS ISO images for one or both architectures.
Results are written to ./dist/ as unique ISO files.
EOF
}

ARCH_SELECTION="both"

while [[ $# -gt 0 ]]; do
    case "$1" in
        -a|--arch)
            shift || { usage; exit 1; }
            ARCH_SELECTION="$1"
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
    shift || true
done

case "$ARCH_SELECTION" in
    x86)      BUILD_TARGETS=("x86") ;;
    x86_64)   BUILD_TARGETS=("x86_64") ;;
    both)     BUILD_TARGETS=("x86" "x86_64") ;;
    *) echo "Invalid architecture selection: ${ARCH_SELECTION}" >&2; usage; exit 1 ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DIST_DIR="${REPO_ROOT}/dist"
mkdir -p "${DIST_DIR}"

run_build() {
    local friendly_arch="$1"
    local make_arch
    case "$friendly_arch" in
        x86) make_arch="i386" ;;
        x86_64) make_arch="x86_64" ;;
        *) echo "Unsupported architecture: $friendly_arch" >&2; return 1 ;;
    esac

    local timestamp
    timestamp="$(date +%Y%m%d_%H%M%S)"
    local iso_name="forest_${friendly_arch}_${timestamp}.iso"

    echo "[BUILD] Starting ${friendly_arch} build (ISO: ${iso_name})"
    (cd "${REPO_ROOT}" && ARCH="${make_arch}" ISO_NAME="${iso_name}" make clean)
    (cd "${REPO_ROOT}" && ARCH="${make_arch}" ISO_NAME="${iso_name}" make iso)

    local iso_path="${REPO_ROOT}/${iso_name}"
    if [[ ! -f "${iso_path}" ]]; then
        echo "[ERROR] Expected ISO not found at ${iso_path}" >&2
        exit 1
    fi

    local final_iso="${DIST_DIR}/forestos-${friendly_arch}-${timestamp}.iso"
    mv "${iso_path}" "${final_iso}"
    echo "[BUILD] Completed ${friendly_arch} build -> ${final_iso}"
}

for arch in "${BUILD_TARGETS[@]}"; do
    run_build "${arch}"
done

echo "[BUILD] All requested builds complete. Artifacts in ${DIST_DIR}"
