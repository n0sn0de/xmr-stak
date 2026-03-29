#!/bin/bash
# Test ALL GPU backends: AMD (local) + NVIDIA Pascal (nos2) + NVIDIA Turing (nosnode)
set -e

echo "======================================="
echo "Triple GPU Mining Test"
echo "AMD (nitro): RX 9070 XT — ROCm"
echo "NVIDIA (nos2): GTX 1070 Ti — CUDA 11.8"
echo "NVIDIA (nosnode): RTX 2070 — CUDA 12.6"
echo "======================================="
echo ""

PASS=0
FAIL=0

# Test AMD first
echo "Testing AMD GPU (local)..."
if ./test-mine.sh; then
    echo ""
    PASS=$((PASS+1))
else
    echo "❌ AMD MINING FAILED"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Testing NVIDIA Pascal (nos2, CUDA 11.8)..."
if REMOTE=nos2 ./test-mine-remote.sh; then
    echo ""
    PASS=$((PASS+1))
else
    echo "❌ NVIDIA nos2 MINING FAILED"
    FAIL=$((FAIL+1))
fi

echo ""
echo "Testing NVIDIA Turing (nosnode, CUDA 12.6)..."
if ./scripts/test-nosnode.sh; then
    echo ""
    PASS=$((PASS+1))
else
    echo "❌ NVIDIA nosnode MINING FAILED"
    FAIL=$((FAIL+1))
fi

echo ""
echo "======================================="
if [ $FAIL -eq 0 ]; then
    echo "✅ ALL $PASS PLATFORMS WORKING"
else
    echo "⚠️  $PASS passed, $FAIL failed"
fi
echo "======================================="

[ $FAIL -eq 0 ]
