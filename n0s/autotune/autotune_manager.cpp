#include "autotune_manager.hpp"

#include "autotune_candidates.hpp"
#include "autotune_persist.hpp"
#include "autotune_score.hpp"

#include "n0s/misc/console.hpp"
#include "n0s/version.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace n0s
{
namespace autotune
{

namespace
{

std::string nowISO8601()
{
	auto now = std::chrono::system_clock::now();
	auto t = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
	gmtime_r(&t, &tm);
	std::ostringstream ss;
	ss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
	return ss.str();
}

const char* modeToString(TuneMode m)
{
	switch(m) {
	case TuneMode::Quick: return "quick";
	case TuneMode::Balanced: return "balanced";
	case TuneMode::Exhaustive: return "exhaustive";
	}
	return "balanced";
}

} // anonymous namespace

AutotuneManager::AutotuneManager(const AutotuneConfig& config)
	: config_(config)
{
	result_.miner_version = get_version_str();
	result_.timestamp = nowISO8601();

	// Try to load existing results for resume/cache
	if(!config_.reset)
		loadAutotuneResult(result_, config_.autotune_file);
}

const CandidateRecord* AutotuneManager::tuneDevice(
	uint32_t device_index,
	const DeviceFingerprint& fingerprint,
	CandidateEvaluator evaluator)
{
	// Check cache unless reset
	if(!config_.reset)
	{
		const auto* cached = findCachedState(result_, fingerprint);
		if(cached && cached->completed && !config_.resume)
		{
			printer::inst()->print_msg(L0,
				"AUTOTUNE: GPU %u — using cached result (score %.1f H/s)",
				device_index,
				cached->best_candidate_id >= 0 ?
					cached->candidates[cached->best_candidate_id].score.final_score : 0.0);
			return cached->best_candidate_id >= 0 ?
				&cached->candidates[cached->best_candidate_id] : nullptr;
		}
	}

	printer::inst()->print_msg(L0,
		"AUTOTUNE: GPU %u [%s] — starting %s mode tuning",
		device_index, fingerprint.gpu_name.c_str(), modeToString(config_.mode));

	auto start = std::chrono::steady_clock::now();

	// Create state for this device
	AutotuneState state;
	state.device_index = device_index;
	state.fingerprint = fingerprint;
	state.mode = config_.mode;
	state.target = config_.target;
	state.timestamp = nowISO8601();

	// Generate candidates based on backend
	if(fingerprint.backend == BackendType::OpenCL)
	{
		auto amd_candidates = generateAmdCandidates(
			fingerprint.compute_units,
			fingerprint.vram_bytes,
			256, // Default max workgroup; real value comes from device query
			config_.mode);

		uint32_t id = 0;
		for(const auto& ac : amd_candidates)
		{
			CandidateRecord rec;
			rec.candidate_id = id++;
			rec.amd = ac;
			state.candidates.push_back(rec);
		}

		printer::inst()->print_msg(L0,
			"AUTOTUNE: GPU %u — generated %zu OpenCL candidates (intensity range: %zu-%zu, worksizes: varied)",
			device_index, state.candidates.size(),
			amd_candidates.empty() ? 0 : amd_candidates.front().intensity,
			amd_candidates.empty() ? 0 : amd_candidates.back().intensity);
	}
	else
	{
		uint32_t compute_cap = 0;
		// Parse architecture string like "sm_61"
		if(fingerprint.gpu_architecture.find("sm_") == 0)
			compute_cap = std::stoul(fingerprint.gpu_architecture.substr(3));

		auto nv_candidates = generateNvidiaCandidates(
			fingerprint.compute_units,
			fingerprint.vram_bytes,
			compute_cap,
			config_.mode);

		uint32_t id = 0;
		for(const auto& nc : nv_candidates)
		{
			CandidateRecord rec;
			rec.candidate_id = id++;
			rec.nvidia = nc;
			state.candidates.push_back(rec);
		}

		printer::inst()->print_msg(L0,
			"AUTOTUNE: GPU %u — generated %zu CUDA candidates",
			device_index, state.candidates.size());
	}

	if(state.candidates.empty())
	{
		printer::inst()->print_msg(L0, "AUTOTUNE: GPU %u — no candidates generated!", device_index);
		return nullptr;
	}

	// Phase 1: Coarse search — evaluate all candidates
	coarseSearch(state, evaluator);

	// Phase 2: Refine around top candidates
	// (For v1, coarse search covers the full candidate set already)
	// refineSearch(state, evaluator);

	// Phase 3: Stability validation on the best
	state.best_candidate_id = selectBest(state);
	if(state.best_candidate_id >= 0)
		stabilityValidation(state, evaluator);

	// Finalize
	auto elapsed = std::chrono::steady_clock::now() - start;
	state.total_elapsed_seconds = std::chrono::duration<double>(elapsed).count();
	state.completed = (state.best_candidate_id >= 0);

	// Replace or append device state in result
	bool found = false;
	for(auto& dev : result_.devices)
	{
		if(dev.fingerprint.isCompatible(fingerprint))
		{
			dev = state;
			found = true;
			break;
		}
	}
	if(!found)
		result_.devices.push_back(state);

	// Persist
	result_.timestamp = nowISO8601();
	saveAutotuneResult(result_, config_.autotune_file);

	if(state.best_candidate_id >= 0)
	{
		const auto& best = state.candidates[state.best_candidate_id];
		if(fingerprint.backend == BackendType::OpenCL)
		{
			printer::inst()->print_msg(L0,
				"AUTOTUNE: GPU %u — WINNER: intensity=%zu worksize=%zu (%.1f H/s, score=%.1f)",
				device_index, best.amd.intensity, best.amd.worksize,
				best.metrics.avg_hashrate, best.score.final_score);
		}
		else
		{
			printer::inst()->print_msg(L0,
				"AUTOTUNE: GPU %u — WINNER: threads=%u blocks=%u bfactor=%u (%.1f H/s, score=%.1f)",
				device_index, best.nvidia.threads, best.nvidia.blocks, best.nvidia.bfactor,
				best.metrics.avg_hashrate, best.score.final_score);
		}

		printer::inst()->print_msg(L0,
			"AUTOTUNE: GPU %u — completed in %.1f seconds, tested %zu candidates",
			device_index, state.total_elapsed_seconds, state.candidates.size());

		return &result_.devices.back().candidates[state.best_candidate_id];
	}

	printer::inst()->print_msg(L0, "AUTOTUNE: GPU %u — no valid candidate found!", device_index);
	return nullptr;
}

void AutotuneManager::coarseSearch(AutotuneState& state, CandidateEvaluator& evaluator)
{
	size_t total = state.candidates.size();
	size_t evaluated = 0;

	for(auto& candidate : state.candidates)
	{
		if(candidate.status != CandidateStatus::Pending)
			continue;

		evaluated++;
		candidate.status = CandidateStatus::Running;

		if(state.fingerprint.backend == BackendType::OpenCL)
		{
			printer::inst()->print_msg(L1,
				"AUTOTUNE: [%zu/%zu] Testing intensity=%zu worksize=%zu ...",
				evaluated, total, candidate.amd.intensity, candidate.amd.worksize);
		}
		else
		{
			printer::inst()->print_msg(L1,
				"AUTOTUNE: [%zu/%zu] Testing threads=%u blocks=%u bfactor=%u ...",
				evaluated, total, candidate.nvidia.threads, candidate.nvidia.blocks, candidate.nvidia.bfactor);
		}

		BenchmarkMetrics metrics;
		bool ran = evaluator(state.device_index, candidate, config_.benchmark_seconds, metrics);
		candidate.metrics = metrics;

		if(!ran)
		{
			candidate.status = CandidateStatus::Failed;
			candidate.reject_reason = "benchmark execution failed";
			printer::inst()->print_msg(L1, "AUTOTUNE: [%zu/%zu] FAILED — benchmark did not complete", evaluated, total);
			continue;
		}

		// Check guardrails
		std::string reject_reason;
		if(shouldReject(metrics, reject_reason))
		{
			candidate.status = CandidateStatus::Rejected;
			candidate.reject_reason = reject_reason;
			printer::inst()->print_msg(L1, "AUTOTUNE: [%zu/%zu] REJECTED — %s", evaluated, total, reject_reason.c_str());
			continue;
		}

		candidate.score = computeScore(metrics, state.target);
		candidate.status = CandidateStatus::Success;

		printer::inst()->print_msg(L1,
			"AUTOTUNE: [%zu/%zu] OK — %.1f H/s (CV=%.1f%%, score=%.1f)",
			evaluated, total, metrics.avg_hashrate, metrics.cv_percent, candidate.score.final_score);
	}
}

void AutotuneManager::refineSearch(AutotuneState& state, CandidateEvaluator& evaluator)
{
	// TODO: In v1.1, generate refined candidates around the top N from coarse search
	// For now, coarse search covers the full candidate set
	(void)state;
	(void)evaluator;
}

bool AutotuneManager::stabilityValidation(AutotuneState& state, CandidateEvaluator& evaluator)
{
	if(state.best_candidate_id < 0) return false;

	auto& best = state.candidates[state.best_candidate_id];
	printer::inst()->print_msg(L0,
		"AUTOTUNE: GPU %u — stability validation (%d seconds) on best candidate...",
		state.device_index, config_.stability_seconds);

	BenchmarkMetrics stability_metrics;
	bool ran = evaluator(state.device_index, best, config_.stability_seconds, stability_metrics);

	if(!ran)
	{
		printer::inst()->print_msg(L0,
			"AUTOTUNE: GPU %u — stability validation FAILED (benchmark error)", state.device_index);
		best.status = CandidateStatus::Failed;
		best.reject_reason = "stability validation failed";
		// Try next best
		state.best_candidate_id = selectBest(state);
		return false;
	}

	// Update metrics with stability run data
	best.metrics.stability_seconds = stability_metrics.benchmark_seconds;

	std::string reject_reason;
	if(shouldReject(stability_metrics, reject_reason))
	{
		printer::inst()->print_msg(L0,
			"AUTOTUNE: GPU %u — stability validation REJECTED: %s",
			state.device_index, reject_reason.c_str());
		best.status = CandidateStatus::Rejected;
		best.reject_reason = "stability: " + reject_reason;
		state.best_candidate_id = selectBest(state);
		return false;
	}

	// Check if hashrate dropped significantly from benchmark
	double drop = (best.metrics.avg_hashrate - stability_metrics.avg_hashrate) / best.metrics.avg_hashrate;
	if(drop > 0.15) // More than 15% drop
	{
		printer::inst()->print_msg(L0,
			"AUTOTUNE: GPU %u — stability validation: hashrate dropped %.1f%% (%.1f → %.1f H/s)",
			state.device_index, drop * 100.0, best.metrics.avg_hashrate, stability_metrics.avg_hashrate);
		best.status = CandidateStatus::Rejected;
		best.reject_reason = "stability: hashrate dropped " + std::to_string(int(drop * 100)) + "%";
		state.best_candidate_id = selectBest(state);
		return false;
	}

	printer::inst()->print_msg(L0,
		"AUTOTUNE: GPU %u — stability validation PASSED (%.1f H/s sustained)",
		state.device_index, stability_metrics.avg_hashrate);
	return true;
}

int32_t AutotuneManager::selectBest(const AutotuneState& state) const
{
	int32_t best_id = -1;
	CandidateScore best_score;

	for(const auto& c : state.candidates)
	{
		if(c.status == CandidateStatus::Success && c.score > best_score)
		{
			best_score = c.score;
			best_id = static_cast<int32_t>(c.candidate_id);
		}
	}
	return best_id;
}

} // namespace autotune
} // namespace n0s
