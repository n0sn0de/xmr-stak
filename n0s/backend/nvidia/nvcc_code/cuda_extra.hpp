#pragma once

/**
 * cuda_extra.hpp — CUDA utility macros, compatibility shims, and error checking
 *
 * Consolidated header containing:
 *   - CUDA version compatibility shims (was cuda_compat.hpp)
 *   - CUDA error checking macros (was cuda_device.hpp)
 *   - Bit manipulation macros (ROTL/ROTR, BYTE_x)
 *   - Memory operation macros (MEMSET/MEMCPY/XOR_BLOCKS)
 *   - Algorithm constants (AES_BLOCK_SIZE, etc.)
 */

#include <cuda_runtime.h>
#include <iostream>
#include <stdexcept>
#include <string>

#include "n0s/backend/cryptonight.hpp"

// ============================================================
// CUDA Version Compatibility Shims (was cuda_compat.hpp)
//
// CUDA 12.x removed legacy shorthand intrinsics that were
// available in CUDA 7-11. These provide version-gated fallbacks.
// ============================================================

#if CUDART_VERSION >= 12000
#ifndef int2float
#define int2float(x) __int2float_rn(x)
#endif
#ifndef float_as_int
#define float_as_int(x) __float_as_int(x)
#endif
#ifndef int_as_float
#define int_as_float(x) __int_as_float(x)
#endif
#endif // CUDART_VERSION >= 12000

// ============================================================
// CUDA Error Checking Macros (was cuda_device.hpp)
// ============================================================

/** Execute and check a CUDA API command */
#define CUDA_CHECK_MSG(id, msg, ...)                                                                          \
	{                                                                                                         \
		cudaError_t error = __VA_ARGS__;                                                                      \
		if(error != cudaSuccess)                                                                              \
		{                                                                                                     \
			std::cerr << "[CUDA] Error gpu " << id << ": <" << __FILE__ << ">:" << __LINE__;                  \
			std::cerr << msg << std::endl;                                                                    \
			throw std::runtime_error(std::string("[CUDA] Error: ") + std::string(cudaGetErrorString(error))); \
		}                                                                                                     \
	}                                                                                                         \
	((void)0)

#define CU_CHECK(id, ...)                                                                                                                                   \
	{                                                                                                                                                       \
		CUresult result = __VA_ARGS__;                                                                                                                      \
		if(result != CUDA_SUCCESS)                                                                                                                          \
		{                                                                                                                                                   \
			const char* s;                                                                                                                                  \
			cuGetErrorString(result, &s);                                                                                                                   \
			std::cerr << "[CUDA] Error gpu " << id << ": <" << __FUNCTION__ << ">:" << __LINE__ << " \"" << (s ? s : "unknown error") << "\"" << std::endl; \
			throw std::runtime_error(std::string("[CUDA] Error: ") + std::string(s ? s : "unknown error"));                                                 \
		}                                                                                                                                                   \
	}                                                                                                                                                       \
	((void)0)

#define CUDA_CHECK(id, ...) CUDA_CHECK_MSG(id, "", __VA_ARGS__)

/** Execute and check a CUDA kernel launch */
#define CUDA_CHECK_KERNEL(id, ...) \
	__VA_ARGS__;                   \
	CUDA_CHECK(id, cudaGetLastError())

#define CUDA_CHECK_MSG_KERNEL(id, msg, ...) \
	__VA_ARGS__;                            \
	CUDA_CHECK_MSG(id, msg, cudaGetLastError())

// ============================================================
// Algorithm Constants
// ============================================================

#define AES_BLOCK_SIZE 16
#define AES_KEY_SIZE 32
#define INIT_SIZE_BLK 8
#define INIT_SIZE_BYTE (INIT_SIZE_BLK * AES_BLOCK_SIZE) // 128 B

#define C32(x) ((uint32_t)(x##U))
#define T32(x) ((x)&C32(0xFFFFFFFF))

// ============================================================
// Bit rotation intrinsics (sm_60+ always has funnel shift + byte perm)
// ============================================================

