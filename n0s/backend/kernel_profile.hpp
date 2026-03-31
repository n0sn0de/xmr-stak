/**
 * kernel_profile.hpp — Per-kernel timing for profiling GPU backends
 *
 * Shared between OpenCL and CUDA backends. Accumulates per-phase
 * timing data over multiple dispatch calls for averaging.
 */
#pragma once

#include <cstdint>
#include <cstdio>

namespace n0s
{

struct KernelProfile
{
	int64_t phase1_us = 0;   ///< Phase 1: Keccak prepare (microseconds)
	int64_t phase2_us = 0;   ///< Phase 2: Scratchpad expand
	int64_t phase3_us = 0;   ///< Phase 3: GPU compute (main loop)
	int64_t phase4_us = 0;   ///< Phase 4: Implode (AES + mix_and_propagate)
	int64_t phase5_us = 0;   ///< Phase 5: Finalize (16×AES + Keccak + target check)
	int64_t phase45_us = 0;  ///< Phase 4+5 combined (for backward compat)
	int64_t total_us = 0;    ///< Total dispatch time
	int iterations = 0;      ///< Number of dispatch calls profiled

	void print_summary(const char* backend_name, size_t intensity) const
	{
		if(iterations == 0) return;

		auto avg = [&](int64_t val) -> double { return static_cast<double>(val) / iterations; };
		double total_avg = avg(total_us);

		auto pct = [&](int64_t val) -> double {
			return total_avg > 0.0 ? (avg(val) / total_avg) * 100.0 : 0.0;
		};

		printf("\n=== %s Kernel Profile (%d dispatches, intensity=%zu) ===\n",
			backend_name, iterations, intensity);
		printf("  Phase 1 (Keccak prepare):    %10.0f µs  (%5.1f%%)\n", avg(phase1_us), pct(phase1_us));
		printf("  Phase 2 (Scratchpad expand): %10.0f µs  (%5.1f%%)\n", avg(phase2_us), pct(phase2_us));
		printf("  Phase 3 (GPU compute):       %10.0f µs  (%5.1f%%)\n", avg(phase3_us), pct(phase3_us));
		printf("  Phase 4 (Implode):           %10.0f µs  (%5.1f%%)\n", avg(phase4_us), pct(phase4_us));
		printf("  Phase 5 (Finalize):          %10.0f µs  (%5.1f%%)\n", avg(phase5_us), pct(phase5_us));
		printf("  Phase 4+5 combined:          %10.0f µs  (%5.1f%%)\n", avg(phase45_us), pct(phase45_us));
		printf("  Total per dispatch:          %10.0f µs\n", total_avg);
		printf("  Estimated H/s:               %10.1f\n",
			total_avg > 0.0 ? (intensity * 1e6) / total_avg : 0.0);
		printf("==========================================================\n\n");
	}

	void reset() { *this = KernelProfile{}; }
};

} // namespace n0s
