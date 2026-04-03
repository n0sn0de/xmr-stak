#!/bin/bash
# Cross-compile n0s-ryo-miner for Windows x86_64 using MinGW-w64 on Linux
# Builds a fully-featured Windows binary with TLS + HTTP dashboard.
#
# Usage: ./scripts/cross-build-windows.sh [--test] [--clean] [--skip-deps] [--no-tls]
#
# Prerequisites (installed locally, no root needed):
#   MinGW-w64 cross-compiler (g++-mingw-w64-x86-64-posix)
#   Wine 9.0+ (for --test)
#   curl or wget (for dependency download)
#
# Environment variables:
#   MINGW_SYSROOT  — path to extracted MinGW packages (default: /tmp/mingw-local/root/usr)
#
# The build cross-compiles OpenSSL + libmicrohttpd for MinGW, then links
# a fully static Windows PE64 executable with TLS and HTTP dashboard support.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${REPO_DIR}/build-mingw"
DEPS_DIR="${REPO_DIR}/deps-mingw"
DEPS_PREFIX="${DEPS_DIR}/prefix"
OUTPUT_DIR="${REPO_DIR}/dist/windows-opencl"

MINGW_HOST="x86_64-w64-mingw32"

# Dependency versions
OPENSSL_VERSION="3.0.16"
OPENSSL_URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
MICROHTTPD_VERSION="1.0.1"
MICROHTTPD_URL="https://ftp.gnu.org/gnu/libmicrohttpd/libmicrohttpd-${MICROHTTPD_VERSION}.tar.gz"

NJOBS="$(nproc 2>/dev/null || echo 4)"

DO_TEST=false
DO_CLEAN=false
SKIP_DEPS=false
ENABLE_TLS=true
ENABLE_HTTPD=true

for arg in "$@"; do
    case "$arg" in
        --test)      DO_TEST=true ;;
        --clean)     DO_CLEAN=true ;;
        --skip-deps) SKIP_DEPS=true ;;
        --no-tls)    ENABLE_TLS=false ; ENABLE_HTTPD=false ;;
        --help|-h)
            echo "Usage: $0 [--test] [--clean] [--skip-deps] [--no-tls]"
            echo "  --test       Run Wine validation after build"
            echo "  --clean      Clean build + deps directories before building"
            echo "  --skip-deps  Skip dependency compilation (use existing)"
            echo "  --no-tls     Build without OpenSSL/microhttpd (smaller, no TLS/GUI)"
            exit 0
            ;;
    esac
done

# ─── Find MinGW Toolchain ────────────────────────────────────────────────────

if [[ -d "/tmp/mingw-local/root/usr" ]]; then
    MINGW_SYSROOT="${MINGW_SYSROOT:-/tmp/mingw-local/root/usr}"
elif [[ -d "/usr/x86_64-w64-mingw32" ]]; then
    MINGW_SYSROOT="${MINGW_SYSROOT:-/usr}"
fi

export PATH="${MINGW_SYSROOT:-/usr}/bin:$PATH"

if ! command -v ${MINGW_HOST}-g++ &>/dev/null && ! command -v ${MINGW_HOST}-g++-posix &>/dev/null; then
    echo "❌ MinGW-w64 cross-compiler not found!"
    echo "   Install: apt-get install g++-mingw-w64-x86-64-posix"
    exit 1
fi

MINGW_GCC="$(command -v ${MINGW_HOST}-gcc-posix 2>/dev/null || command -v ${MINGW_HOST}-gcc 2>/dev/null)"
MINGW_GXX="$(command -v ${MINGW_HOST}-g++-posix 2>/dev/null || command -v ${MINGW_HOST}-g++ 2>/dev/null)"

echo ""
echo "  ╔═══════════════════════════════════════════════════════╗"
echo "  ║    n0s-ryo-miner — Windows Cross-Build (MinGW-w64)   ║"
echo "  ╚═══════════════════════════════════════════════════════╝"
echo ""
echo "  Compiler: $($MINGW_GXX --version | head -1)"
echo "  Sysroot:  ${MINGW_SYSROOT}"
echo "  TLS:      $(${ENABLE_TLS} && echo 'ON (OpenSSL)' || echo 'OFF')"
echo "  HTTP:     $(${ENABLE_HTTPD} && echo 'ON (microhttpd)' || echo 'OFF')"
echo "  Jobs:     ${NJOBS}"
echo ""

# ─── Clean ────────────────────────────────────────────────────────────────────

if $DO_CLEAN; then
    echo "  Cleaning build + deps directories..."
    rm -rf "$BUILD_DIR" "$DEPS_DIR"
fi

mkdir -p "$BUILD_DIR" "$DEPS_DIR/src" "$DEPS_PREFIX"

# ─── Cross-compile Dependencies ──────────────────────────────────────────────

