#!/bin/bash
# Quick mining test script — verifies miner can actually mine
set -e

POOL="${POOL:-192.168.50.186:3333}"
TIMEOUT="${TIMEOUT:-30}"
BINARY="${BINARY:-./build/bin/n0s-ryo-miner}"

echo "====================================="
echo "Mining Test"
echo "Binary: $BINARY"
echo "Pool: $POOL"
echo "Timeout: ${TIMEOUT}s"
echo "====================================="

# Clean build
rm -rf build
mkdir build
cd build
cmake .. \
  -DCUDA_ENABLE=OFF \
  -DOpenCL_ENABLE=ON \
  -DCMAKE_BUILD_TYPE=Release \
  -DOpenCL_INCLUDE_DIR=/opt/rocm-7.2.0/include \
  -DOpenCL_LIBRARY=/usr/lib/x86_64-linux-gnu/libOpenCL.so \
  >/dev/null 2>&1

echo "Building..."
if ! cmake --build . -j$(nproc) 2>&1 | tail -5; then
    echo "❌ BUILD FAILED"
    exit 1
fi
echo "✅ Build successful"

cd ..

echo ""
echo "Mining for ${TIMEOUT} seconds..."
OUTPUT=$(timeout $TIMEOUT $BINARY --noAMDCache \
  -o $POOL \
  -u WALLET \
  -p x \
  --currency cryptonight_gpu 2>&1 || true)

# Check for errors
if echo "$OUTPUT" | grep -q "CL_INVALID\|Error.*clEnqueue\|Error.*clCreate"; then
    echo "❌ OPENCL ERRORS DETECTED:"
    echo "$OUTPUT" | grep "CL_INVALID\|Error.*clEnqueue\|Error.*clCreate" | head -10
    exit 1
fi

# Check for successful shares
SHARES=$(echo "$OUTPUT" | grep -c "Share accepted" || true)
if [ "$SHARES" -gt 0 ]; then
    echo "✅ MINING WORKS — $SHARES shares accepted"
    echo "$OUTPUT" | grep "HASHRATE\|Share accepted" | tail -10
    exit 0
else
    echo "⚠️  NO SHARES FOUND (may need longer timeout)"
    echo "$OUTPUT" | grep -E "Pool|connected|logged" | tail -5
    exit 1
fi
