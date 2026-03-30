#!/bin/bash
# Containerized OpenCL build using podman
# Usage: ./scripts/container-build-opencl.sh [ubuntu_version]
#
# Examples:
#   ./scripts/container-build-opencl.sh 22.04     # Ubuntu 22.04
#   ./scripts/container-build-opencl.sh 24.04     # Ubuntu 24.04
#   ./scripts/container-build-opencl.sh           # default: 22.04

set -e

UBUNTU_VER="${1:-22.04}"
IMAGE="docker.io/library/ubuntu:${UBUNTU_VER}"
CONTAINER_NAME="n0s-build-opencl-${UBUNTU_VER}"
OUTPUT_DIR="dist/opencl-ubuntu${UBUNTU_VER}"
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"

echo "====================================="
echo "Container Build: OpenCL (AMD)"
echo "Image: ${IMAGE}"
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
        export DEBIAN_FRONTEND=noninteractive
        apt-get update -qq && apt-get install -y -qq \
            cmake g++ \
            opencl-headers ocl-icd-opencl-dev \
            libmicrohttpd-dev libssl-dev \
            >/dev/null 2>&1

        # Copy source (read-only mount, build in /tmp)
        cp -r /src /tmp/build
        cd /tmp/build
        rm -rf build && mkdir build && cd build

        # Configure
        cmake .. \
            -DCUDA_ENABLE=OFF \
            -DOpenCL_ENABLE=ON \
            -DMICROHTTPD_ENABLE=ON \
            -DHWLOC_ENABLE=OFF \
            -DCMAKE_BUILD_TYPE=Release \
            -DN0S_COMPILE=generic \
            2>&1 | tail -5

        # Build
        cmake --build . -j\$(nproc) 2>&1 | tail -10

        # Verify
        test -f bin/n0s-ryo-miner && test -f bin/libn0s_opencl_backend.so

        # Copy artifacts
        cp bin/n0s-ryo-miner /out/
        cp bin/libn0s_opencl_backend.so /out/
        echo ''
        echo '=== Build artifacts ==='
        ls -lh /out/
    "

if [ $? -eq 0 ]; then
    echo ""
    echo "✅ OpenCL (Ubuntu ${UBUNTU_VER}) build successful"
    echo "Artifacts in: ${OUTPUT_DIR}/"
    ls -lh "${REPO_DIR}/${OUTPUT_DIR}/"
else
    echo ""
    echo "❌ OpenCL build FAILED"
    exit 1
fi
