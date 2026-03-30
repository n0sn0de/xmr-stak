#!/bin/bash
# Build the CN-GPU validation harness
set -e
cd "$(dirname "$0")/.."

echo "Compiling C++ sources..."
g++ -std=c++17 -O2 -march=native -msse2 -maes -mavx2 \
    -I. -c n0s/backend/cpu/crypto/keccak.cpp -o tests/keccak.o

g++ -std=c++17 -O2 -march=native -msse2 -maes -mavx2 \
    -I. -c tests/cn_gpu_harness.cpp -o tests/cn_gpu_harness.o

g++ -std=c++17 -O2 -march=native -msse2 -maes -mavx2 \
    -I. -c n0s/backend/cpu/crypto/cn_gpu_avx.cpp -o tests/cn_gpu_avx.o

g++ -std=c++17 -O2 -march=native -msse2 -maes \
    -I. -c n0s/backend/cpu/crypto/cn_gpu_ssse3.cpp -o tests/cn_gpu_ssse3.o

echo "Linking..."
g++ -O2 -march=native \
    tests/cn_gpu_harness.o \
    tests/cn_gpu_avx.o \
    tests/cn_gpu_ssse3.o \
    tests/keccak.o \
    -o tests/cn_gpu_harness -lpthread

echo "✅ Built tests/cn_gpu_harness"
