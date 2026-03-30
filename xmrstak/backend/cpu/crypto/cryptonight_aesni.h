/*
  * CryptoNight-GPU CPU hash implementation
  *
  * This file contains the CPU reference implementation for CryptoNight-GPU hashing.
  * It is used for:
  *   - Share verification on the CPU side
  *   - The standalone validation harness (tests/cn_gpu_harness.cpp)
  *   - CPU-only mining (if enabled, but normally disabled)
  *
  * The pipeline is:
  *   1. Keccak-1600 on input → 200-byte hash state
  *   2. Expand scratchpad (keccak-based, NOT AES-based for cn_gpu)
  *   3. GPU inner loop: 49,152 iterations of float math + scratchpad mutation
  *   4. Implode scratchpad: 2 passes AES+XOR+mix, 16 extra AES+mix rounds
  *   5. Final Keccak-f permutation
  *   6. Output = first 32 bytes of hash state (no extra_hashes branch)
  *
  * Original code from xmr-stak by fireice-uk & psychocrypt.
  * Stripped to cn_gpu-only by n0sn0de.
  */
#pragma once

#include "cn_gpu.hpp"
#include "cryptonight.h"
#include "xmrstak/backend/cryptonight.hpp"
#include <cfenv>
#include <cstring>
#include <cstdio>

#ifdef __GNUC__
#include <x86intrin.h>
#else
#include <intrin.h>
#endif

#include "soft_aes.hpp"

extern "C"
{
	void keccak(const uint8_t* in, int inlen, uint8_t* md, int mdlen);
	void keccakf(uint64_t st[25], int rounds);
}

// ============================================================
// Floating-point rounding mode
// ============================================================

inline void set_float_rounding_mode_nearest()
{
#ifdef _MSC_VER
	_control87(RC_NEAR, MCW_RC);
#else
	std::fesetround(FE_TONEAREST);
#endif
}

// ============================================================
// AES Key Generation
// ============================================================

// Shift-XOR for AES key schedule: sl_xor(a1 a2 a3 a4) = a1 (a2^a1) (a3^a2^a1) (a4^a3^a2^a1)
static inline __m128i sl_xor(__m128i tmp1)
{
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
static inline void aes_genkey_sub(__m128i* xout0, __m128i* xout2)
{
	__m128i xout1 = _mm_aeskeygenassist_si128(*xout2, rcon);
	xout1 = _mm_shuffle_epi32(xout1, 0xFF);
	*xout0 = sl_xor(*xout0);
	*xout0 = _mm_xor_si128(*xout0, xout1);
	xout1 = _mm_aeskeygenassist_si128(*xout0, 0x00);
	xout1 = _mm_shuffle_epi32(xout1, 0xAA);
	*xout2 = sl_xor(*xout2);
	*xout2 = _mm_xor_si128(*xout2, xout1);
}

static inline void soft_aes_genkey_sub(__m128i* xout0, __m128i* xout2, uint8_t rcon)
{
	__m128i xout1 = soft_aeskeygenassist(*xout2, rcon);
	xout1 = _mm_shuffle_epi32(xout1, 0xFF);
	*xout0 = sl_xor(*xout0);
	*xout0 = _mm_xor_si128(*xout0, xout1);
	xout1 = soft_aeskeygenassist(*xout0, 0x00);
	xout1 = _mm_shuffle_epi32(xout1, 0xAA);
	*xout2 = sl_xor(*xout2);
	*xout2 = _mm_xor_si128(*xout2, xout1);
}

template <bool SOFT_AES>
static inline void aes_genkey(const __m128i* memory, __m128i* k0, __m128i* k1, __m128i* k2, __m128i* k3,
	__m128i* k4, __m128i* k5, __m128i* k6, __m128i* k7, __m128i* k8, __m128i* k9)
{
	__m128i xout0, xout2;

	xout0 = _mm_load_si128(memory);
	xout2 = _mm_load_si128(memory + 1);
	*k0 = xout0;
	*k1 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x01);
	else
		aes_genkey_sub<0x01>(&xout0, &xout2);
	*k2 = xout0;
	*k3 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x02);
	else
		aes_genkey_sub<0x02>(&xout0, &xout2);
	*k4 = xout0;
	*k5 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x04);
	else
		aes_genkey_sub<0x04>(&xout0, &xout2);
	*k6 = xout0;
	*k7 = xout2;

	if(SOFT_AES)
		soft_aes_genkey_sub(&xout0, &xout2, 0x08);
	else
		aes_genkey_sub<0x08>(&xout0, &xout2);
	*k8 = xout0;
	*k9 = xout2;
}

