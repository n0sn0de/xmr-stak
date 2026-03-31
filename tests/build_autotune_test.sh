#!/bin/bash
# Build and run autotune framework unit tests
set -e

cd "$(dirname "$0")/.."

echo "Building autotune tests..."
g++ -std=c++17 -Wall -Wextra -I. \
    tests/test_autotune.cpp \
    n0s/autotune/autotune_persist.cpp \
    -o /tmp/test_autotune

echo "Running autotune tests..."
/tmp/test_autotune

echo ""
echo "All autotune tests passed ✅"
