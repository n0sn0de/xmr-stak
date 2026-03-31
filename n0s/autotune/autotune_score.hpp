#pragma once

#include "autotune_types.hpp"

namespace n0s
{
namespace autotune
{

/// Compute a score for a candidate based on benchmark metrics.
///
/// Scoring philosophy: steady accepted throughput with strict stability rejection.
/// - Invalid results or backend errors incur severe penalties
/// - High variance (CV) indicates instability → penalty
/// - Zero hashrate or failed candidates get score 0
inline CandidateScore computeScore(const BenchmarkMetrics& metrics, TuneTarget target)
{
	CandidateScore score;
	score.raw_hashrate = metrics.avg_hashrate;

	// Failed to produce any results
	if(metrics.avg_hashrate <= 0.0 || metrics.valid_results == 0)
	{
		score.final_score = 0.0;
		return score;
	}

	double base = metrics.avg_hashrate;

	// Stability penalty: high CV means unreliable hashrate
	// CV < 3% = no penalty, 3-10% = linear penalty up to 20%, >10% = severe
	if(metrics.cv_percent > 10.0)
		score.stability_penalty = base * 0.5;
	else if(metrics.cv_percent > 3.0)
		score.stability_penalty = base * 0.2 * ((metrics.cv_percent - 3.0) / 7.0);

	// Error penalty: any invalid results or backend errors
	uint32_t total = metrics.valid_results + metrics.invalid_results;
	if(total > 0 && metrics.invalid_results > 0)
	{
		double error_rate = static_cast<double>(metrics.invalid_results) / total;
		// Even 1% error rate = 50% penalty; >5% = reject entirely
		if(error_rate > 0.05)
			score.error_penalty = base; // Total wipeout
		else
			score.error_penalty = base * error_rate * 10.0;
	}

	if(metrics.backend_errors > 0)
		score.error_penalty += base * 0.3; // 30% penalty for any backend errors

	score.final_score = base - score.stability_penalty - score.error_penalty;
	if(score.final_score < 0.0)
		score.final_score = 0.0;

	// Efficiency mode: divide by power if available
	if(target == TuneTarget::Efficiency && metrics.power_watts > 0.0)
		score.final_score /= metrics.power_watts;
	else if(target == TuneTarget::Balanced && metrics.power_watts > 0.0)
		score.final_score = score.final_score * 0.7 + (score.final_score / metrics.power_watts) * 0.3;

	return score;
}

/// Check if a candidate should be immediately rejected (guardrails)
inline bool shouldReject(const BenchmarkMetrics& metrics, std::string& reason)
{
	if(metrics.avg_hashrate <= 0.0)
	{
		reason = "zero hashrate";
		return true;
	}
	if(metrics.valid_results == 0)
	{
		reason = "no valid results produced";
		return true;
	}
	uint32_t total = metrics.valid_results + metrics.invalid_results;
	if(total > 0)
	{
		double error_rate = static_cast<double>(metrics.invalid_results) / total;
		if(error_rate > 0.05)
		{
			reason = "error rate " + std::to_string(error_rate * 100.0) + "% exceeds 5% threshold";
			return true;
		}
	}
	if(metrics.backend_errors > 3)
	{
		reason = "too many backend errors (" + std::to_string(metrics.backend_errors) + ")";
		return true;
	}
	if(metrics.cv_percent > 25.0)
	{
		reason = "hashrate CV " + std::to_string(metrics.cv_percent) + "% exceeds 25% threshold";
		return true;
	}
	return false;
}

} // namespace autotune
} // namespace n0s
