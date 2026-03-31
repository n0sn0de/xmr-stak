#pragma once

#include "autotune_types.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace n0s
{
namespace autotune
{

/// Generate AMD/OpenCL candidate parameter sets for a device.
///
/// @param compute_units  Number of CUs on the device
/// @param vram_bytes     Available VRAM
/// @param max_workgroup  OpenCL max workgroup size
/// @param mode           Tuning mode (controls search granularity)
/// @return Vector of AMD candidates sorted by expected quality (best first)
inline std::vector<AmdCandidate> generateAmdCandidates(
	uint32_t compute_units,
	uint64_t vram_bytes,
	size_t max_workgroup,
	TuneMode mode)
{
	std::vector<AmdCandidate> candidates;

	// CryptoNight-GPU memory per thread: 2 MiB scratchpad + 240 bytes metadata
	constexpr size_t mem_per_thread = (2u * 1024u * 1024u) + 240u;
	constexpr size_t min_free_mem = 128u * 1024u * 1024u; // Keep 128 MiB free

	uint64_t usable_vram = (vram_bytes > min_free_mem) ? (vram_bytes - min_free_mem) : 0;
	size_t max_intensity = usable_vram / mem_per_thread;
	if(max_intensity == 0) return candidates;

	// Worksize candidates: must be power of 2, <= max_workgroup
	// For cn_gpu, 8 and 16 are the known-good values
	std::vector<size_t> worksizes;
	for(size_t ws : {8u, 16u, 32u})
	{
		if(ws <= max_workgroup)
			worksizes.push_back(ws);
	}
	if(worksizes.empty())
		worksizes.push_back(8);

	// Intensity candidates: multiples of (worksize * compute_units)
	// This ensures full CU utilization
	auto addCandidate = [&](size_t intensity, size_t worksize) {
		if(intensity > 0 && intensity <= max_intensity && worksize > 0)
			candidates.push_back({intensity, worksize});
	};

	for(size_t ws : worksizes)
	{
		size_t unit = ws * compute_units;
		if(unit == 0) continue;

		// Base: what autoAdjust currently picks (6 waves * 8 threads * CUs for cn_gpu)
		size_t base_intensity = compute_units * 6 * 8;
		base_intensity = (base_intensity / ws) * ws; // Align to worksize

		if(mode == TuneMode::Quick)
		{
			// Just test around the default ±25%
			addCandidate(base_intensity, ws);
			addCandidate((base_intensity * 3) / 4, ws);
			addCandidate((base_intensity * 5) / 4, ws);
		}
		else
		{
			// Sweep: 50% to 150% of base in steps, plus CU-aligned values
			size_t lo = std::max(unit, base_intensity / 2);
			size_t hi = std::min(max_intensity, base_intensity * 3 / 2);
			size_t step = (mode == TuneMode::Exhaustive) ? unit : unit * 2;
			if(step == 0) step = 1;

			for(size_t i = lo; i <= hi; i += step)
			{
				size_t aligned = (i / ws) * ws;
				if(aligned > 0)
					addCandidate(aligned, ws);
			}
			// Always include the base value
			addCandidate(base_intensity, ws);
		}
	}

	// Deduplicate
	std::sort(candidates.begin(), candidates.end(), [](const AmdCandidate& a, const AmdCandidate& b) {
		return (a.intensity != b.intensity) ? (a.intensity < b.intensity) : (a.worksize < b.worksize);
	});
	candidates.erase(
		std::unique(candidates.begin(), candidates.end(), [](const AmdCandidate& a, const AmdCandidate& b) {
			return a.intensity == b.intensity && a.worksize == b.worksize;
		}),
		candidates.end());

	return candidates;
}

/// Generate NVIDIA/CUDA candidate parameter sets for a device.
///
/// CryptoNight-GPU specific constraints:
///   - `threads` is the number of thread groups per block (each group = 16 CUDA threads)
///   - The kernel uses __launch_bounds__(128, 8) so 128 threads/block = 8 groups is ideal
///   - `blocks` controls total parallelism: total_hashes = threads × blocks
///   - `blocks` is bounded by SM_count × occupancy_mult and available VRAM
///   - Each hash needs ~2 MiB scratchpad + 16 KiB local + 680 bytes metadata
///
/// @param sm_count       Number of SMs on the device
/// @param vram_bytes     Available VRAM
/// @param compute_cap    Compute capability (e.g., 61 for Pascal, 75 for Turing)
/// @param mode           Tuning mode
/// @return Vector of NVIDIA candidates
inline std::vector<NvidiaCandidate> generateNvidiaCandidates(
	uint32_t sm_count,
	uint64_t vram_bytes,
	uint32_t compute_cap,
	TuneMode mode)
{
	std::vector<NvidiaCandidate> candidates;

	// cn_gpu memory per hash: 2 MiB scratchpad + 16 KiB local mem + 680 bytes metadata
	constexpr size_t hash_mem = (2u * 1024u * 1024u) + 16192u + 680u;
	constexpr size_t min_free_mem = 128u * 1024u * 1024u;

	uint64_t usable_vram = (vram_bytes > min_free_mem) ? (vram_bytes - min_free_mem) : 0;
	size_t max_total_hashes = usable_vram / hash_mem;
	if(max_total_hashes == 0 || sm_count == 0) return candidates;

	// cn_gpu threads: The kernel is designed for 8 thread groups per block
	// (8 groups × 16 threads/hash = 128 threads/block, matching __launch_bounds__)
	// Values other than 8 crash or severely underperform.
	constexpr uint32_t cn_gpu_threads = 8;

	// Block multipliers: blocks = SM_count × multiplier
	// Architecture-optimal ranges from CUDA init code:
	//   Pascal (sm_6x): 7 × SM_count is optimal
	//   Turing+ (sm_7x+): 6 × SM_count is optimal
	// We sweep around these values to find the true best.
	uint32_t arch_optimal_mult = (compute_cap >= 70) ? 6 : 7;

	std::vector<uint32_t> block_multipliers;
	if(mode == TuneMode::Quick)
	{
		// 3 candidates: optimal ± 1
		block_multipliers = {arch_optimal_mult - 1, arch_optimal_mult, arch_optimal_mult + 1};
	}
	else if(mode == TuneMode::Balanced)
	{
		// Sweep from arch_optimal - 2 to arch_optimal + 3
		for(uint32_t m = std::max(2u, arch_optimal_mult - 2); m <= arch_optimal_mult + 3; ++m)
			block_multipliers.push_back(m);
	}
	else // Exhaustive
	{
		// Full sweep 2..12
		for(uint32_t m = 2; m <= 12; ++m)
			block_multipliers.push_back(m);
	}

	// bfactor: 0 is best for dedicated mining
	std::vector<uint32_t> bfactors = {0};
	if(mode == TuneMode::Exhaustive)
		bfactors = {0, 6, 8};

	for(uint32_t mult : block_multipliers)
	{
		uint32_t blocks = sm_count * mult;
		size_t total_hashes = static_cast<size_t>(cn_gpu_threads) * blocks;
		if(total_hashes > max_total_hashes || total_hashes == 0) continue;

		for(uint32_t bf : bfactors)
			candidates.push_back({cn_gpu_threads, blocks, bf});
	}

	// Deduplicate (shouldn't be needed but safety first)
	std::sort(candidates.begin(), candidates.end(), [](const NvidiaCandidate& a, const NvidiaCandidate& b) {
		if(a.threads != b.threads) return a.threads < b.threads;
		if(a.blocks != b.blocks) return a.blocks < b.blocks;
		return a.bfactor < b.bfactor;
	});
	candidates.erase(
		std::unique(candidates.begin(), candidates.end(), [](const NvidiaCandidate& a, const NvidiaCandidate& b) {
			return a.threads == b.threads && a.blocks == b.blocks && a.bfactor == b.bfactor;
		}),
		candidates.end());

	return candidates;
}

} // namespace autotune
} // namespace n0s
