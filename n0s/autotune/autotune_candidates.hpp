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
/// @param sm_count       Number of SMs on the device
/// @param vram_bytes     Available VRAM
/// @param compute_cap    Compute capability (e.g., 61 for Pascal, 75 for Turing)
/// @param mode           Tuning mode
/// @return Vector of NVIDIA candidates
inline std::vector<NvidiaCandidate> generateNvidiaCandidates(
	uint32_t sm_count,
	uint64_t vram_bytes,
	uint32_t compute_cap [[maybe_unused]],
	TuneMode mode)
{
	std::vector<NvidiaCandidate> candidates;

	constexpr size_t mem_per_thread = (2u * 1024u * 1024u) + 240u;
	constexpr size_t min_free_mem = 128u * 1024u * 1024u;

	uint64_t usable_vram = (vram_bytes > min_free_mem) ? (vram_bytes - min_free_mem) : 0;
	size_t max_threads_total = usable_vram / mem_per_thread;
	if(max_threads_total == 0) return candidates;

	// Thread counts: typically 8-64 per block for cn_gpu
	std::vector<uint32_t> thread_options;
	if(mode == TuneMode::Quick)
		thread_options = {8, 16, 32};
	else if(mode == TuneMode::Balanced)
		thread_options = {8, 12, 16, 24, 32, 48};
	else
		thread_options = {4, 8, 12, 16, 20, 24, 32, 40, 48, 64};

	// Block counts: typically SM_count * multiplier
	// cn_gpu uses blocks = total_threads / threads_per_block
	std::vector<uint32_t> block_multipliers;
	if(mode == TuneMode::Quick)
		block_multipliers = {3, 4, 6};
	else if(mode == TuneMode::Balanced)
		block_multipliers = {2, 3, 4, 5, 6, 8};
	else
		block_multipliers = {1, 2, 3, 4, 5, 6, 7, 8, 10, 12};

	// bfactor: 0 is best for dedicated mining, higher values yield to desktop
	std::vector<uint32_t> bfactors = {0};
	if(mode == TuneMode::Exhaustive)
		bfactors = {0, 6, 8};

	for(uint32_t threads : thread_options)
	{
		for(uint32_t mult : block_multipliers)
		{
			uint32_t blocks = sm_count * mult;
			size_t total = static_cast<size_t>(threads) * blocks;
			if(total > max_threads_total || total == 0) continue;

			for(uint32_t bf : bfactors)
				candidates.push_back({threads, blocks, bf});
		}
	}

	// Deduplicate
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
