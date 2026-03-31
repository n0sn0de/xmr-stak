/**
 * cuda_cryptonight_gpu.hpp — CryptoNight-GPU CUDA Kernels
 *
 * GPU implementation of the CryptoNight-GPU algorithm for NVIDIA GPUs.
 *
 * Kernels:
 *   kernel_expand_scratchpad  — Phase 2: Keccak-based scratchpad expansion
 *   kernel_gpu_compute        — Phase 3: Floating-point computation loop
 *
 * Phase 1 (prepare) and Phase 5 (finalize) are in cuda_extra.cu
 * Phase 4 (implode/compress) is in cuda_core.cu (cryptonight_core_gpu_phase3)
 *
 * CRITICAL: NVCC must be invoked with --fmad=false --prec-div=true --ftz=false
 * to ensure IEEE 754 compliance for bit-exact hashes.
 *
 * Original: xmr-stak by fireice-uk & psychocrypt
 * Cleaned up by n0sn0de
 */
#pragma once

#include <cstdint>
#include <cuda_runtime.h>
#include <cstdio>

#include "cuda_extra.hpp"
#include "cuda_keccak.hpp"

// ============================================================
// Global memory access helpers
//
// Pre-Volta (< sm_70): use PTX ld/st with .cg cache hint
// Volta+: plain dereference (hardware handles caching better)
// ============================================================

template <typename T>
__device__ __forceinline__ T loadGlobal64(T* const addr)
{
#if(__CUDA_ARCH__ < 700)
	T x;
	asm volatile("ld.global.cg.u64 %0, [%1];"
				 : "=l"(x)
				 : "l"(addr));
	return x;
#else
	return *addr;
#endif
}

template <typename T>
__device__ __forceinline__ T loadGlobal32(T* const addr)
{
#if(__CUDA_ARCH__ < 700)
	T x;
	asm volatile("ld.global.cg.u32 %0, [%1];"
				 : "=r"(x)
				 : "l"(addr));
	return x;
#else
	return *addr;
#endif
}

template <typename T>
__device__ __forceinline__ void storeGlobal32(T* addr, T const& val)
{
#if(__CUDA_ARCH__ < 700)
	asm volatile("st.global.cg.u32 [%0], %1;"
				 :
				 : "l"(addr), "r"(val));
#else
	*addr = val;
#endif
}

template <typename T>
__device__ __forceinline__ void storeGlobal64(T* addr, T const& val)
{
#if(__CUDA_ARCH__ < 700)
	asm volatile("st.global.cg.u64 [%0], %1;"
				 :
				 : "l"(addr), "l"(val));
#else
	*addr = val;
#endif
}

namespace n0s
{
namespace cuda
{

// ============================================================
// SIMD-like types for CUDA
//
// These mirror SSE __m128i / __m128 to keep the algorithm
// structurally similar between CPU and GPU implementations.
// ============================================================

struct __m128i : public int4
{
	__forceinline__ __device__ __m128i() {}

	__forceinline__ __device__ __m128i(
		const uint32_t x0, const uint32_t x1,
		const uint32_t x2, const uint32_t x3)
	{
		x = x0; y = x1; z = x2; w = x3;
	}

	__forceinline__ __device__ __m128i(const int x0)
	{
		x = x0; y = x0; z = x0; w = x0;
	}

	__forceinline__ __device__ __m128i operator|(const __m128i& other)
	{
		return __m128i(x | other.x, y | other.y, z | other.z, w | other.w);
	}

	__forceinline__ __device__ __m128i operator^(const __m128i& other)
	{
		return __m128i(x ^ other.x, y ^ other.y, z ^ other.z, w ^ other.w);
	}
};

struct __m128 : public float4
{
	__forceinline__ __device__ __m128() {}

	__forceinline__ __device__ __m128(
		const float x0, const float x1,
		const float x2, const float x3)
	{
		float4::x = x0; float4::y = x1; float4::z = x2; float4::w = x3;
	}

	__forceinline__ __device__ __m128(const float x0)
	{
		float4::x = x0; float4::y = x0; float4::z = x0; float4::w = x0;
	}

	__forceinline__ __device__ __m128(const __m128i& x0)
	{
		float4::x = __int2float_rn(x0.x);
		float4::y = __int2float_rn(x0.y);
		float4::z = __int2float_rn(x0.z);
		float4::w = __int2float_rn(x0.w);
	}

