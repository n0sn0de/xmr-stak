/// Unit tests for autotune framework components
///
/// Tests candidate generation, scoring, persistence, and the manager
/// without requiring actual GPU hardware.

#include "n0s/autotune/autotune_candidates.hpp"
#include "n0s/autotune/autotune_persist.hpp"
#include "n0s/autotune/autotune_score.hpp"
#include "n0s/autotune/autotune_types.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

using namespace n0s::autotune;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
	static void test_##name(); \
	struct test_##name##_reg { test_##name##_reg() { test_##name(); } } test_##name##_instance; \
	static void test_##name()

#define ASSERT(cond, msg) \
	do { \
		if(!(cond)) { \
			std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")" << std::endl; \
			tests_failed++; \
			return; \
		} \
	} while(0)

#define PASS(name) do { tests_passed++; std::cout << "  ✓ " << name << std::endl; } while(0)

// ─── Scoring Tests ───

TEST(score_zero_hashrate)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 0.0;
	auto s = computeScore(m, TuneTarget::Hashrate);
	ASSERT(s.final_score == 0.0, "zero hashrate should give zero score");
	PASS("score_zero_hashrate");
}

TEST(score_clean_run)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 100;
	m.cv_percent = 1.0;
	auto s = computeScore(m, TuneTarget::Hashrate);
	ASSERT(s.final_score == 1000.0, "clean run should have full score");
	ASSERT(s.stability_penalty == 0.0, "low CV should have no penalty");
	ASSERT(s.error_penalty == 0.0, "no errors should have no penalty");
	PASS("score_clean_run");
}

TEST(score_high_cv_penalty)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 100;
	m.cv_percent = 15.0;
	auto s = computeScore(m, TuneTarget::Hashrate);
	ASSERT(s.final_score < 600.0, "high CV should severely penalize");
	ASSERT(s.stability_penalty > 0.0, "high CV should have stability penalty");
	PASS("score_high_cv_penalty");
}

TEST(score_error_penalty)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 95;
	m.invalid_results = 5;
	m.cv_percent = 2.0;
	auto s = computeScore(m, TuneTarget::Hashrate);
	ASSERT(s.error_penalty > 0.0, "invalid results should incur penalty");
	ASSERT(s.final_score < 1000.0, "errors should reduce score");
	PASS("score_error_penalty");
}

TEST(score_backend_errors)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 100;
	m.cv_percent = 1.0;
	m.backend_errors = 2;
	auto s = computeScore(m, TuneTarget::Hashrate);
	ASSERT(s.error_penalty == 300.0, "backend errors should give 30% penalty");
	ASSERT(s.final_score == 700.0, "score should be reduced by backend error penalty");
	PASS("score_backend_errors");
}

// ─── Rejection Tests ───

TEST(reject_zero_hashrate)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 0.0;
	std::string reason;
	ASSERT(shouldReject(m, reason), "should reject zero hashrate");
	PASS("reject_zero_hashrate");
}

TEST(reject_high_error_rate)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 90;
	m.invalid_results = 10; // 10% error rate
	std::string reason;
	ASSERT(shouldReject(m, reason), "should reject >5% error rate");
	PASS("reject_high_error_rate");
}

TEST(reject_high_cv)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 100;
	m.cv_percent = 30.0;
	std::string reason;
	ASSERT(shouldReject(m, reason), "should reject CV > 25%");
	PASS("reject_high_cv");
}

TEST(accept_good_metrics)
{
	BenchmarkMetrics m{};
	m.avg_hashrate = 1000.0;
	m.valid_results = 100;
	m.cv_percent = 3.0;
	std::string reason;
	ASSERT(!shouldReject(m, reason), "should accept good metrics");
	PASS("accept_good_metrics");
}

// ─── Candidate Generation Tests ───

TEST(amd_candidates_quick)
{
	auto c = generateAmdCandidates(32, 8ULL * 1024 * 1024 * 1024, 256, TuneMode::Quick);
	ASSERT(!c.empty(), "should generate AMD candidates");
	for(const auto& cand : c)
	{
		ASSERT(cand.worksize > 0, "worksize must be positive");
		ASSERT(cand.intensity > 0, "intensity must be positive");
		ASSERT(cand.intensity % cand.worksize == 0, "intensity must be aligned to worksize");
	}
	PASS("amd_candidates_quick");
}