// ============================================================
// AES Round Functions
// ============================================================

static inline void aes_round(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3,
	__m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7)
{
	*x0 = _mm_aesenc_si128(*x0, key);
	*x1 = _mm_aesenc_si128(*x1, key);
	*x2 = _mm_aesenc_si128(*x2, key);
	*x3 = _mm_aesenc_si128(*x3, key);
	*x4 = _mm_aesenc_si128(*x4, key);
	*x5 = _mm_aesenc_si128(*x5, key);
	*x6 = _mm_aesenc_si128(*x6, key);
	*x7 = _mm_aesenc_si128(*x7, key);
}

static inline void soft_aes_round(__m128i key, __m128i* x0, __m128i* x1, __m128i* x2, __m128i* x3,
	__m128i* x4, __m128i* x5, __m128i* x6, __m128i* x7)
{
	*x0 = soft_aesenc(*x0, key);
	*x1 = soft_aesenc(*x1, key);
	*x2 = soft_aesenc(*x2, key);
	*x3 = soft_aesenc(*x3, key);
	*x4 = soft_aesenc(*x4, key);
	*x5 = soft_aesenc(*x5, key);
	*x6 = soft_aesenc(*x6, key);
	*x7 = soft_aesenc(*x7, key);
}

// ============================================================
// mix_and_propagate: XOR chain across 8 state blocks
// ============================================================

