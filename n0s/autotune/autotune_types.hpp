#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace n0s
{
namespace autotune
{

/// Tuning mode controls search depth vs time tradeoff
enum class TuneMode
{
	Quick,      // ~5 candidates, short benchmarks
	Balanced,   // ~15-25 candidates, moderate benchmarks
	Exhaustive  // Full grid search, long stability validation
};

/// Optimization target
enum class TuneTarget
{
	Hashrate,   // Maximize H/s
	Efficiency, // H/s per watt (requires power telemetry)
	Balanced    // Weighted combination
};

/// Backend type for a GPU device
enum class BackendType
{
	OpenCL,
	CUDA
};

/// Device fingerprint — uniquely identifies a GPU + runtime combination
struct DeviceFingerprint
{
	BackendType backend;
	std::string gpu_name;         // e.g., "AMD Radeon RX 9070 XT"
	std::string gpu_architecture; // e.g., "gfx1201" or "sm_61"
	uint64_t vram_bytes;
	uint32_t compute_units;       // CUs for AMD, SMs for NVIDIA
	std::string driver_version;
	std::string runtime_version;  // OpenCL version or CUDA runtime version
	std::string miner_version;    // git commit or version string
	std::string algorithm;        // "cryptonight_gpu"

	/// Check if this fingerprint is compatible with a cached result
	/// Returns true if the hardware matches (ignores miner version changes)
	bool isCompatible(const DeviceFingerprint& other) const
	{
		return backend == other.backend &&
		       gpu_name == other.gpu_name &&
		       gpu_architecture == other.gpu_architecture &&
		       vram_bytes == other.vram_bytes &&
		       compute_units == other.compute_units &&
		       driver_version == other.driver_version &&
		       runtime_version == other.runtime_version &&
		       algorithm == other.algorithm;
	}
};

/// A single set of tunable parameters for AMD/OpenCL
struct AmdCandidate
{
	size_t intensity;
	size_t worksize;
};

/// A single set of tunable parameters for NVIDIA/CUDA
struct NvidiaCandidate
{
	uint32_t threads;
	uint32_t blocks;
	uint32_t bfactor;
};

/// Result status for a candidate evaluation
enum class CandidateStatus
{
	Pending,
	Running,
	Success,
	Failed,      // Kernel error, crash, invalid results
	Rejected,    // Ran fine but didn't meet stability criteria
	Skipped      // Skipped on resume (already tested)
};

/// Metrics collected during a candidate benchmark run
struct BenchmarkMetrics
{
	double avg_hashrate = 0.0;   // H/s
	double min_hashrate = 0.0;
	double max_hashrate = 0.0;
	double cv_percent = 0.0;     // Coefficient of variation
	uint32_t valid_results = 0;
	uint32_t invalid_results = 0;
	uint32_t backend_errors = 0;
	double benchmark_seconds = 0.0;
	double stability_seconds = 0.0;
	double power_watts = 0.0;   // 0 = not measured
};

/// Score for a candidate — higher is better
struct CandidateScore
{
	double raw_hashrate = 0.0;
	double stability_penalty = 0.0;
	double error_penalty = 0.0;
	double final_score = 0.0;

	bool operator>(const CandidateScore& other) const { return final_score > other.final_score; }
	bool operator<(const CandidateScore& other) const { return final_score < other.final_score; }
};

/// A candidate evaluation record (for persistence)
struct CandidateRecord
{
	uint32_t candidate_id;
	CandidateStatus status = CandidateStatus::Pending;
	std::string reject_reason;

	// Parameters (union-like — only one set is meaningful based on backend)
	AmdCandidate amd{};
	NvidiaCandidate nvidia{};

	BenchmarkMetrics metrics{};
	CandidateScore score{};
};

/// Overall autotune state for a single device
struct AutotuneState
{
	DeviceFingerprint fingerprint;
	uint32_t device_index;
	TuneMode mode = TuneMode::Balanced;
	TuneTarget target = TuneTarget::Hashrate;

	std::vector<CandidateRecord> candidates;
	int32_t best_candidate_id = -1;
	bool completed = false;
	std::string timestamp;  // ISO 8601

	// Timing
	double total_elapsed_seconds = 0.0;
};

/// Top-level autotune result file (autotune.json)
struct AutotuneResult
{
	std::vector<AutotuneState> devices;
	std::string miner_version;
	std::string timestamp;
};

} // namespace autotune
} // namespace n0s