TEST(amd_candidates_balanced)
{
	auto q = generateAmdCandidates(32, 8ULL * 1024 * 1024 * 1024, 256, TuneMode::Quick);
	auto b = generateAmdCandidates(32, 8ULL * 1024 * 1024 * 1024, 256, TuneMode::Balanced);
	ASSERT(b.size() > q.size(), "balanced should generate more candidates than quick");
	PASS("amd_candidates_balanced");
}

TEST(amd_candidates_no_vram)
{
	auto c = generateAmdCandidates(32, 0, 256, TuneMode::Quick);
	ASSERT(c.empty(), "should generate no candidates with no VRAM");
	PASS("amd_candidates_no_vram");
}

TEST(nvidia_candidates_quick)
{
	auto c = generateNvidiaCandidates(15, 8ULL * 1024 * 1024 * 1024, 61, TuneMode::Quick);
	ASSERT(!c.empty(), "should generate NVIDIA candidates");
	for(const auto& cand : c)
	{
		ASSERT(cand.threads > 0, "threads must be positive");
		ASSERT(cand.blocks > 0, "blocks must be positive");
	}
	PASS("nvidia_candidates_quick");
}

TEST(nvidia_candidates_exhaustive)
{
	auto q = generateNvidiaCandidates(15, 8ULL * 1024 * 1024 * 1024, 61, TuneMode::Quick);
	auto e = generateNvidiaCandidates(15, 8ULL * 1024 * 1024 * 1024, 61, TuneMode::Exhaustive);
	ASSERT(e.size() > q.size(), "exhaustive should generate more candidates than quick");
	PASS("nvidia_candidates_exhaustive");
}

TEST(nvidia_candidates_vram_limit)
{
	// 256 MiB VRAM — should limit candidates
	auto c = generateNvidiaCandidates(15, 256ULL * 1024 * 1024, 61, TuneMode::Balanced);
	size_t max_threads = (256ULL * 1024 * 1024 - 128ULL * 1024 * 1024) / ((2u * 1024u * 1024u) + 240u);
	for(const auto& cand : c)
	{
		size_t total = static_cast<size_t>(cand.threads) * cand.blocks;
		ASSERT(total <= max_threads, "total threads should not exceed VRAM limit");
	}
	PASS("nvidia_candidates_vram_limit");
}

// ─── Fingerprint Tests ───

TEST(fingerprint_compatible)
{
	DeviceFingerprint a{BackendType::OpenCL, "RX 9070 XT", "gfx1201", 16ULL*1024*1024*1024, 32, "6.1.0", "3.0", "v1.0", "cryptonight_gpu"};
	DeviceFingerprint b = a;
	b.miner_version = "v1.1"; // Different miner version should still be compatible
	ASSERT(a.isCompatible(b), "same device with different miner version should be compatible");
	PASS("fingerprint_compatible");
}

TEST(fingerprint_incompatible_driver)
{
	DeviceFingerprint a{BackendType::OpenCL, "RX 9070 XT", "gfx1201", 16ULL*1024*1024*1024, 32, "6.1.0", "3.0", "v1.0", "cryptonight_gpu"};
	DeviceFingerprint b = a;
	b.driver_version = "6.2.0";
	ASSERT(!a.isCompatible(b), "different driver version should be incompatible");
	PASS("fingerprint_incompatible_driver");
}

TEST(fingerprint_incompatible_backend)
{
	DeviceFingerprint a{BackendType::OpenCL, "RX 9070 XT", "gfx1201", 16ULL*1024*1024*1024, 32, "6.1.0", "3.0", "v1.0", "cryptonight_gpu"};
	DeviceFingerprint b = a;
	b.backend = BackendType::CUDA;
	ASSERT(!a.isCompatible(b), "different backend should be incompatible");
	PASS("fingerprint_incompatible_backend");
}

// ─── Persistence Tests ───