	__forceinline__ __device__ __m128i get_int()
	{
		return __m128i((int)x, (int)y, (int)z, (int)w);
	}

	__forceinline__ __device__ __m128 operator+(const __m128& other)
	{
		return __m128(x + other.x, y + other.y, z + other.z, w + other.w);
	}

	__forceinline__ __device__ __m128 operator-(const __m128& other)
	{
		return __m128(x - other.x, y - other.y, z - other.z, w - other.w);
	}

	__forceinline__ __device__ __m128 operator*(const __m128& other)
	{
		return __m128(x * other.x, y * other.y, z * other.z, w * other.w);
	}

	__forceinline__ __device__ __m128 operator/(const __m128& other)
	{
		return __m128(x / other.x, y / other.y, z / other.z, w / other.w);
	}

	__forceinline__ __device__ __m128& trunc()
	{
		x = ::truncf(x); y = ::truncf(y); z = ::truncf(z); w = ::truncf(w);
		return *this;
	}

	__forceinline__ __device__ __m128& abs()
	{
		x = ::fabsf(x); y = ::fabsf(y); z = ::fabsf(z); w = ::fabsf(w);
		return *this;
	}

	__forceinline__ __device__ __m128& floor()
	{
		x = ::floorf(x); y = ::floorf(y); z = ::floorf(z); w = ::floorf(w);
		return *this;
	}
};

// ============================================================
// SSE intrinsic wrappers for CUDA
// ============================================================

__forceinline__ __device__ __m128 _mm_add_ps(__m128 a, __m128 b) { return a + b; }
__forceinline__ __device__ __m128 _mm_sub_ps(__m128 a, __m128 b) { return a - b; }
__forceinline__ __device__ __m128 _mm_mul_ps(__m128 a, __m128 b) { return a * b; }
__forceinline__ __device__ __m128 _mm_div_ps(__m128 a, __m128 b) { return a / b; }

__forceinline__ __device__ __m128 _mm_and_ps(__m128 a, int b)
{
	return __m128(
		__int_as_float(__float_as_int(a.x) & b),
		__int_as_float(__float_as_int(a.y) & b),
		__int_as_float(__float_as_int(a.z) & b),
		__int_as_float(__float_as_int(a.w) & b));
}

__forceinline__ __device__ __m128 _mm_or_ps(__m128 a, int b)
{
	return __m128(
		__int_as_float(__float_as_int(a.x) | b),
		__int_as_float(__float_as_int(a.y) | b),
		__int_as_float(__float_as_int(a.z) | b),
		__int_as_float(__float_as_int(a.w) | b));
}

__forceinline__ __device__ __m128 _mm_xor_ps(__m128 a, int b)
{
	return __m128(
		__int_as_float(__float_as_int(a.x) ^ b),
		__int_as_float(__float_as_int(a.y) ^ b),
		__int_as_float(__float_as_int(a.z) ^ b),
		__int_as_float(__float_as_int(a.w) ^ b));
}

__forceinline__ __device__ __m128i _mm_xor_si128(__m128i a, __m128i b) { return a ^ b; }

__forceinline__ __device__ __m128i _mm_alignr_epi8(__m128i a, const uint32_t rot)
{
	const uint32_t right = 8 * rot;
	const uint32_t left = (32 - 8 * rot);
	return __m128i(
		((uint32_t)a.x >> right) | (a.y << left),
		((uint32_t)a.y >> right) | (a.z << left),
		((uint32_t)a.z >> right) | (a.w << left),
		((uint32_t)a.w >> right) | (a.x << left));
}

// ============================================================
// Scratchpad addressing
// ============================================================

/// Get pointer to 16-byte chunk within 64-byte aligned scratchpad slot
__device__ inline __m128i* scratchpad_ptr(uint32_t idx, uint32_t n, int* scratchpad, const uint32_t MASK)
{
	return (__m128i*)((uint8_t*)scratchpad + (idx & MASK) + n * 16);
}

// ============================================================
// Phase 3: Floating-point computation functions
//
// These implement the CryptoNight-GPU FP chain that makes
// the algorithm GPU-friendly. See docs/CN-GPU-WHITEPAPER.md
// ============================================================

/// Break FMA dependency chain by forcing exponent to ?????01
/// This ensures values stay in [1.0, 2.0) range
__forceinline__ __device__ __m128 fma_break(__m128 x)
{
	x = _mm_and_ps(x, 0xFEFFFFFF);   // clear exponent bit 24
	return _mm_or_ps(x, 0x00800000);  // set exponent bit 23
}

/**
 * fp_sub_round — One sub-round of the floating-point computation
 *
 * Computes numerator and denominator contributions from 4 input vectors,
 * with constant feedback to maintain the dependency chain.
 *
 * @param n0..n3  Four 128-bit float vectors from scratchpad/shuffle
 * @param rnd_c   Round constant (previous iteration's accumulator)
 * @param n       Running numerator accumulator (modified)
 * @param d       Running denominator accumulator (modified)
 * @param c       Feedback accumulator (modified)
 */
__forceinline__ __device__ void fp_sub_round(
	__m128 n0, __m128 n1, __m128 n2, __m128 n3,
	__m128 rnd_c, __m128& n, __m128& d, __m128& c)
{
	// Numerator contribution
	n1 = _mm_add_ps(n1, c);
	__m128 nn = _mm_mul_ps(n0, c);
	nn = _mm_mul_ps(n1, _mm_mul_ps(nn, nn));
	nn = fma_break(nn);
	n = _mm_add_ps(n, nn);

	// Denominator contribution
	n3 = _mm_sub_ps(n3, c);
	__m128 dd = _mm_mul_ps(n2, c);
	dd = _mm_mul_ps(n3, _mm_mul_ps(dd, dd));
	dd = fma_break(dd);
	d = _mm_add_ps(d, dd);

	// Constant feedback: drift accumulator to prevent convergence
	c = _mm_add_ps(c, rnd_c);
	c = _mm_add_ps(c, 0.734375f);   // FP_FEEDBACK_CONSTANT
	__m128 r = _mm_add_ps(nn, dd);
	r = _mm_and_ps(r, 0x807FFFFF);  // extract sign + mantissa
	r = _mm_or_ps(r, 0x40000000);   // force into [2.0, 4.0) range
	c = _mm_add_ps(c, r);
}

/**
 * fp_round — One full round: 8 sub-rounds with rotated inputs, then safe division
 *
 * The 8 sub-rounds use all permutations of input order to ensure
 * thorough mixing. The division at the end uses d clamped to |d| > 2.0
 * to prevent division by zero or small numbers.
 */
__forceinline__ __device__ void fp_round(
	__m128 n0, __m128 n1, __m128 n2, __m128 n3,
	__m128 rnd_c, __m128& c, __m128& r)
{
	__m128 n(0.0f), d(0.0f);

	fp_sub_round(n0, n1, n2, n3, rnd_c, n, d, c);
	fp_sub_round(n1, n2, n3, n0, rnd_c, n, d, c);
	fp_sub_round(n2, n3, n0, n1, rnd_c, n, d, c);
	fp_sub_round(n3, n0, n1, n2, rnd_c, n, d, c);
	fp_sub_round(n3, n2, n1, n0, rnd_c, n, d, c);
	fp_sub_round(n2, n1, n0, n3, rnd_c, n, d, c);
	fp_sub_round(n1, n0, n3, n2, rnd_c, n, d, c);
	fp_sub_round(n0, n3, n2, n1, rnd_c, n, d, c);

	// Clamp denominator: |d| > 2.0 to prevent division hazards
	d = _mm_and_ps(d, 0xFF7FFFFF);  // clear sign bit of exponent
	d = _mm_or_ps(d, 0x40000000);   // force exponent to >= 2.0
	r = _mm_add_ps(r, _mm_div_ps(n, d));
}

/**
 * compute_fp_chain — Full floating-point computation for one thread
 *
 * Runs 4 rounds of fp_round (= 32 sub-rounds), producing a float result
 * that's converted to a 32-bit integer index for scratchpad addressing.
 *
 * @param n0..n3  Scratchpad data vectors (from cross-thread shuffle)
 * @param cnt     Per-thread constant (from THREAD_CONSTANTS[tid])
 * @param rnd_c   Round constant (previous iteration's accumulator)
 * @param sum     Output: accumulated float result for next iteration
 * @return        Integer result for scratchpad XOR
 */
__forceinline__ __device__ __m128i compute_fp_chain(
	__m128 n0, __m128 n1, __m128 n2, __m128 n3,
	float cnt, __m128 rnd_c, __m128& sum)
{
	__m128 c(cnt);
	__m128 r = __m128(0.0f);

	// 4 rounds × 8 sub-rounds × ~9 FLOPs = ~288 FLOPs per thread
	for(int i = 0; i < 4; ++i)
		fp_round(n0, n1, n2, n3, rnd_c, c, r);

	// Quick fmod: force result into [2.0, 4.0) range
	r = _mm_and_ps(r, 0x807FFFFF);
	r = _mm_or_ps(r, 0x40000000);
	sum = r;
	r = _mm_mul_ps(r, __m128(536870880.0f));  // FP_RESULT_SCALE
	return r.get_int();
}

/**
 * compute_fp_chain_rotated — Wrapper that applies byte rotation to the result
 *
 * Each thread within a group computes with a different rotation (0-3 bytes),
 * creating cross-lane data dependencies.
 */
__forceinline__ __device__ void compute_fp_chain_rotated(
	const uint32_t rot,
	const __m128i& v0, const __m128i& v1, const __m128i& v2, const __m128i& v3,
	float cnt, __m128 rnd_c, __m128& sum, __m128i& out)
{
	__m128 n0(v0), n1(v1), n2(v2), n3(v3);

	__m128i r = compute_fp_chain(n0, n1, n2, n3, cnt, rnd_c, sum);
	out = rot == 0 ? r : _mm_alignr_epi8(r, rot);
}

// ============================================================
// Phase 3: Constant data (stored in CUDA constant memory)
// ============================================================

/**
 * SHUFFLE_PATTERN — Cross-thread data dependency lookup table
 *
 * Each of the 16 threads reads scratchpad data from 4 specific threads
 * (including itself). This creates warp-level data sharing dependencies
 * that make the algorithm naturally GPU-parallel.
 *
 * Index: thread ID [0-15]
 * Value: array of 4 source thread IDs
 */
__constant__ uint32_t SHUFFLE_PATTERN[16][4] = {
	{0, 1, 2, 3},
	{0, 2, 3, 1},
	{0, 3, 1, 2},
	{0, 3, 2, 1},

	{1, 0, 2, 3},
	{1, 2, 3, 0},
	{1, 3, 0, 2},
	{1, 3, 2, 0},

	{2, 1, 0, 3},
	{2, 0, 3, 1},
	{2, 3, 1, 0},
	{2, 3, 0, 1},

	{3, 1, 2, 0},
	{3, 2, 0, 1},
	{3, 0, 1, 2},
	{3, 0, 2, 1}};

/**
 * THREAD_CONSTANTS — Per-thread initial counter values
 *
 * These IEEE 754 float32 values seed each thread's FP accumulator chain.
 * All values in [1.25, 1.5] range for numerical stability.
 */
__constant__ float THREAD_CONSTANTS[16] = {
	1.34375f,
	1.28125f,
	1.359375f,
	1.3671875f,

	1.4296875f,
	1.3984375f,
	1.3828125f,
	1.3046875f,

	1.4140625f,
	1.2734375f,
	1.2578125f,
	1.2890625f,

	1.3203125f,
	1.3515625f,
	1.3359375f,
	1.4609375f};

// ============================================================
// Warp synchronization
// ============================================================

__forceinline__ __device__ void warp_sync()
{
	__syncwarp();
}

// ============================================================
// Phase 3: Shared memory layout
// ============================================================

/// Per-hash shared memory: 16 output vectors + 17 float accumulators
/// (17th accumulator is scratch space for reduction)
struct SharedMemory
{
	__m128i computation_output[16];
	__m128 fp_accumulators[17];
};

// ============================================================
// Phase 2 & 3 Kernel Forward Declarations
//
// Implementations in cuda_phase2_3.cu
// ============================================================

/// Phase 2 Kernel: Expand 200-byte Keccak state into 2MB scratchpad
__global__ void kernel_expand_scratchpad(
	const size_t MEMORY,
	int32_t* state_buffer_in,
	int* scratchpad_in);

/// Phase 3 Kernel: GPU floating-point computation loop
__global__ void kernel_gpu_compute(
	const uint32_t ITERATIONS,
	const size_t MEMORY,
	const uint32_t MASK,
	int32_t* state_buffer,
	int* scratchpad_in,
	int bfactor,
	int partidx,
	uint32_t* roundVs,
	uint32_t* roundS);

} // namespace cuda
} // namespace n0s
