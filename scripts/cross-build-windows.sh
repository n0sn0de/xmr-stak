#!/bin/bash
# Cross-compile n0s-ryo-miner for Windows x86_64 using MinGW-w64 on Linux
# Test with Wine: wine64 bin/n0s-ryo-miner.exe --version
#
# Usage: ./scripts/cross-build-windows.sh [--test] [--clean]
#
# Prerequisites (installed locally, no root needed):
#   MinGW-w64 cross-compiler (g++-mingw-w64-x86-64-posix)
#   Wine 9.0+ (for testing)
#   zlib1.dll in Wine prefix (for user32.dll dependency chain)
#
# Environment variables:
#   MINGW_SYSROOT  — path to extracted MinGW packages (default: /tmp/mingw-local/root)
#   WINE_PREFIX    — Wine prefix path (default: ~/.wine-n0s)
#   WINE_BIN_DIR   — path to wine64/wineserver binaries (default: /tmp/wine-local/usr/bin)
#
# The build produces a statically-linked Windows PE64 executable that
# requires only system OpenCL (vendor-provided) at runtime.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${REPO_DIR}/build-mingw"
OUTPUT_DIR="${REPO_DIR}/dist/windows-opencl"

DO_TEST=false
DO_CLEAN=false

for arg in "$@"; do
    case "$arg" in
        --test)  DO_TEST=true ;;
        --clean) DO_CLEAN=true ;;
        --help|-h)
            echo "Usage: $0 [--test] [--clean]"
            echo "  --test   Run Wine validation after build"
            echo "  --clean  Clean build directory before building"
            exit 0
            ;;
    esac
done

echo "====================================="
echo "Cross-Build: Windows x86_64 (MinGW)"
echo "====================================="

# Verify MinGW compiler is available
if ! command -v x86_64-w64-mingw32-g++ &>/dev/null; then
    if [ -f "${MINGW_SYSROOT:-/tmp/mingw-local/root}/usr/bin/x86_64-w64-mingw32-g++-posix" ]; then
        export PATH="${MINGW_SYSROOT:-/tmp/mingw-local/root}/usr/bin:$PATH"
    else
        echo "❌ MinGW-w64 cross-compiler not found!"
        echo "   Install: apt-get install g++-mingw-w64-x86-64-posix"
        echo "   Or extract with: apt-get download g++-mingw-w64-x86-64-posix && dpkg-deb -x *.deb /tmp/mingw-local/root"
        exit 1
    fi
fi

echo "Compiler: $(x86_64-w64-mingw32-g++ --version | head -1)"

if $DO_CLEAN; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure — OpenCL headers only (no runtime needed for cross-compile)
# Disable: CUDA, OpenSSL, microhttpd, hwloc (can be enabled when cross-compiled deps are available)
echo ""
echo "Configuring..."
cmake "$REPO_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="${REPO_DIR}/cmake/mingw-w64-x86_64.cmake" \
    -DCUDA_ENABLE=OFF \
    -DOpenCL_ENABLE=OFF \
    -DOpenSSL_ENABLE=OFF \
    -DMICROHTTPD_ENABLE=OFF \
    -DHWLOC_ENABLE=OFF \
    -DCMAKE_BUILD_TYPE=Release \
    -DN0S_COMPILE=generic \
    2>&1 | tail -5

# Build
echo ""
echo "Building..."
cmake --build . -j"$(nproc)" 2>&1 | tail -10

# Verify binary exists and is Windows PE
if [ ! -f "bin/n0s-ryo-miner.exe" ]; then
    echo "❌ Build failed — no executable produced!"
    exit 1
fi

echo ""
echo "=== Build Artifacts ==="
file bin/n0s-ryo-miner.exe
ls -lh bin/n0s-ryo-miner.exe