TEST(persist_roundtrip)
{
	AutotuneResult result;
	result.miner_version = "v0.9.0-test";
	result.timestamp = "2026-03-30T20:00:00Z";

	AutotuneState state;
	state.device_index = 0;
	state.fingerprint = {BackendType::OpenCL, "RX 9070 XT", "gfx1201", 16ULL*1024*1024*1024, 32, "6.1.0", "3.0", "v0.9.0-test", "cryptonight_gpu"};
	state.mode = TuneMode::Balanced;
	state.target = TuneTarget::Hashrate;
	state.completed = true;
	state.best_candidate_id = 1;
	state.total_elapsed_seconds = 123.4;
	state.timestamp = "2026-03-30T20:01:00Z";

	CandidateRecord c0;
	c0.candidate_id = 0;
	c0.status = CandidateStatus::Rejected;
	c0.reject_reason = "too slow";
	c0.amd = {256, 8};
	c0.metrics.avg_hashrate = 100.0;
	c0.score.final_score = 100.0;
	state.candidates.push_back(c0);

	CandidateRecord c1;
	c1.candidate_id = 1;
	c1.status = CandidateStatus::Success;
	c1.amd = {512, 16};
	c1.metrics.avg_hashrate = 4500.0;
	c1.metrics.cv_percent = 2.1;
	c1.metrics.valid_results = 50;
	c1.score.final_score = 4500.0;
	state.candidates.push_back(c1);

	result.devices.push_back(state);

	// Save
	const char* testfile = "/tmp/test_autotune.json";
	ASSERT(saveAutotuneResult(result, testfile), "save should succeed");

	// Load
	AutotuneResult loaded;
	ASSERT(loadAutotuneResult(loaded, testfile), "load should succeed");

	ASSERT(loaded.miner_version == "v0.9.0-test", "miner version should match");
	ASSERT(loaded.devices.size() == 1, "should have 1 device");
	ASSERT(loaded.devices[0].completed == true, "should be completed");
	ASSERT(loaded.devices[0].best_candidate_id == 1, "best candidate should be 1");
	ASSERT(loaded.devices[0].candidates.size() == 2, "should have 2 candidates");
	ASSERT(loaded.devices[0].candidates[0].status == CandidateStatus::Rejected, "c0 should be rejected");
	ASSERT(loaded.devices[0].candidates[1].amd.intensity == 512, "c1 intensity should be 512");
	ASSERT(loaded.devices[0].candidates[1].amd.worksize == 16, "c1 worksize should be 16");
	ASSERT(std::abs(loaded.devices[0].candidates[1].metrics.avg_hashrate - 4500.0) < 0.1, "c1 hashrate should be 4500");

	// Cache lookup
	const auto* cached = findCachedState(loaded, state.fingerprint);
	ASSERT(cached != nullptr, "should find cached state");
	ASSERT(cached->best_candidate_id == 1, "cached best should be 1");

	// Incompatible fingerprint
	DeviceFingerprint diff = state.fingerprint;
	diff.gpu_name = "Different GPU";
	ASSERT(findCachedState(loaded, diff) == nullptr, "should not find incompatible state");

	std::remove(testfile);
	PASS("persist_roundtrip");
}

TEST(persist_nvidia_roundtrip)
{
	AutotuneResult result;
	result.miner_version = "v0.9.0-test";
	result.timestamp = "2026-03-30T20:00:00Z";

	AutotuneState state;
	state.device_index = 0;
	state.fingerprint = {BackendType::CUDA, "GTX 1070 Ti", "sm_61", 8ULL*1024*1024*1024, 15, "535.0", "11.8", "v0.9.0-test", "cryptonight_gpu"};
	state.completed = true;
	state.best_candidate_id = 0;
	state.timestamp = "2026-03-30T20:00:00Z";

	CandidateRecord c0;
	c0.candidate_id = 0;
	c0.status = CandidateStatus::Success;
	c0.nvidia = {32, 60, 0};
	c0.metrics.avg_hashrate = 1578.0;
	c0.score.final_score = 1578.0;
	state.candidates.push_back(c0);

	result.devices.push_back(state);

	const char* testfile = "/tmp/test_autotune_nv.json";
	ASSERT(saveAutotuneResult(result, testfile), "save should succeed");

	AutotuneResult loaded;
	ASSERT(loadAutotuneResult(loaded, testfile), "load should succeed");
	ASSERT(loaded.devices[0].candidates[0].nvidia.threads == 32, "threads should be 32");
	ASSERT(loaded.devices[0].candidates[0].nvidia.blocks == 60, "blocks should be 60");
	ASSERT(loaded.devices[0].candidates[0].nvidia.bfactor == 0, "bfactor should be 0");

	std::remove(testfile);
	PASS("persist_nvidia_roundtrip");
}

int main()
{
	std::cout << "\n=== Autotune Framework Tests ===" << std::endl;

	// Tests run via static initialization above

	std::cout << "\n" << tests_passed << " passed, " << tests_failed << " failed" << std::endl;
	return tests_failed > 0 ? 1 : 0;
}
