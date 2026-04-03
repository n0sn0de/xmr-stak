#pragma once

#include "n0s/backend/cryptonight.hpp"
#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#else
#include <cpuid.h>
#include <x86intrin.h>
#endif

inline void cngpu_cpuid(uint32_t eax, int32_t ecx, int32_t val[4])
{
	val[0] = 0;
	val[1] = 0;
	val[2] = 0;
	val[3] = 0;

#ifdef _MSC_VER
	__cpuidex(val, static_cast<int>(eax), static_cast<int>(ecx));
#else
	__cpuid_count(eax, ecx, val[0], val[1], val[2], val[3]);
#endif
}

inline bool cngpu_check_avx2()
{
	int32_t cpu_info[4];
	cngpu_cpuid(7, 0, cpu_info);
	return (cpu_info[1] & (1 << 5)) != 0;
}

void cn_gpu_inner_avx(const uint8_t* spad, uint8_t* lpad, const n0s_algo& algo);

void cn_gpu_inner_ssse3(const uint8_t* spad, uint8_t* lpad, const n0s_algo& algo);
