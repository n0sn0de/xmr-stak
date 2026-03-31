#pragma once

#include "autotune_types.hpp"

#include <functional>
#include <set>
#include <string>

namespace n0s
{
namespace autotune
{

/// Configuration for the autotune manager
struct AutotuneConfig
{
	TuneMode mode = TuneMode::Balanced;
	TuneTarget target = TuneTarget::Hashrate;
	std::string backend_filter = "all"; // "amd", "nvidia", or "all"
	std::set<uint32_t> gpu_indices;     // Empty = all GPUs
	bool reset = false;                 // Ignore cached results
	bool resume = false;                // Resume interrupted run
	int benchmark_seconds = 30;         // Per-candidate benchmark duration
	int stability_seconds = 60;         // Stability validation for finalists
	std::string export_path;            // Optional export path
	std::string autotune_file = "autotune.json"; // Persistence file
};

/// Callback for evaluating a single candidate on a device.
/// The implementation is backend-specific (AMD or NVIDIA).
///
/// @param device_index  GPU index
/// @param candidate     The candidate record (with parameters set)
/// @param benchmark_sec Duration to benchmark
/// @param metrics       [out] Filled with benchmark results
/// @return true if the benchmark ran successfully (even if hashrate was bad)
using CandidateEvaluator = std::function<bool(
	uint32_t device_index,
	const CandidateRecord& candidate,
	int benchmark_seconds,
	BenchmarkMetrics& metrics)>;

/// Callback for collecting a device fingerprint
/// @param device_index  GPU index
/// @param fingerprint   [out] Filled with device info
/// @return true on success
using FingerprintCollector = std::function<bool(
	uint32_t device_index,
	DeviceFingerprint& fingerprint)>;

/// Main autotune orchestrator.
///
/// This class manages the full autotune lifecycle:
///   1. Device discovery and fingerprinting
///   2. Candidate generation
///   3. Coarse search → refinement → stability validation
///   4. Scoring and winner selection
///   5. Persistence to autotune.json
///
/// Backend-specific work is delegated via callbacks (CandidateEvaluator,
/// FingerprintCollector) that the AMD and NVIDIA backends provide.
class AutotuneManager
{
public:
	AutotuneManager(const AutotuneConfig& config);

	/// Run the full autotune workflow for a single device.
	/// @param device_index  GPU to tune
	/// @param fingerprint   Pre-collected device fingerprint
	/// @param evaluator     Backend-specific benchmark callback
	/// @return The best candidate found, or nullptr if tuning failed
	const CandidateRecord* tuneDevice(
		uint32_t device_index,
		const DeviceFingerprint& fingerprint,
		CandidateEvaluator evaluator);

	/// Get the current autotune result (for persistence or inspection)
	const AutotuneResult& getResult() const { return result_; }

	/// Get mutable result for adding device states
	AutotuneResult& getResult() { return result_; }

private:
	/// Run coarse search phase
	void coarseSearch(AutotuneState& state, CandidateEvaluator& evaluator);

	/// Run refinement around top N candidates
	void refineSearch(AutotuneState& state, CandidateEvaluator& evaluator);

	/// Run stability validation on the best candidate
	bool stabilityValidation(AutotuneState& state, CandidateEvaluator& evaluator);

	/// Select the best candidate from evaluated results
	int32_t selectBest(const AutotuneState& state) const;

	AutotuneConfig config_;
	AutotuneResult result_;
};

} // namespace autotune
} // namespace n0s
