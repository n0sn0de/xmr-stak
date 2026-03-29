#!/bin/bash
# Build the CN-GPU validation harness
set -e
cd "$(dirname "$0")/.."

echo "Compiling C crypto sources..."
# Compile each C file to .o
for f in c_blake256 c_groestl c_jh c_keccak c_skein; do
    gcc -c -O2 -march=native -msse2 -I. \
        xmrstak/backend/cpu/crypto/${f}.c \
        -o tests/${f}.o
done

echo "Compiling C++ sources..."
g++ -std=c++17 -O2 -march=native -msse2 -maes -mavx2 \
    -I. -c tests/cn_gpu_harness.cpp -o tests/cn_gpu_harness.o

g++ -std=c++17 -O2 -march=native -msse2 -maes -mavx2 \
    -I. -c xmrstak/backend/cpu/crypto/cn_gpu_avx.cpp -o tests/cn_gpu_avx.o

g++ -std=c++17 -O2 -march=native -msse2 -maes \
    -I. -c xmrstak/backend/cpu/crypto/cn_gpu_ssse3.cpp -o tests/cn_gpu_ssse3.o

echo "Linking..."
g++ -O2 -march=native \
    tests/cn_gpu_harness.o \
    tests/cn_gpu_avx.o \
    tests/cn_gpu_ssse3.o \
    tests/c_blake256.o \
    tests/c_groestl.o \
    tests/c_jh.o \
    tests/c_keccak.o \
    tests/c_skein.o \
    -o tests/cn_gpu_harness -lpthread

echo "✅ Built tests/cn_gpu_harness"