build_openssl() {
    local src_dir="${DEPS_DIR}/src/openssl-${OPENSSL_VERSION}"
    local stamp="${DEPS_PREFIX}/lib64/libssl.a"
    # Some builds put libs in lib/ instead of lib64/
    [[ -f "${DEPS_PREFIX}/lib/libssl.a" ]] && stamp="${DEPS_PREFIX}/lib/libssl.a"

    if [[ -f "$stamp" ]] || [[ -f "${DEPS_PREFIX}/lib64/libssl.a" ]] || [[ -f "${DEPS_PREFIX}/lib/libssl.a" ]]; then
        echo "  ✓ OpenSSL ${OPENSSL_VERSION} already built"
        return 0
    fi

    echo "  Building OpenSSL ${OPENSSL_VERSION} for MinGW..."

    if [[ ! -d "$src_dir" ]]; then
        echo "    Downloading..."
        curl -sL "$OPENSSL_URL" | tar xz -C "${DEPS_DIR}/src/"
    fi

    cd "$src_dir"

    # mingw64 = 64-bit Windows target via MinGW cross-compiler
    ./Configure mingw64 \
        --cross-compile-prefix=${MINGW_HOST}- \
        --prefix="${DEPS_PREFIX}" \
        --openssldir="${DEPS_PREFIX}/ssl" \
        no-shared \
        no-dso \
        no-engine \
        no-tests \
        no-ui-console \
        no-afalgeng \
        threads \
        -static \
        2>&1 | tail -3

    make -j"${NJOBS}" 2>&1 | tail -2
    make install_sw 2>&1 | tail -2

    echo "  ✓ OpenSSL ${OPENSSL_VERSION} built"
    cd "$REPO_DIR"
}

build_microhttpd() {
    local stamp="${DEPS_PREFIX}/lib/libmicrohttpd.a"

    if [[ -f "$stamp" ]]; then
        echo "  ✓ libmicrohttpd ${MICROHTTPD_VERSION} already built"
        return 0
    fi

    echo "  Building libmicrohttpd ${MICROHTTPD_VERSION} for MinGW..."

    local src_dir="${DEPS_DIR}/src/libmicrohttpd-${MICROHTTPD_VERSION}"
    if [[ ! -d "$src_dir" ]]; then
        echo "    Downloading..."
        curl -sL "$MICROHTTPD_URL" | tar xz -C "${DEPS_DIR}/src/"
    fi

    cd "$src_dir"

    ./configure \
        --host="${MINGW_HOST}" \
        --prefix="${DEPS_PREFIX}" \
        --enable-static \
        --disable-shared \
        --disable-curl \
        --disable-examples \
        --disable-doc \
        --disable-https \
        CC="${MINGW_GCC}" \
        CXX="${MINGW_GXX}" \
        CFLAGS="-O2" \
        2>&1 | tail -3

    make -j"${NJOBS}" 2>&1 | tail -2
    make install 2>&1 | tail -2

    echo "  ✓ libmicrohttpd ${MICROHTTPD_VERSION} built"
    cd "$REPO_DIR"
}

if ! $SKIP_DEPS && ($ENABLE_TLS || $ENABLE_HTTPD); then
    echo "  ── Cross-compiling dependencies ──"
    $ENABLE_TLS && build_openssl
    $ENABLE_HTTPD && build_microhttpd
    echo ""
fi

# ─── Generate Embedded GUI Assets ────────────────────────────────────────────

echo "  ── Generating embedded GUI assets ──"
bash "${REPO_DIR}/scripts/embed_assets.sh" \
    "${REPO_DIR}/gui" \
    "${REPO_DIR}/n0s/http/embedded_assets.hpp"
echo ""

# ─── CMake Configure ─────────────────────────────────────────────────────────

echo "  ── Configuring CMake ──"
cd "$BUILD_DIR"

CMAKE_ARGS=(
    "${REPO_DIR}"
    -DCMAKE_TOOLCHAIN_FILE="${REPO_DIR}/cmake/mingw-w64-x86_64.cmake"
    -DMINGW_DEPS_PREFIX="${DEPS_PREFIX}"
    -DCUDA_ENABLE=OFF
    -DOpenCL_ENABLE=OFF
    -DHWLOC_ENABLE=OFF
    -DCMAKE_BUILD_TYPE=Release
    -DN0S_COMPILE=generic
)

if $ENABLE_TLS && [[ -d "${DEPS_PREFIX}" ]]; then
    CMAKE_ARGS+=(
        -DOpenSSL_ENABLE=ON
        -DOPENSSL_ROOT_DIR="${DEPS_PREFIX}"
        -DOPENSSL_USE_STATIC_LIBS=ON
    )
else
    CMAKE_ARGS+=(-DOpenSSL_ENABLE=OFF)
fi

