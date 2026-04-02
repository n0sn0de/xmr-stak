#!/bin/bash
# Containerized CUDA build using podman
# Usage: ./scripts/container-build.sh [cuda_version] [arch_list]
#
# Examples:
#   ./scripts/container-build.sh 11.8 "61;75;86"     # CUDA 11.8, Pascal+Turing+Ampere
#   ./scripts/container-build.sh 12.6 "75;86;89;90"  # CUDA 12.6, Turing through Hopper
#   ./scripts/container-build.sh                      # defaults: CUDA 11.8, all supported

set -e

CUDA_VER="${1:-11.8}"
CUDA_ARCH="${2:-}"
UBUNTU_VER="${3:-22.04}"

# Map CUDA version to default arch list
if [ -z "$CUDA_ARCH" ]; then
    case "$CUDA_VER" in
        11.8*)  CUDA_ARCH="61;75;80;86;89" ;;
        12.[0-5]*) CUDA_ARCH="61;75;80;86;89" ;;
        12.[6-7]*) CUDA_ARCH="61;75;80;86;89;90" ;;
        12.[8-9]*|13.*) CUDA_ARCH="61;75;80;86;89;90;100;120" ;;
        *) echo "Unknown CUDA version: $CUDA_VER"; exit 1 ;;
    esac
fi

IMAGE="docker.io/nvidia/cuda:${CUDA_VER}.0-devel-ubuntu${UBUNTU_VER}"
CONTAINER_NAME="n0s-build-cuda${CUDA_VER}"
OUTPUT_DIR="dist/cuda-${CUDA_VER}"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "====================================="
echo "Container Build: CUDA ${CUDA_VER}"
echo "Image: ${IMAGE}"
echo "Arch: ${CUDA_ARCH}"
echo "Ubuntu: ${UBUNTU_VER}"
echo "====================================="

# Pull image if needed
if ! podman image exists "${IMAGE}" 2>/dev/null; then
    echo "Pulling ${IMAGE}..."
    podman pull "${IMAGE}"
fi

# Clean previous output
rm -rf "${REPO_DIR}/${OUTPUT_DIR}"
mkdir -p "${REPO_DIR}/${OUTPUT_DIR}"

# Build inside container
echo "Building..."
podman run --rm \
    --name "${CONTAINER_NAME}" \
    -v "${REPO_DIR}:/src:ro" \
    -v "${REPO_DIR}/${OUTPUT_DIR}:/out:rw" \
    "${IMAGE}" \
    bash -c "
        set -e
        # Install build deps
        apt-get update -qq && apt-get install -y -qq \
            cmake g++ libmicrohttpd-dev libssl-dev libhwloc-dev >/dev/null 2>&1

        # Copy source (read-only mount, build in /tmp)
        cp -r /src /tmp/build
        cd /tmp/build
        rm -rf build && mkdir build && cd build

        # Configure
        cmake .. \
            -DCUDA_ENABLE=ON \
            -DOpenCL_ENABLE=OFF \
            -DMICROHTTPD_ENABLE=ON \
            -DHWLOC_ENABLE=OFF \
            -DCUDA_ARCH='${CUDA_ARCH}' \
            -DCMAKE_BUILD_TYPE=Release \
            -DN0S_COMPILE=generic \
            2>&1 | tail -5

        # Build
        cmake --build . -j\$(nproc) 2>&1 | tail -10

        # Verify — single binary, no .so files needed
        test -f bin/n0s-ryo-miner
        # Confirm no shared backend libs were produced
        ! test -f bin/libn0s_cuda_backend.so || echo 'WARNING: unexpected .so found'
        ! test -f bin/libn0s_opencl_backend.so || echo 'WARNING: unexpected .so found'

        # Copy single binary artifact
        cp bin/n0s-ryo-miner /out/
        echo ''
        echo '=== Build artifact (single binary) ==='
        ls -lh /out/
        echo ''
        echo 'CUDA architectures compiled:'
        echo '${CUDA_ARCH}'
    "

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ CUDA ${CUDA_VER} build successful"
    echo "Artifacts in: ${OUTPUT_DIR}/"
    ls -lh "${REPO_DIR}/${OUTPUT_DIR}/"
else
    echo ""
    echo "❌ CUDA ${CUDA_VER} build FAILED"
    exit 1
fi
