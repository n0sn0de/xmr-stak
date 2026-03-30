#pragma once
#include <cinttypes>
#include <cstddef>
#include <string>

// ─── Algorithm identifier ─────────────────────────────────────────────
// Only CryptoNight-GPU (RYO) is supported. The numeric value is an ABI
// contract: OpenCL kernels receive it as -DALGO=13, CUDA as a compile-time
// constant.
enum n0s_algo_id
{
	invalid_algo = 0,
	cryptonight_gpu = 13
};

// ─── CryptoNight-GPU constants ────────────────────────────────────────
// See also: n0s/algorithm/cn_gpu.hpp for the clean standalone version
constexpr size_t CN_MEMORY = 2 * 1024 * 1024;  // 2 MiB scratchpad
constexpr uint32_t CN_GPU_ITER = 0xC000;        // 49,152 Phase 3 iterations
constexpr uint32_t CN_GPU_MASK = 0x1FFFC0;      // 64-byte aligned scratchpad address mask

// ─── Algorithm descriptor ─────────────────────────────────────────────
// Carries the algorithm identity + parameters needed by kernels and the
// pool protocol (ext_algo submit extension). Single-purpose for cn_gpu,
// but the struct keeps the parameter accessors so callers don't need to
// know about raw constants.
struct n0s_algo
{
	n0s_algo_id id = invalid_algo;
	uint32_t iter = 0u;
	size_t mem = 0u;
	uint32_t mask = 0u;

	/// Default: invalid algorithm
	constexpr n0s_algo() = default;

	/// Construct from an algo id (invalid or cryptonight_gpu)
	constexpr n0s_algo(n0s_algo_id algo) : id(algo) {}

	/// Full constructor with all parameters
	constexpr n0s_algo(n0s_algo_id algo, uint32_t iterations, size_t memory, uint32_t mem_mask)
		: id(algo), iter(iterations), mem(memory), mask(mem_mask) {}

	// ── Accessors ─────────────────────────────────────────────────
	n0s_algo_id Id() const { return id; }
	uint32_t Iter() const { return iter; }

	size_t Mem() const
	{
		return (id == invalid_algo) ? 0 : mem;
	}

	uint32_t Mask() const
	{
		if(mask == 0)
			return static_cast<uint32_t>(((mem - 1u) / 16) * 16);
		return mask;
	}

	std::string Name() const
	{
		return (id == cryptonight_gpu) ? "cryptonight_gpu" : "invalid_algo";
	}

	// BaseName() was identical to Name() for cn_gpu — kept as alias for
	// pool protocol compatibility (ext_algo submit uses both fields)
	std::string BaseName() const { return Name(); }

	// ── Comparison ────────────────────────────────────────────────
	bool operator==(const n0s_algo& other) const
	{
		return id == other.id && mem == other.mem && iter == other.iter && mask == other.mask;
	}

	bool operator!=(const n0s_algo& other) const { return !(*this == other); }

	bool operator==(n0s_algo_id other_id) const { return id == other_id; }
	bool operator!=(n0s_algo_id other_id) const { return id != other_id; }

	/// Implicit conversion to n0s_algo_id for switch/comparison convenience
	operator n0s_algo_id() const { return id; }
};

// ─── The one algorithm we support ─────────────────────────────────────
constexpr n0s_algo POW(n0s_algo_id algo_id)
{
	if(algo_id == cryptonight_gpu)
		return {cryptonight_gpu, CN_GPU_ITER, CN_MEMORY, CN_GPU_MASK};
	return {invalid_algo};
}