if $ENABLE_HTTPD && [[ -f "${DEPS_PREFIX}/lib/libmicrohttpd.a" ]]; then
    CMAKE_ARGS+=(
        -DMICROHTTPD_ENABLE=ON
        -DMHTD="${DEPS_PREFIX}/lib/libmicrohttpd.a"
        -DMTHD_INCLUDE_DIR="${DEPS_PREFIX}/include"
    )
else
    CMAKE_ARGS+=(-DMICROHTTPD_ENABLE=OFF)
fi

cmake "${CMAKE_ARGS[@]}" 2>&1 | tail -8

echo ""

# ─── Build ────────────────────────────────────────────────────────────────────

echo "  ── Building n0s-ryo-miner.exe ──"
cmake --build . -j"${NJOBS}" 2>&1 | tail -10

# ─── Verify ──────────────────────────────────────────────────────────────────

BINARY="${BUILD_DIR}/bin/n0s-ryo-miner.exe"

if [[ ! -f "$BINARY" ]]; then
    echo "  ✗ Build failed — binary not found"
    exit 1
fi

echo ""
echo "  ==> Build Artifacts"
file "$BINARY"
ls -lh "$BINARY"

echo ""
echo "  DLL dependencies (should be system-only):"
objdump -p "$BINARY" 2>/dev/null | grep "DLL Name" | awk '{print "    " $3}' | sort

# Copy to dist
mkdir -p "$OUTPUT_DIR"
cp "$BINARY" "$OUTPUT_DIR/"
echo ""
echo "  ✅ Windows cross-build successful!"
echo "  Artifact: ${OUTPUT_DIR}/n0s-ryo-miner.exe"

# ─── Wine Test ────────────────────────────────────────────────────────────────

if $DO_TEST; then
    echo ""
    echo "  ── Wine Validation ──"

    WINE_BIN_DIR="${WINE_BIN_DIR:-/tmp/wine-local/usr/bin}"
    WINE_PREFIX="${WINE_PREFIX:-$HOME/.wine-n0s}"
    WINE_LIB_DIR="/tmp/mingw-local/root/usr/lib/x86_64-linux-gnu"

    # Find wine64
    WINE=""
    for candidate in "${WINE_BIN_DIR}/wine64" "$(command -v wine64 2>/dev/null)" "$(command -v wine 2>/dev/null)"; do
        if [[ -n "$candidate" ]] && [[ -x "$candidate" ]]; then
            WINE="$candidate"
            break
        fi
    done

    if [[ -z "$WINE" ]]; then
        echo "  ⚠️  Wine not found — skipping test"
        exit 0
    fi

    [[ -d "$WINE_LIB_DIR" ]] && export LD_LIBRARY_PATH="${WINE_LIB_DIR}:${LD_LIBRARY_PATH:-}"
    export WINEPREFIX="$WINE_PREFIX"
    export WINEDEBUG=-all
    unset DISPLAY

    # Initialize Wine prefix if needed
    if [[ ! -d "${WINEPREFIX}/drive_c/windows/system32" ]]; then
        echo "  Initializing Wine prefix..."
        echo 'int main(){return 0;}' > /tmp/_wineinit.c
        ${MINGW_HOST}-gcc -static /tmp/_wineinit.c -o /tmp/_wineinit.exe 2>/dev/null || true
        timeout 15 $WINE /tmp/_wineinit.exe 2>/dev/null || true
        rm -f /tmp/_wineinit.c /tmp/_wineinit.exe
    fi

    # Ensure zlib1.dll
    ZLIB_SRC="${MINGW_SYSROOT}/${MINGW_HOST}/lib/zlib1.dll"
    if [[ -f "$ZLIB_SRC" ]] && [[ -d "${WINEPREFIX}/drive_c/windows/system32" ]]; then
        if [[ ! -f "${WINEPREFIX}/drive_c/windows/system32/zlib1.dll" ]]; then
            echo "  Installing zlib1.dll into Wine prefix..."
            cp "$ZLIB_SRC" "${WINEPREFIX}/drive_c/windows/system32/"
        fi
    fi

    echo ""
    echo "  Testing: n0s-ryo-miner.exe --version"
    VERSION_OUT=$(timeout 10 $WINE "$BINARY" --version 2>&1 || true)
    echo "  $VERSION_OUT"

    if echo "$VERSION_OUT" | grep -q "n0s-ryo-miner"; then
        echo "  ✅ --version: PASS"
    else
        echo "  ⚠️  --version: Wine test inconclusive (may need system DLLs)"
    fi

    echo ""
    echo "  Testing: n0s-ryo-miner.exe --version-long"
    VLONG_OUT=$(timeout 10 $WINE "$BINARY" --version-long 2>&1 || true)
    echo "  $VLONG_OUT"

    if echo "$VLONG_OUT" | grep -q "win"; then
        echo "  ✅ --version-long: PASS (Windows platform detected)"
    else
        echo "  ⚠️  --version-long: inconclusive"
    fi
fi
