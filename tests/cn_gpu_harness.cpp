/**
 * cn_gpu_harness.cpp — CryptoNight-GPU Validation Harness
 *
 * Standalone tool that computes CN-GPU hashes using the CPU reference
 * implementation and dumps intermediate state for bit-exact verification.
 *
 * Usage:
 *   ./cn_gpu_harness                    # Run built-in test vectors
 *   ./cn_gpu_harness --hex <input_hex>  # Hash arbitrary hex input
 *   ./cn_gpu_harness --dump <input_hex> # Full phase dump for debugging
 *
 * Build (from repo root):
 *   g++ -std=c++17 -O2 -march=native -msse2 -maes -mavx2 \
 *       -I. tests/cn_gpu_harness.cpp \
 *       n0s/backend/cpu/crypto/cn_gpu_avx.cpp \
 *       n0s/backend/cpu/crypto/cn_gpu_ssse3.cpp \
 *       n0s/backend/cpu/crypto/c_blake256.c \
 *       n0s/backend/cpu/crypto/c_groestl.c \
 *       n0s/backend/cpu/crypto/c_jh.c \
 *       n0s/backend/cpu/crypto/keccak.cpp \
 *       n0s/backend/cpu/crypto/c_skein.c \
 *       -o tests/cn_gpu_harness -lpthread
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <cfenv>

#include "n0s/backend/cryptonight.hpp"

// Stub out jconf dependency — we don't need runtime config for hashing
namespace n0s { class globalStates; }
class jconf {
public:
    struct coinDescription {
        std::vector<n0s_algo> GetAllAlgorithms() const {
            return { POW(cryptonight_gpu) };
        }
    };
    coinDescription GetCurrentCoinSelection() const { return cd_; }
    static jconf* inst() {
        static jconf j;
        return &j;
    }
private:
    coinDescription cd_;
};

#include "n0s/backend/cpu/crypto/cryptonight.h"
#include "n0s/backend/cpu/crypto/cn_gpu.hpp"

// Import keccak
extern "C" {
    void keccak(const uint8_t* in, int inlen, uint8_t* md, int mdlen);
    void keccakf(uint64_t st[25], int rounds);
    extern void (*const extra_hashes[4])(const void*, uint32_t, char*);
}

// ============================================================
// Minimal memory allocator (bypass jconf in cryptonight_alloc_ctx)
// ============================================================
#include <mm_malloc.h>
#include <x86intrin.h>

static cryptonight_ctx* alloc_ctx() {
    cryptonight_ctx* ctx = (cryptonight_ctx*)_mm_malloc(sizeof(cryptonight_ctx), 4096);
    if (!ctx) {
        fprintf(stderr, "ERROR: Failed to allocate cryptonight_ctx\n");
        exit(1);
    }
    memset(ctx, 0, sizeof(cryptonight_ctx));
    ctx->long_state = (uint8_t*)_mm_malloc(CN_MEMORY, CN_MEMORY);
    if (!ctx->long_state) {
        fprintf(stderr, "ERROR: Failed to allocate 2MB scratchpad\n");
        exit(1);
    }
    return ctx;
}

static void free_ctx(cryptonight_ctx* ctx) {
    if (ctx) {
        if (ctx->long_state) _mm_free(ctx->long_state);
        _mm_free(ctx);
    }
}

// ============================================================
// Include the CPU hash implementation templates
// ============================================================

// Forward declarations from cryptonight_aesni.h that we need
static inline void set_float_rounding_mode_nearest() {
    std::fesetround(FE_TONEAREST);
}

// We replicate the Cryptonight_hash_gpu::hash logic directly here
// to avoid pulling in the entire macro nightmare from cryptonight_aesni.h

// AES key generation and round functions
#include "n0s/backend/cpu/crypto/soft_aes.hpp"

static inline __m128i sl_xor_h(__m128i tmp1) {
    __m128i tmp4;
    tmp4 = _mm_slli_si128(tmp1, 0x04);
    tmp1 = _mm_xor_si128(tmp1, tmp4);
    tmp4 = _mm_slli_si128(tmp4, 0x04);
    tmp1 = _mm_xor_si128(tmp1, tmp4);
    tmp4 = _mm_slli_si128(tmp4, 0x04);
    tmp1 = _mm_xor_si128(tmp1, tmp4);
    return tmp1;
}

template <uint8_t rcon>
static inline void aes_genkey_sub_h(__m128i* xout0, __m128i* xout2) {
    __m128i xout1 = _mm_aeskeygenassist_si128(*xout2, rcon);
    xout1 = _mm_shuffle_epi32(xout1, 0xFF);
    *xout0 = sl_xor_h(*xout0);
    *xout0 = _mm_xor_si128(*xout0, xout1);
    xout1 = _mm_aeskeygenassist_si128(*xout0, 0x00);
    xout1 = _mm_shuffle_epi32(xout1, 0xAA);
    *xout2 = sl_xor_h(*xout2);
    *xout2 = _mm_xor_si128(*xout2, xout1);
}

static inline void aes_genkey_h(const __m128i* memory, __m128i* k0, __m128i* k1, __m128i* k2, __m128i* k3,
    __m128i* k4, __m128i* k5, __m128i* k6, __m128i* k7, __m128i* k8, __m128i* k9) {
    __m128i xout0, xout2;
    xout0 = _mm_load_si128(memory);
    xout2 = _mm_load_si128(memory + 1);
    *k0 = xout0; *k1 = xout2;
    aes_genkey_sub_h<0x01>(&xout0, &xout2); *k2 = xout0; *k3 = xout2;
    aes_genkey_sub_h<0x02>(&xout0, &xout2); *k4 = xout0; *k5 = xout2;
    aes_genkey_sub_h<0x04>(&xout0, &xout2); *k6 = xout0; *k7 = xout2;
    aes_genkey_sub_h<0x08>(&xout0, &xout2); *k8 = xout0; *k9 = xout2;
}

static inline void aes_round_h(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3,
    __m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7) {
    *x0 = _mm_aesenc_si128(*x0, key);
    *x1 = _mm_aesenc_si128(*x1, key);
    *x2 = _mm_aesenc_si128(*x2, key);
    *x3 = _mm_aesenc_si128(*x3, key);
    *x4 = _mm_aesenc_si128(*x4, key);
    *x5 = _mm_aesenc_si128(*x5, key);
    *x6 = _mm_aesenc_si128(*x6, key);
    *x7 = _mm_aesenc_si128(*x7, key);
}

static inline void mix_and_propagate_h(__m128i& x0, __m128i& x1, __m128i& x2, __m128i& x3,
    __m128i& x4, __m128i& x5, __m128i& x6, __m128i& x7) {
    __m128i tmp0 = x0;
    x0 = _mm_xor_si128(x0, x1);
    x1 = _mm_xor_si128(x1, x2);
    x2 = _mm_xor_si128(x2, x3);
    x3 = _mm_xor_si128(x3, x4);
    x4 = _mm_xor_si128(x4, x5);
    x5 = _mm_xor_si128(x5, x6);
    x6 = _mm_xor_si128(x6, x7);
    x7 = _mm_xor_si128(x7, tmp0);
}

// GPU-specific scratchpad expansion (keccak-based, not AES-based)
static void explode_scratchpad_gpu(const uint8_t* input, uint8_t* output) {
    alignas(128) uint64_t hash[25];
    for (uint64_t i = 0; i < CN_MEMORY / 512; i++) {
        memcpy(hash, input, 200);
        hash[0] ^= i;
        keccakf(hash, 24);
        memcpy(output, hash, 160);
        output += 160;
        keccakf(hash, 24);
        memcpy(output, hash, 176);
        output += 176;
        keccakf(hash, 24);
        memcpy(output, hash, 176);
        output += 176;
    }
}

// CN-GPU implode: 2 passes over scratchpad + 16 rounds AES + mix_and_propagate
static void implode_scratchpad_gpu(__m128i* input, __m128i* output) {
    __m128i xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7;
    __m128i k0, k1, k2, k3, k4, k5, k6, k7, k8, k9;

    aes_genkey_h(output + 2, &k0, &k1, &k2, &k3, &k4, &k5, &k6, &k7, &k8, &k9);

    xout0 = _mm_load_si128(output + 4);
    xout1 = _mm_load_si128(output + 5);
    xout2 = _mm_load_si128(output + 6);
    xout3 = _mm_load_si128(output + 7);
    xout4 = _mm_load_si128(output + 8);
    xout5 = _mm_load_si128(output + 9);
    xout6 = _mm_load_si128(output + 10);
    xout7 = _mm_load_si128(output + 11);

    // Pass 1: XOR + AES + mix_and_propagate
    for (size_t i = 0; i < CN_MEMORY / sizeof(__m128i); i += 8) {
        xout0 = _mm_xor_si128(_mm_load_si128(input + i + 0), xout0);
        xout1 = _mm_xor_si128(_mm_load_si128(input + i + 1), xout1);
        xout2 = _mm_xor_si128(_mm_load_si128(input + i + 2), xout2);
        xout3 = _mm_xor_si128(_mm_load_si128(input + i + 3), xout3);
        xout4 = _mm_xor_si128(_mm_load_si128(input + i + 4), xout4);
        xout5 = _mm_xor_si128(_mm_load_si128(input + i + 5), xout5);
        xout6 = _mm_xor_si128(_mm_load_si128(input + i + 6), xout6);
        xout7 = _mm_xor_si128(_mm_load_si128(input + i + 7), xout7);

        aes_round_h(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);

        mix_and_propagate_h(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
    }

    // Pass 2: XOR + AES + mix_and_propagate
    for (size_t i = 0; i < CN_MEMORY / sizeof(__m128i); i += 8) {
        xout0 = _mm_xor_si128(_mm_load_si128(input + i + 0), xout0);
        xout1 = _mm_xor_si128(_mm_load_si128(input + i + 1), xout1);
        xout2 = _mm_xor_si128(_mm_load_si128(input + i + 2), xout2);
        xout3 = _mm_xor_si128(_mm_load_si128(input + i + 3), xout3);
        xout4 = _mm_xor_si128(_mm_load_si128(input + i + 4), xout4);
        xout5 = _mm_xor_si128(_mm_load_si128(input + i + 5), xout5);
        xout6 = _mm_xor_si128(_mm_load_si128(input + i + 6), xout6);
        xout7 = _mm_xor_si128(_mm_load_si128(input + i + 7), xout7);

        aes_round_h(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);

        mix_and_propagate_h(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
    }

    // 16 extra rounds
    for (size_t i = 0; i < 16; i++) {
        aes_round_h(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
        aes_round_h(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);

        mix_and_propagate_h(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
    }

    _mm_store_si128(output + 4, xout0);
    _mm_store_si128(output + 5, xout1);
    _mm_store_si128(output + 6, xout2);
    _mm_store_si128(output + 7, xout3);
    _mm_store_si128(output + 8, xout4);
    _mm_store_si128(output + 9, xout5);
    _mm_store_si128(output + 10, xout6);
    _mm_store_si128(output + 11, xout7);
}

// ============================================================
// Full CN-GPU hash (CPU reference)
// ============================================================
static void cn_gpu_hash(const uint8_t* input, size_t len, uint8_t* output,
                        uint8_t* hash_state_out = nullptr,
                        uint8_t* scratchpad_snapshot_after_explode = nullptr,
                        uint8_t* scratchpad_snapshot_after_inner = nullptr) {
    cryptonight_ctx* ctx = alloc_ctx();
    n0s_algo algo = POW(cryptonight_gpu);

    // Phase 1: Keccak
    set_float_rounding_mode_nearest();
    keccak(input, (int)len, ctx->hash_state, 200);

    if (hash_state_out)
        memcpy(hash_state_out, ctx->hash_state, 200);

    // Phase 2: Expand scratchpad (GPU-style: keccak-based)
    explode_scratchpad_gpu(ctx->hash_state, ctx->long_state);

    if (scratchpad_snapshot_after_explode) {
        // Dump first + last 512 bytes
        memcpy(scratchpad_snapshot_after_explode, ctx->long_state, 512);
        memcpy(scratchpad_snapshot_after_explode + 512, ctx->long_state + CN_MEMORY - 512, 512);
    }

    // Phase 3: GPU inner loop (AVX2 or SSSE3 on CPU)
    if (cngpu_check_avx2())
        cn_gpu_inner_avx(ctx->hash_state, ctx->long_state, algo);
    else
        cn_gpu_inner_ssse3(ctx->hash_state, ctx->long_state, algo);

    if (scratchpad_snapshot_after_inner) {
        memcpy(scratchpad_snapshot_after_inner, ctx->long_state, 512);
        memcpy(scratchpad_snapshot_after_inner + 512, ctx->long_state + CN_MEMORY - 512, 512);
    }

    // Phase 4: Implode scratchpad (HEAVY_MIX mode)
    implode_scratchpad_gpu((__m128i*)ctx->long_state, (__m128i*)ctx->hash_state);

    // Phase 5: Final keccak
    keccakf((uint64_t*)ctx->hash_state, 24);

    // cn_gpu outputs first 32 bytes directly (no extra_hashes branch)
    memcpy(output, ctx->hash_state, 32);

    free_ctx(ctx);
}

// ============================================================
// Utility functions
// ============================================================
static void hex_to_bytes(const char* hex, uint8_t* bytes, size_t* out_len) {
    size_t len = strlen(hex);
    *out_len = len / 2;
    for (size_t i = 0; i < *out_len; i++) {
        unsigned int byte;
        sscanf(hex + 2 * i, "%02x", &byte);
        bytes[i] = (uint8_t)byte;
    }
}

static void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++)
        printf("%02x", data[i]);
}

static void print_hex_block(const char* label, const uint8_t* data, size_t len) {
    printf("%s: ", label);
    print_hex(data, len);
    printf("\n");
}

// ============================================================
// Test vectors
//
// These are known-good CN-GPU hashes. If any of these fail after
// a code change, something is broken.
// ============================================================
struct TestVector {
    const char* input_hex;
    const char* expected_hash_hex;
    const char* description;
};

// We'll generate our own test vectors on first run and verify reproducibility
// For now, use empty/simple inputs
static const TestVector test_vectors[] = {
    {
        // 76-byte block header (all zeros, typical mining input length)
        "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
        "232405b4d6db9ebf06a9bfb0d50e0e73fe212bca7a026a7e6bf4ff1fe5d88ca2",
        "76-byte zero block"
    },
    {
        // 76-byte block with nonce = 1 at offset 39
        "00000000000000000000000000000000000000000000000000000000000000000000000000000000010000000000000000000000000000000000000000000000000000000000000000000000",
        "85e990391b03ebe3dc446f5c3e87e6427ee6a406885c1c4fde71b1c007e47628",
        "76-byte block, nonce=1"
    },
    {
        // Short input (43 bytes minimum for cn_gpu)
        "0101010101010101010101010101010101010101010101010101010101010101010101010101010101010101",
        "4646f4b2b409f5736ff52a16568a6561b5c779ee9f9f809d13b327351824dbb8",
        "43-byte all-ones"
    },
};
static constexpr int NUM_TEST_VECTORS = sizeof(test_vectors) / sizeof(test_vectors[0]);

// ============================================================
// Main
// ============================================================
int main(int argc, char** argv) {
    printf("=== CryptoNight-GPU Validation Harness ===\n");
    printf("CPU: %s\n", cngpu_check_avx2() ? "AVX2" : "SSSE3");
    printf("Algorithm: cryptonight_gpu (id=%d)\n", cryptonight_gpu);
    printf("Memory: %zu bytes (%zu MiB)\n", (size_t)CN_MEMORY, (size_t)CN_MEMORY / (1024 * 1024));
    printf("Iterations: %u (0x%X)\n", CN_GPU_ITER, CN_GPU_ITER);
    printf("Mask: 0x%X\n", CN_GPU_MASK);
    printf("\n");

    if (argc >= 3 && (strcmp(argv[1], "--hex") == 0 || strcmp(argv[1], "--dump") == 0)) {
        bool dump = (strcmp(argv[1], "--dump") == 0);
        uint8_t input[256];
        size_t input_len;
        hex_to_bytes(argv[2], input, &input_len);

        printf("Input (%zu bytes): ", input_len);
        print_hex(input, input_len);
        printf("\n\n");

        uint8_t output[32];

        if (dump) {
            uint8_t hash_state[200];
            uint8_t spad_after_explode[1024];  // first 512 + last 512
            uint8_t spad_after_inner[1024];

            auto t0 = std::chrono::high_resolution_clock::now();
            cn_gpu_hash(input, input_len, output, hash_state, spad_after_explode, spad_after_inner);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            printf("--- Phase 1: Keccak ---\n");
            print_hex_block("hash_state[0:64]", hash_state, 64);
            print_hex_block("hash_state[64:128]", hash_state + 64, 64);
            print_hex_block("hash_state[128:200]", hash_state + 128, 72);
            printf("\n");

            printf("--- Phase 2: Scratchpad Expansion ---\n");
            print_hex_block("scratchpad[0:64]", spad_after_explode, 64);
            print_hex_block("scratchpad[last 64]", spad_after_explode + 1024 - 64, 64);
            printf("\n");

            printf("--- Phase 3: GPU Inner Loop (after) ---\n");
            print_hex_block("scratchpad[0:64]", spad_after_inner, 64);
            print_hex_block("scratchpad[last 64]", spad_after_inner + 1024 - 64, 64);
            printf("\n");

            printf("--- Phase 5: Final Hash ---\n");
            print_hex_block("hash", output, 32);
            printf("\nTime: %.1f ms\n", ms);
        } else {
            auto t0 = std::chrono::high_resolution_clock::now();
            cn_gpu_hash(input, input_len, output);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            printf("Hash: ");
            print_hex(output, 32);
            printf("\nTime: %.1f ms\n", ms);
        }
        return 0;
    }

    // Run test vectors
    printf("Running %d test vectors...\n\n", NUM_TEST_VECTORS);

    // First pass: compute all hashes
    uint8_t hashes[NUM_TEST_VECTORS][32];

    for (int t = 0; t < NUM_TEST_VECTORS; t++) {
        uint8_t input[256];
        size_t input_len;
        hex_to_bytes(test_vectors[t].input_hex, input, &input_len);

        printf("[%d] %s (%zu bytes)\n", t, test_vectors[t].description, input_len);

        auto t0 = std::chrono::high_resolution_clock::now();
        cn_gpu_hash(input, input_len, hashes[t]);
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        printf("    Hash: ");
        print_hex(hashes[t], 32);
        printf(" (%.1f ms)\n", ms);
    }

    // Second pass: verify against golden hashes AND reproducibility
    printf("\nVerifying against golden hashes...\n");
    int pass = 0, fail = 0;

    for (int t = 0; t < NUM_TEST_VECTORS; t++) {
        uint8_t input[256];
        size_t input_len;
        hex_to_bytes(test_vectors[t].input_hex, input, &input_len);

        // Check golden hash
        if (test_vectors[t].expected_hash_hex) {
            uint8_t expected[32];
            size_t elen;
            hex_to_bytes(test_vectors[t].expected_hash_hex, expected, &elen);
            if (memcmp(hashes[t], expected, 32) == 0) {
                printf("  [%d] ✅ PASS (matches golden hash)\n", t);
                pass++;
            } else {
                printf("  [%d] ❌ FAIL (golden hash mismatch!)\n", t);
                printf("       Expected: %s\n", test_vectors[t].expected_hash_hex);
                printf("       Got:      "); print_hex(hashes[t], 32); printf("\n");
                fail++;
                continue;
            }
        }

        // Check reproducibility
        uint8_t hash2[32];
        cn_gpu_hash(input, input_len, hash2);

        if (memcmp(hashes[t], hash2, 32) != 0) {
            printf("  [%d] ❌ FAIL (non-deterministic!)\n", t);
            printf("       Run 1: "); print_hex(hashes[t], 32); printf("\n");
            printf("       Run 2: "); print_hex(hash2, 32); printf("\n");
            fail++;
            pass--; // undo the golden pass
        }
    }

    printf("\n=== Results: %d passed, %d failed ===\n", pass, fail);

    // Print hashes as future reference
    printf("\n--- Golden Hashes (copy these into test vectors) ---\n");
    for (int t = 0; t < NUM_TEST_VECTORS; t++) {
        printf("  \"%s\": \"", test_vectors[t].description);
        print_hex(hashes[t], 32);
        printf("\"\n");
    }

    return fail > 0 ? 1 : 0;
}
