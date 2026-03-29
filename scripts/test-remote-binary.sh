#!/bin/bash
# Test a pre-built binary on remote NVIDIA hardware
# Usage: ./scripts/test-remote-binary.sh <remote_host> <binary_dir> [timeout]
#
# Examples:
#   ./scripts/test-remote-binary.sh nos2 dist/cuda-11.8 45
#   ./scripts/test-remote-binary.sh nosnode dist/cuda-12.6 50

set -e

REMOTE="${1:?Usage: $0 <remote_host> <binary_dir> [timeout]}"
BINARY_DIR="${2:?Usage: $0 <remote_host> <binary_dir> [timeout]}"
TIMEOUT="${3:-45}"
POOL="${POOL:-192.168.50.186:3333}"
REMOTE_DIR="/tmp/n0s-mine-test"

echo "====================================="
echo "Remote Binary Mine Test"
echo "Remote: ${REMOTE}"
echo "Binary: ${BINARY_DIR}"
echo "Pool: ${POOL}"
echo "Timeout: ${TIMEOUT}s"
echo "====================================="

# Verify binary exists locally
if [ ! -f "${BINARY_DIR}/n0s-ryo-miner" ] || [ ! -f "${BINARY_DIR}/libxmrstak_cuda_backend.so" ]; then
    echo "❌ Binary not found in ${BINARY_DIR}/"
    ls -la "${BINARY_DIR}/" 2>/dev/null
    exit 1
fi

# Deploy binary to remote (using pipe to avoid scp/rsync shell output issues)
echo "Deploying binary to ${REMOTE}..."
ssh "${REMOTE}" "rm -rf ${REMOTE_DIR} && mkdir -p ${REMOTE_DIR}"
cat "${BINARY_DIR}/n0s-ryo-miner" | ssh "${REMOTE}" "cat > ${REMOTE_DIR}/n0s-ryo-miner && chmod +x ${REMOTE_DIR}/n0s-ryo-miner"
cat "${BINARY_DIR}/libxmrstak_cuda_backend.so" | ssh "${REMOTE}" "cat > ${REMOTE_DIR}/libxmrstak_cuda_backend.so"
echo "Deployed $(du -sh "${BINARY_DIR}" | cut -f1) to ${REMOTE}:${REMOTE_DIR}/"

# Mine test
echo "Mining for ${TIMEOUT} seconds on ${REMOTE}..."
OUTPUT=$(ssh "${REMOTE}" "cd ${REMOTE_DIR} && timeout ${TIMEOUT} ./n0s-ryo-miner --noAMD \
    -o ${POOL} -u WALLET -p x 2>&1" || true)

# Check for CUDA errors
if echo "$OUTPUT" | grep -q "CUDA.*ERROR\|Error.*cuda\|INVALID_DEVICE"; then
    echo "❌ CUDA ERRORS DETECTED:"
    echo "$OUTPUT" | grep -iE "error|warning|cuda|failed" | head -10
    exit 1
fi

# Check for shares
SHARES=$(echo "$OUTPUT" | grep -c "Share accepted" || true)
if [ "$SHARES" -gt 0 ]; then
    echo "✅ MINING WORKS — ${SHARES} shares accepted on ${REMOTE}"
    echo "$OUTPUT" | grep -E "CUDA \[|architecture|NOTE" | head -3
    exit 0
else
    echo "⚠️  NO SHARES (may need longer timeout or driver mismatch)"
    echo "$OUTPUT" | grep -iE "error|warning|cuda|pool|GPU|failed|version" | head -15
    exit 1
fi
