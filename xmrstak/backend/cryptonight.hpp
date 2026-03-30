#pragma once
#include <inttypes.h>
#include <stddef.h>
#include <string>
#include <type_traits>

enum n0s_algo_id
{
	invalid_algo = 0,
	cryptonight_gpu = 13 // explicit value: ABI contract with OpenCL (#define) and CUDA (-DALGO=)
};

/** get name of the algorithm */
inline std::string get_algo_name(n0s_algo_id algo_id)
{
	switch(algo_id)
	{
	case cryptonight_gpu:
		return "cryptonight_gpu";
	default:
		return "invalid_algo";
	}
}

struct n0s_algo
{
	n0s_algo(n0s_algo_id name_id) :
		algo_name(name_id),
		base_algo(name_id)
	{
	}
	n0s_algo(n0s_algo_id name_id, n0s_algo_id algorithm) :
		algo_name(name_id),
		base_algo(algorithm)
	{
	}
	n0s_algo(n0s_algo_id name_id, n0s_algo_id algorithm, uint32_t iteration) :
		algo_name(name_id),
		base_algo(algorithm),
		iter(iteration)
	{
	}
	n0s_algo(n0s_algo_id name_id, n0s_algo_id algorithm, uint32_t iteration, size_t memory) :
		algo_name(name_id),
		base_algo(algorithm),
		iter(iteration),
		mem(memory)
	{
	}
	n0s_algo(n0s_algo_id name_id, n0s_algo_id algorithm, uint32_t iteration, size_t memory, uint32_t mem_mask) :
		algo_name(name_id),
		base_algo(algorithm),
		iter(iteration),
		mem(memory),
		mask(mem_mask)
	{
	}

	/** check if the algorithm is equal to another algorithm
	 *
	 * we do not check the member algo_name because this is only an alias name
	 */
	bool operator==(const n0s_algo& other) const
	{
		return other.Id() == Id() && other.Mem() == Mem() && other.Iter() == Iter() && other.Mask() == Mask();
	}

	bool operator==(const n0s_algo_id& id) const
	{
		return base_algo == id;
	}

	operator n0s_algo_id() const
	{
		return base_algo;
	}

	n0s_algo_id Id() const
	{
		return base_algo;
	}

	size_t Mem() const
	{
		if(base_algo == invalid_algo)
			return 0;
		else
			return mem;
	}

	uint32_t Iter() const
	{
		return iter;
	}

	/** Name of the algorithm
	 *
	 * This name is only an alias for the native implemented base algorithm.
	 */
	std::string Name() const
	{
		return get_algo_name(algo_name);
	}

	/** Name of the parent algorithm
	 *
	 * This is the real algorithm which is implemented in all POW functions.
	 */
	std::string BaseName() const
	{
		return get_algo_name(base_algo);
	}

	uint32_t Mask() const
	{
		// default is a 16 byte aligne mask
		if(mask == 0)
			return ((mem - 1u) / 16) * 16;
		else
			return mask;
	}

	n0s_algo_id algo_name = invalid_algo;
	n0s_algo_id base_algo = invalid_algo;
	uint32_t iter = 0u;
	size_t mem = 0u;
	uint32_t mask = 0u;
};

// CryptoNight-GPU constants
// See also: n0s/algorithm/cn_gpu.hpp for the clean standalone version
constexpr size_t CN_MEMORY = 2 * 1024 * 1024;       // 2 MiB scratchpad
constexpr uint32_t CN_GPU_ITER = 0xC000;             // 49,152 Phase 3 iterations
constexpr uint32_t CN_GPU_MASK = 0x1FFFC0;           // 64-byte aligned scratchpad address mask

inline n0s_algo POW(n0s_algo_id algo_id)
{
	if(algo_id == cryptonight_gpu)
		return {cryptonight_gpu, cryptonight_gpu, CN_GPU_ITER, CN_MEMORY, CN_GPU_MASK};
	return {invalid_algo, invalid_algo};
}