inline void mix_and_propagate(__m128i& x0, __m128i& x1, __m128i& x2, __m128i& x3,
	__m128i& x4, __m128i& x5, __m128i& x6, __m128i& x7)
{
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

// ============================================================
// Phase 2: Scratchpad Expansion (keccak-based, GPU-style)
//
// Unlike other CryptoNight variants that use AES for expansion,
// cn_gpu expands the scratchpad using repeated Keccak-f permutations.
// Each 512-byte chunk is generated from the 200-byte hash state.
// ============================================================

template <bool PREFETCH, n0s_algo_id ALGO>
void cn_explode_scratchpad_gpu(const uint8_t* input, uint8_t* output, const n0s_algo& algo)
{
	constexpr size_t hash_size = 200; // 25x8 bytes
	alignas(128) uint64_t hash[25];
	const size_t mem = algo.Mem();

	for(uint64_t i = 0; i < mem / 512; i++)
	{
		memcpy(hash, input, hash_size);
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

		if(PREFETCH)
		{
			_mm_prefetch((const char*)output - 512, _MM_HINT_T2);
			_mm_prefetch((const char*)output - 384, _MM_HINT_T2);
			_mm_prefetch((const char*)output - 256, _MM_HINT_T2);
			_mm_prefetch((const char*)output - 128, _MM_HINT_T2);
		}
	}
}

// ============================================================
// Phase 4: Scratchpad Compression (implode) — HEAVY_MIX mode
//
// cn_gpu uses "heavy mix" implode:
//   Pass 1: XOR scratchpad blocks into state, 10 AES rounds + mix_and_propagate per block
//   Pass 2: Same again (second full pass over scratchpad)
//   16 extra rounds of AES + mix_and_propagate (no scratchpad XOR)
// ============================================================

template <bool SOFT_AES, bool PREFETCH, n0s_algo_id ALGO>
void cn_implode_scratchpad(const __m128i* input, __m128i* output, const n0s_algo& algo)
{
	__m128i xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7;
	__m128i k0, k1, k2, k3, k4, k5, k6, k7, k8, k9;

	// Key derived from hash_state[32:64] (the second 32 bytes)
	aes_genkey<SOFT_AES>(output + 2, &k0, &k1, &k2, &k3, &k4, &k5, &k6, &k7, &k8, &k9);

	// Initial state from hash_state[64:192] (8 × 16-byte blocks)
	xout0 = _mm_load_si128(output + 4);
	xout1 = _mm_load_si128(output + 5);
	xout2 = _mm_load_si128(output + 6);
	xout3 = _mm_load_si128(output + 7);
	xout4 = _mm_load_si128(output + 8);
	xout5 = _mm_load_si128(output + 9);
	xout6 = _mm_load_si128(output + 10);
	xout7 = _mm_load_si128(output + 11);

	const size_t MEM = algo.Mem();

	// Helper lambda: one pass over scratchpad with AES rounds + mix_and_propagate
	auto scratchpad_pass = [&]() {
		for(size_t i = 0; i < MEM / sizeof(__m128i); i += 8)
		{
			if(PREFETCH)
				_mm_prefetch((const char*)input + i + 0, _MM_HINT_NTA);

			xout0 = _mm_xor_si128(_mm_load_si128(input + i + 0), xout0);
			xout1 = _mm_xor_si128(_mm_load_si128(input + i + 1), xout1);
			xout2 = _mm_xor_si128(_mm_load_si128(input + i + 2), xout2);
			xout3 = _mm_xor_si128(_mm_load_si128(input + i + 3), xout3);

			if(PREFETCH)
				_mm_prefetch((const char*)input + i + 4, _MM_HINT_NTA);

			xout4 = _mm_xor_si128(_mm_load_si128(input + i + 4), xout4);
			xout5 = _mm_xor_si128(_mm_load_si128(input + i + 5), xout5);
			xout6 = _mm_xor_si128(_mm_load_si128(input + i + 6), xout6);
			xout7 = _mm_xor_si128(_mm_load_si128(input + i + 7), xout7);

			if(SOFT_AES)
			{
				soft_aes_round(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				soft_aes_round(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			}
			else
			{
				aes_round(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
				aes_round(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			}

			mix_and_propagate(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
		}
	};

	// cn_gpu HEAVY_MIX: two full passes over scratchpad
	scratchpad_pass();
	scratchpad_pass();

	// 16 extra rounds of AES + mix_and_propagate (no scratchpad XOR)
	for(size_t i = 0; i < 16; i++)
	{
		if(SOFT_AES)
		{
			soft_aes_round(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			soft_aes_round(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
		}
		else
		{
			aes_round(k0, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k1, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k2, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k3, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k4, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k5, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k6, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k7, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k8, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
			aes_round(k9, &xout0, &xout1, &xout2, &xout3, &xout4, &xout5, &xout6, &xout7);
		}

		mix_and_propagate(xout0, xout1, xout2, xout3, xout4, xout5, xout6, xout7);
	}

	// Write compressed state back to hash_state[64:192]
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
// CryptoNight-GPU hash function (CPU reference implementation)
// ============================================================

struct Cryptonight_hash_gpu
{
	static constexpr size_t N = 1;

	template <n0s_algo_id ALGO, bool SOFT_AES, bool PREFETCH>
	static void hash(const void* input, size_t len, void* output, cryptonight_ctx** ctx, const n0s_algo& algo)
	{
		// Phase 1: Keccak-1600
		set_float_rounding_mode_nearest();
		keccak((const uint8_t*)input, len, ctx[0]->hash_state, 200);

		// Phase 2: Expand scratchpad (keccak-based)
		cn_explode_scratchpad_gpu<PREFETCH, ALGO>(ctx[0]->hash_state, ctx[0]->long_state, algo);

		// Phase 3: GPU inner loop (float math + scratchpad mutation)
		if(cngpu_check_avx2())
			cn_gpu_inner_avx(ctx[0]->hash_state, ctx[0]->long_state, algo);
		else
			cn_gpu_inner_ssse3(ctx[0]->hash_state, ctx[0]->long_state, algo);

		// Phase 4: Implode scratchpad (HEAVY_MIX: 2 passes + 16 extra rounds)
		cn_implode_scratchpad<SOFT_AES, PREFETCH, ALGO>((__m128i*)ctx[0]->long_state, (__m128i*)ctx[0]->hash_state, algo);

		// Phase 5: Final Keccak-f permutation
		keccakf((uint64_t*)ctx[0]->hash_state, 24);

		// cn_gpu outputs first 32 bytes directly (no extra_hashes dispatch)
		memcpy(output, ctx[0]->hash_state, 32);
	}
};
