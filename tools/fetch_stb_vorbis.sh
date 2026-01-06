#!/usr/bin/env bash
set -euo pipefail

# Download the public-domain stb_vorbis.c into src/.
# Usage: tools/fetch_stb_vorbis.sh [--force]

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="${REPO_ROOT}/src/stb_vorbis.c"
URL="https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c"

FORCE=0
if [ "${1-}" = "--force" ]; then
    FORCE=1
fi

if [ -f "$TARGET" ] && [ "$FORCE" -eq 0 ]; then
    echo "stb_vorbis.c already exists at $TARGET (use --force to re-download)"
    exit 0
fi

if command -v curl >/dev/null 2>&1; then
    echo "Fetching stb_vorbis.c with curl..."
    curl -fsSL "$URL" -o "$TARGET"
elif command -v wget >/dev/null 2>&1; then
    echo "Fetching stb_vorbis.c with wget..."
    wget -qO "$TARGET" "$URL"
else
    echo "Error: curl or wget required to download stb_vorbis.c" >&2
    exit 1
fi

echo "Saved stb_vorbis.c to $TARGET"
