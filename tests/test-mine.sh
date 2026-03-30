#!/bin/bash
# Quick live mining test — runs for 45 seconds, checks for accepted shares
set -e

BINARY="$(dirname "$0")/../build/bin/n0s-ryo-miner"
POOL="192.168.50.186:3333"
DURATION=${1:-45}

if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found at $BINARY"
    exit 1
fi

echo "=== test-mine.sh: Mining for ${DURATION}s against $POOL ==="

# Run miner in background, capture output
TMPLOG=$(mktemp)
timeout "$DURATION" "$BINARY" 2>&1 | tee "$TMPLOG" &
PID=$!

# Wait for it to finish (timeout will kill it)
wait $PID 2>/dev/null || true

# Check results
SHARES=$(grep -c "Result accepted" "$TMPLOG" 2>/dev/null || echo 0)
REJECTS=$(grep -c "Result rejected" "$TMPLOG" 2>/dev/null || echo 0)
ERRORS=$(grep -ci "error\|fatal\|segfault\|signal" "$TMPLOG" 2>/dev/null || echo 0)

echo ""
echo "=== RESULTS ==="
echo "Shares accepted: $SHARES"
echo "Shares rejected: $REJECTS"
echo "Errors: $ERRORS"

rm -f "$TMPLOG"

if [ "$REJECTS" -gt 0 ]; then
    echo "FAIL: Got $REJECTS rejected shares!"
    exit 1
fi

if [ "$SHARES" -eq 0 ]; then
    echo "WARN: No shares found in ${DURATION}s (might need longer run)"
    exit 0
fi

echo "PASS: $SHARES shares, 0 rejections"
exit 0