# Copy to dist
mkdir -p "$OUTPUT_DIR"
cp bin/n0s-ryo-miner.exe "$OUTPUT_DIR/"
echo ""
echo "✅ Windows cross-build successful!"
echo "Artifact: ${OUTPUT_DIR}/n0s-ryo-miner.exe"

# Optional Wine test
if $DO_TEST; then
    echo ""
    echo "====================================="
    echo "Wine Validation"
    echo "====================================="

    WINE_BIN_DIR="${WINE_BIN_DIR:-/tmp/wine-local/usr/bin}"
    WINE_PREFIX="${WINE_PREFIX:-$HOME/.wine-n0s}"
    WINE_LIB_DIR="/tmp/mingw-local/root/usr/lib/x86_64-linux-gnu"

    if [ ! -x "${WINE_BIN_DIR}/wine64" ]; then
        echo "⚠️  Wine not found at ${WINE_BIN_DIR}/wine64 — skipping test"
        exit 0
    fi

    export LD_LIBRARY_PATH="${WINE_LIB_DIR}:${LD_LIBRARY_PATH}"
    export PATH="${WINE_BIN_DIR}:$HOME/bin:${PATH}"
    export WINEPREFIX="$WINE_PREFIX"
    export WINEDEBUG=-all
    unset DISPLAY

    # Initialize Wine prefix if needed
    if [ ! -d "${WINEPREFIX}/drive_c/windows/system32" ]; then
        echo "Initializing Wine prefix at ${WINEPREFIX}..."
        # Run a simple exe to trigger prefix creation
        echo 'int main(){return 0;}' > /tmp/_wineinit.c
        x86_64-w64-mingw32-gcc -static /tmp/_wineinit.c -o /tmp/_wineinit.exe 2>/dev/null
        timeout 15 wine64 /tmp/_wineinit.exe 2>/dev/null || true
        rm -f /tmp/_wineinit.c /tmp/_wineinit.exe
    fi

    # Ensure zlib1.dll is in the prefix (needed for Wine's user32.dll dep chain)
    ZLIB_SRC="${MINGW_SYSROOT:-/tmp/mingw-local/root}/usr/x86_64-w64-mingw32/lib/zlib1.dll"
    if [ -f "$ZLIB_SRC" ] && [ -d "${WINEPREFIX}/drive_c/windows/system32" ]; then
        if [ ! -f "${WINEPREFIX}/drive_c/windows/system32/zlib1.dll" ]; then
            echo "Installing zlib1.dll into Wine prefix..."
            cp "$ZLIB_SRC" "${WINEPREFIX}/drive_c/windows/system32/"
        fi
    else
        echo "⚠️  zlib1.dll not found — Wine tests may fail"
    fi

    echo ""
    echo "Testing --version..."
    VERSION_OUT=$(timeout 10 wine64 bin/n0s-ryo-miner.exe --version 2>&1 || true)
    echo "$VERSION_OUT"

    if echo "$VERSION_OUT" | grep -q "n0s-ryo-miner"; then
        echo "✅ --version: PASS"
    else
        echo "❌ --version: FAIL"
    fi

    echo ""
    echo "Testing --help..."
    HELP_OUT=$(timeout 10 wine64 bin/n0s-ryo-miner.exe --help 2>&1 || true)
    HELP_LINES=$(echo "$HELP_OUT" | wc -l)

    if [ "$HELP_LINES" -gt 10 ]; then
        echo "✅ --help: PASS ($HELP_LINES lines)"
    else
        echo "❌ --help: FAIL (only $HELP_LINES lines)"
    fi

    echo ""
    echo "Testing --version-long..."
    VLONG_OUT=$(timeout 10 wine64 bin/n0s-ryo-miner.exe --version-long 2>&1 || true)
    echo "$VLONG_OUT"

    if echo "$VLONG_OUT" | grep -q "win"; then
        echo "✅ --version-long: PASS (Windows platform detected)"
    else
        echo "❌ --version-long: FAIL"
    fi
fi