__forceinline__ __device__ uint64_t cuda_ROTL64(const uint64_t value, const int offset)
{
	uint2 result;
	if(offset >= 32)
	{
		asm("shf.l.wrap.b32 %0, %1, %2, %3;"
			: "=r"(result.x)
			: "r"(__double2loint(__longlong_as_double(value))), "r"(__double2hiint(__longlong_as_double(value))), "r"(offset));
		asm("shf.l.wrap.b32 %0, %1, %2, %3;"
			: "=r"(result.y)
			: "r"(__double2hiint(__longlong_as_double(value))), "r"(__double2loint(__longlong_as_double(value))), "r"(offset));
	}
	else
	{
		asm("shf.l.wrap.b32 %0, %1, %2, %3;"
			: "=r"(result.x)
			: "r"(__double2hiint(__longlong_as_double(value))), "r"(__double2loint(__longlong_as_double(value))), "r"(offset));
		asm("shf.l.wrap.b32 %0, %1, %2, %3;"
			: "=r"(result.y)
			: "r"(__double2loint(__longlong_as_double(value))), "r"(__double2hiint(__longlong_as_double(value))), "r"(offset));
	}
	return __double_as_longlong(__hiloint2double(result.y, result.x));
}

#define ROTL64(x, n) (cuda_ROTL64(x, n))

#define ROTL32(x, n) __funnelshift_l((x), (x), (n))
#define ROTR32(x, n) __funnelshift_r((x), (x), (n))

#define BYTE_0(x) __byte_perm(x, 0u, 0x4440)
#define BYTE_1(x) __byte_perm(x, 0u, 0x4441)
#define BYTE_2(x) __byte_perm(x, 0u, 0x4442)
#define BYTE_3(x) __byte_perm(x, 0u, 0x4443)

#define ROTL32_8(x) __byte_perm(x, x, 0x2103)
#define ROTL32_16(x) __byte_perm(x, x, 0x1032)
#define ROTL32_24(x) __byte_perm(x, x, 0x0321)

#define MEMSET8(dst, what, cnt)                          \
	{                                                    \
		int i_memset8;                                   \
		uint64_t* out_memset8 = (uint64_t*)(dst);        \
		for(i_memset8 = 0; i_memset8 < cnt; i_memset8++) \
			out_memset8[i_memset8] = (what);             \
	}

#define MEMSET4(dst, what, cnt)                          \
	{                                                    \
		int i_memset4;                                   \
		uint32_t* out_memset4 = (uint32_t*)(dst);        \
		for(i_memset4 = 0; i_memset4 < cnt; i_memset4++) \
			out_memset4[i_memset4] = (what);             \
	}

#define MEMCPY8(dst, src, cnt)                              \
	{                                                       \
		int i_memcpy8;                                      \
		uint64_t* in_memcpy8 = (uint64_t*)(src);            \
		uint64_t* out_memcpy8 = (uint64_t*)(dst);           \
		for(i_memcpy8 = 0; i_memcpy8 < cnt; i_memcpy8++)    \
			out_memcpy8[i_memcpy8] = in_memcpy8[i_memcpy8]; \
	}

#define MEMCPY4(dst, src, cnt)                              \
	{                                                       \
		int i_memcpy4;                                      \
		uint32_t* in_memcpy4 = (uint32_t*)(src);            \
		uint32_t* out_memcpy4 = (uint32_t*)(dst);           \
		for(i_memcpy4 = 0; i_memcpy4 < cnt; i_memcpy4++)    \
			out_memcpy4[i_memcpy4] = in_memcpy4[i_memcpy4]; \
	}

#define XOR_BLOCKS(a, b)                        \
	{                                           \
		((uint64_t*)a)[0] ^= ((uint64_t*)b)[0]; \
		((uint64_t*)a)[1] ^= ((uint64_t*)b)[1]; \
	}

#define XOR_BLOCKS_DST(x, y, z)                                        \
	{                                                                  \
		((uint64_t*)z)[0] = ((uint64_t*)(x))[0] ^ ((uint64_t*)(y))[0]; \
		((uint64_t*)z)[1] = ((uint64_t*)(x))[1] ^ ((uint64_t*)(y))[1]; \
	}

#define MUL_SUM_XOR_DST(a, c, dst)                                                                           \
	{                                                                                                        \
		const uint64_t dst0 = ((uint64_t*)dst)[0];                                                           \
		uint64_t hi, lo = cuda_mul128(((uint64_t*)a)[0], dst0, &hi) + ((uint64_t*)c)[1];                     \
		hi += ((uint64_t*)c)[0];                                                                             \
		((uint64_t*)c)[0] = dst0 ^ hi;                                                                       \
		((uint64_t*)dst)[0] = hi;                                                                            \
		((uint64_t*)c)[1] = atomicExch(((unsigned long long int*)dst) + 1, (unsigned long long int)lo) ^ lo; \
	}

#define E2I(x) ((size_t)(((*((uint64_t*)(x)) >> 4) & 0x1ffff)))
