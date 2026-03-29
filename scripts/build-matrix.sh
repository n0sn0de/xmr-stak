#!/bin/bash
# Build the full CUDA version matrix using containers
# Then optionally test on available hardware
#
# Usage: ./scripts/build-matrix.sh [--test]

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
DO_TEST="${1:-}"

echo "============================================="
echo "n0s-ryo-miner Build Matrix"
echo "============================================="
echo ""

# Build Matrix:
# CUDA 11.8 on Ubuntu 22.04 — widest compat (Pascal through Ada)
# CUDA 12.6 on Ubuntu 22.04 — adds Hopper support
# CUDA 12.8 on Ubuntu 22.04 — adds Blackwell support

BUILDS=(
    "11.8|61;75;80;86;89|22.04|Pascal through Ada"
    "12.6|61;75;80;86;89;90|22.04|Pascal through Hopper"
    "12.8|61;75;80;86;89;90;100;120|22.04|Pascal through Blackwell"
)

PASS=0
FAIL=0

for BUILD in "${BUILDS[@]}"; do
    IFS='|' read -r CUDA_VER ARCHS UBUNTU_VER DESCRIPTION <<< "$BUILD"
    echo "─────────────────────────────────────────────"
    echo "Building: CUDA ${CUDA_VER} — ${DESCRIPTION}"
    echo "─────────────────────────────────────────────"

    if "${SCRIPT_DIR}/container-build.sh" "${CUDA_VER}" "${ARCHS}" "${UBUNTU_VER}"; then
        PASS=$((PASS + 1))
        echo ""
    else
        FAIL=$((FAIL + 1))
        echo "❌ CUDA ${CUDA_VER} build failed!"
        echo ""
    fi
done

echo ""
echo "============================================="
echo "Build Matrix Results: ${PASS} passed, ${FAIL} failed"
echo "============================================="

# Optional: test on hardware
if [ "$DO_TEST" = "--test" ]; then
    echo ""
    echo "============================================="
    echo "Testing on available hardware"
    echo "============================================="

    # CUDA 11.8 build → test on nos2 (Pascal, driver supports 13.0)
    echo ""
    echo "Testing CUDA 11.8 build on nos2 (GTX 1070 Ti, Pascal)..."
    "${SCRIPT_DIR}/test-remote-binary.sh" nos2 "${REPO_DIR}/dist/cuda-11.8" 45 || true

    # CUDA 12.6 build → test on nosnode (Turing, driver supports 12.2, forward compat)
    echo ""
    echo "Testing CUDA 12.6 build on nosnode (RTX 2070, Turing)..."
    "${SCRIPT_DIR}/test-remote-binary.sh" nosnode "${REPO_DIR}/dist/cuda-12.6" 50 || true
fi

[ $FAIL -eq 0 ]
