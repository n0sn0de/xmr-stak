#!/bin/bash
# Remote NVIDIA mining test
set -e

REMOTE="${REMOTE:-nos2}"
POOL="${POOL:-192.168.50.186:3333}"
TIMEOUT="${TIMEOUT:-40}"
REPO_URL="https://github.com/n0sn0de/xmr-stak.git"

# Resolve remote home directory dynamically
# Use printf to avoid capturing ssh-agent noise
REMOTE_HOME=$(ssh "$REMOTE" 'printf "%s" "$HOME"' 2>/dev/null | tail -1)
if [ -z "$REMOTE_HOME" ]; then
    echo "❌ Could not resolve remote HOME for $REMOTE"
    exit 1
fi
REMOTE_DIR="${REMOTE_HOME}/xmr-stak-test"

echo "====================================="
echo "Remote NVIDIA Mining Test"
echo "Remote: $REMOTE"
echo "Remote dir: $REMOTE_DIR"
echo "Pool: $POOL"
echo "Timeout: ${TIMEOUT}s"
echo "====================================="

# Deploy code
echo "Deploying to $REMOTE..."
BRANCH="${BRANCH:-$(git rev-parse --abbrev-ref HEAD)}"
ssh "$REMOTE" "rm -rf '$REMOTE_DIR' && git clone -b '$BRANCH' '$REPO_URL' '$REMOTE_DIR' 2>&1 | tail -3"

# Build (NVIDIA only, no AMD)
echo "Building on remote..."
ssh "$REMOTE" "set -e && export PATH=/usr/local/cuda-11.8/bin:\$PATH && \
  export LD_LIBRARY_PATH=/usr/local/cuda-11.8/lib64:\$LD_LIBRARY_PATH && \
  cd '$REMOTE_DIR' && rm -rf build && mkdir build && cd build && \
  cmake .. -DCUDA_ENABLE=ON -DOpenCL_ENABLE=OFF -DMICROHTTPD_ENABLE=OFF \
    -DCUDA_ARCH='61' -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1 && \
  cmake --build . -j\$(nproc) 2>&1 | tail -5 && \
  test -f bin/n0s-ryo-miner"

if [ $? -ne 0 ]; then
    echo "❌ REMOTE BUILD FAILED"
    exit 1
fi
echo "✅ Remote build successful"

# Mine test
echo ""
echo "Mining for ${TIMEOUT} seconds on $REMOTE..."
OUTPUT=$(ssh "$REMOTE" "cd '$REMOTE_DIR' && timeout $TIMEOUT ./build/bin/n0s-ryo-miner --noAMD \
  -o $POOL -u WALLET -p x 2>&1" || true)

# Check for errors
if echo "$OUTPUT" | grep -q "CUDA.*ERROR\|Error.*cuda\|INVALID_DEVICE"; then
    echo "❌ CUDA ERRORS DETECTED:"
    echo "$OUTPUT" | grep "CUDA.*ERROR\|Error.*cuda\|INVALID_DEVICE" | head -10
    exit 1
fi

# Check for shares
SHARES=$(echo "$OUTPUT" | grep -c "Share accepted" || true)
if [ "$SHARES" -gt 0 ]; then
    echo "✅ NVIDIA MINING WORKS — $SHARES shares accepted"
    echo "$OUTPUT" | grep "HASHRATE\|Share accepted" | tail -10
    exit 0
else
    echo "⚠️  NO SHARES FOUND (may need longer timeout)"
    echo "$OUTPUT" | grep -E "Pool|connected|logged|GPU" | tail -10
    exit 1
fi
