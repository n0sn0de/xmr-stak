/*
 * REST API v1 — Clean JSON endpoints for the GUI dashboard
 *
 * All api_*_report() functions are called from the executor's event loop thread,
 * so they have safe access to executor's private members (pool state, telemetry, etc).
 * This file is #include'd-style integrated into executor.cpp via forward declarations
 * in executor.hpp — the functions are executor member functions.
 *
 * Endpoints:
 *   GET /api/v1/status           — Mining state, uptime, connection
 *   GET /api/v1/hashrate         — Per-GPU and total hashrate
 *   GET /api/v1/hashrate/history — Time-series hashrate data
 *   GET /api/v1/gpus             — GPU list with telemetry
 *   GET /api/v1/pool             — Pool info, shares, difficulty
 *   GET /api/v1/config           — Current miner configuration (sanitized)
 *   GET /api/v1/autotune         — Autotune results (if available)
 *   GET /api/v1/version          — Version and build info
 */

#include "n0s/misc/executor.hpp"
#include "n0s/jconf.hpp"
#include "n0s/misc/gpu_telemetry.hpp"
#include "n0s/net/jpsock.hpp"
#include "n0s/version.hpp"
#include "n0s/backend/iBackend.hpp"
#include "n0s/autotune/autotune_persist.hpp"
#include "n0s/autotune/autotune_types.hpp"
#include "n0s/params.hpp"

#include "n0s/vendor/rapidjson/document.h"
#include "n0s/vendor/rapidjson/stringbuffer.h"
#include "n0s/vendor/rapidjson/writer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

using namespace rapidjson;

// Helper: sanitize doubles — JSON doesn't support NaN/Infinity
static double sanitize_double(double v)
{
	if(std::isnan(v) || std::isinf(v))
		return 0.0;
	return v;
}

// Helper: serialize rapidjson document to string
static std::string json_to_string(const Document& doc)
{
	StringBuffer buf;
	Writer<StringBuffer> writer(buf);
	doc.Accept(writer);
	return std::string(buf.GetString(), buf.GetSize());
}

void executor::api_status_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	// Mining state
	bool mining = !pvThreads.empty();
	doc.AddMember("mining", mining, alloc);
	doc.AddMember("threads", static_cast<uint64_t>(pvThreads.size()), alloc);

	// Uptime (seconds since pool connection or start)
	using namespace std::chrono;
	auto now = system_clock::now();
	size_t uptimeSec = 0;

	jpsock* pool = pick_pool_by_id(current_pool_id);
	bool connected = pool != nullptr && pool->is_running() && pool->is_logged_in();

	if(connected)
	{
		uptimeSec = duration_cast<seconds>(now - tPoolConnTime).count();
	}

	doc.AddMember("uptime_seconds", static_cast<uint64_t>(uptimeSec), alloc);
	doc.AddMember("connected", connected, alloc);

	if(connected)
	{
		Value poolAddr(pool->get_pool_addr(), alloc);
		doc.AddMember("pool", poolAddr, alloc);
	}
	else
	{
		doc.AddMember("pool", Value(kNullType), alloc);
	}

	// Backend types active
	{
		Value backends(kArrayType);
		bool hasAmd = false, hasNvidia = false;
		for(const auto& t : pvThreads)
		{
			if(t->backendType == n0s::iBackend::AMD) hasAmd = true;
			if(t->backendType == n0s::iBackend::NVIDIA) hasNvidia = true;
		}
		if(hasAmd) backends.PushBack("opencl", alloc);
		if(hasNvidia) backends.PushBack("cuda", alloc);
		doc.AddMember("backends", backends, alloc);
	}

	out = json_to_string(doc);
}

void executor::api_hashrate_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	size_t nthd = pvThreads.size();
	double total_10s = 0.0, total_60s = 0.0, total_15m = 0.0;

	Value threads(kArrayType);
	for(size_t i = 0; i < nthd; i++)
	{
		double h10 = sanitize_double(telem->calc_telemetry_data(10000, i));
		double h60 = sanitize_double(telem->calc_telemetry_data(60000, i));
		double h15m = sanitize_double(telem->calc_telemetry_data(900000, i));

		total_10s += h10;
		total_60s += h60;
		total_15m += h15m;

		Value thd(kObjectType);
		thd.AddMember("index", static_cast<uint64_t>(i), alloc);
		thd.AddMember("hashrate_10s", h10, alloc);
		thd.AddMember("hashrate_60s", h60, alloc);
		thd.AddMember("hashrate_15m", h15m, alloc);

		// Add backend type and GPU index
		if(i < pvThreads.size())
		{
			Value backend(n0s::iBackend::getName(pvThreads[i]->backendType), alloc);
			thd.AddMember("backend", backend, alloc);
			thd.AddMember("gpu_index", pvThreads[i]->iGpuIndex, alloc);
		}

		threads.PushBack(thd, alloc);
	}

	Value total(kObjectType);
	total.AddMember("hashrate_10s", sanitize_double(total_10s), alloc);
	total.AddMember("hashrate_60s", sanitize_double(total_60s), alloc);
	total.AddMember("hashrate_15m", sanitize_double(total_15m), alloc);
	doc.AddMember("total", total, alloc);

	doc.AddMember("threads", threads, alloc);
	doc.AddMember("highest", sanitize_double(fHighestHps), alloc);

	out = json_to_string(doc);
}

void executor::api_gpus_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	Value gpuArray(kArrayType);

	// Deduplicate GPUs by (backend, gpu_index) since multiple threads can map to same GPU
	struct GpuInfo {
		n0s::iBackend::BackendType type;
		uint32_t gpuIndex;
		double hashrate;
	};
	std::vector<GpuInfo> seen;

	for(size_t i = 0; i < pvThreads.size(); i++)
	{
		auto& t = pvThreads[i];
		bool found = false;
		for(auto& s : seen)
		{
			if(s.type == t->backendType && s.gpuIndex == t->iGpuIndex)
			{
				s.hashrate += sanitize_double(telem->calc_telemetry_data(10000, i));
				found = true;
				break;
			}
		}
		if(!found)
		{
			GpuInfo gi;
			gi.type = t->backendType;
			gi.gpuIndex = t->iGpuIndex;
			gi.hashrate = sanitize_double(telem->calc_telemetry_data(10000, i));
			seen.push_back(gi);
		}
	}

	for(const auto& gi : seen)
	{
		Value gpu(kObjectType);
		gpu.AddMember("index", gi.gpuIndex, alloc);
		Value backend(n0s::iBackend::getName(gi.type), alloc);
		gpu.AddMember("backend", backend, alloc);
		gpu.AddMember("hashrate", gi.hashrate, alloc);

		// Query telemetry
		n0s::GpuTelemetry tel;
		bool hasTelemetry = false;

		if(gi.type == n0s::iBackend::AMD)
			hasTelemetry = n0s::queryAmdTelemetry(gi.gpuIndex, tel);
		else if(gi.type == n0s::iBackend::NVIDIA)
			hasTelemetry = n0s::queryNvidiaTelemetry(gi.gpuIndex, tel);

		// GPU name from telemetry (nvidia-smi or amd-smi)
		if(!tel.name.empty())
		{
			Value name(tel.name.c_str(), alloc);
			gpu.AddMember("name", name, alloc);
		}

		if(hasTelemetry)
		{
			Value telemetry(kObjectType);
			if(tel.temp_c >= 0) telemetry.AddMember("temp_c", tel.temp_c, alloc);
			if(tel.power_w >= 0) telemetry.AddMember("power_w", tel.power_w, alloc);
			if(tel.fan_pct >= 0) telemetry.AddMember("fan_pct", tel.fan_pct, alloc);
			if(tel.fan_rpm >= 0) telemetry.AddMember("fan_rpm", tel.fan_rpm, alloc);
			if(tel.gpu_clock_mhz >= 0) telemetry.AddMember("gpu_clock_mhz", tel.gpu_clock_mhz, alloc);
			if(tel.mem_clock_mhz >= 0) telemetry.AddMember("mem_clock_mhz", tel.mem_clock_mhz, alloc);

			// H/W (hashrate per watt)
			if(tel.power_w > 0 && gi.hashrate > 0)
				telemetry.AddMember("hw_ratio", sanitize_double(gi.hashrate / tel.power_w), alloc);

			gpu.AddMember("telemetry", telemetry, alloc);
		}

		gpuArray.PushBack(gpu, alloc);
	}

	doc.AddMember("gpus", gpuArray, alloc);

	out = json_to_string(doc);
}

void executor::api_pool_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	jpsock* pool = pick_pool_by_id(current_pool_id);
	bool connected = pool != nullptr && pool->is_running() && pool->is_logged_in();

	doc.AddMember("connected", connected, alloc);

	if(connected)
	{
		Value poolAddr(pool->get_pool_addr(), alloc);
		doc.AddMember("address", poolAddr, alloc);
	}
	else
	{
		doc.AddMember("address", Value(kNullType), alloc);
	}

	// Shares
	size_t goodShares = vMineResults[0].count;
	size_t totalShares = goodShares;
	for(size_t i = 1; i < vMineResults.size(); i++)
		totalShares += vMineResults[i].count;

	doc.AddMember("shares_accepted", static_cast<uint64_t>(goodShares), alloc);
	doc.AddMember("shares_rejected", static_cast<uint64_t>(totalShares - goodShares), alloc);
	doc.AddMember("shares_total", static_cast<uint64_t>(totalShares), alloc);

	// Difficulty
	doc.AddMember("difficulty", static_cast<uint64_t>(iPoolDiff), alloc);

	// Hashes total
	doc.AddMember("hashes_total", static_cast<uint64_t>(iPoolHashes), alloc);

	// Connection uptime
	size_t connSec = 0;
	if(connected)
	{
		using namespace std::chrono;
		connSec = duration_cast<seconds>(system_clock::now() - tPoolConnTime).count();
	}
	doc.AddMember("uptime_seconds", static_cast<uint64_t>(connSec), alloc);

	// Pool ping (median)
	size_t poolPing = 0;
	size_t n_calls = iPoolCallTimes.size();
	if(n_calls > 1)
	{
		auto times = iPoolCallTimes; // copy for nth_element
		std::nth_element(times.begin(), times.begin() + n_calls / 2, times.end());
		poolPing = times[n_calls / 2];
	}
	doc.AddMember("ping_ms", static_cast<uint64_t>(poolPing), alloc);

	// Average share time
	double avgShareTime = 0.0;
	if(!iPoolCallTimes.empty())
		avgShareTime = static_cast<double>(connSec) / iPoolCallTimes.size();
	doc.AddMember("avg_share_time", avgShareTime, alloc);

	// Top difficulties
	Value topDiffs(kArrayType);
	for(size_t i = 0; i < 10; i++)
		topDiffs.PushBack(static_cast<uint64_t>(iTopDiff[i]), alloc);
	doc.AddMember("top_difficulties", topDiffs, alloc);

	// Error log
	Value errors(kArrayType);
	for(size_t i = 1; i < vMineResults.size(); i++)
	{
		Value err(kObjectType);
		err.AddMember("count", static_cast<uint64_t>(vMineResults[i].count), alloc);
		Value msg(vMineResults[i].msg.c_str(), alloc);
		err.AddMember("text", msg, alloc);
		errors.PushBack(err, alloc);
	}
	doc.AddMember("errors", errors, alloc);

	out = json_to_string(doc);
}

void executor::api_hashrate_history_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	size_t gpuCount = 0;
	auto samples = hashrateHistory.get_all(gpuCount);

	doc.AddMember("gpu_count", static_cast<uint64_t>(gpuCount), alloc);
	doc.AddMember("sample_count", static_cast<uint64_t>(samples.size()), alloc);

	Value arr(kArrayType);
	for(const auto& s : samples)
	{
		Value entry(kObjectType);
		entry.AddMember("t", s.timestamp_ms, alloc);
		entry.AddMember("total", sanitize_double(s.total_hs), alloc);

		Value gpus(kArrayType);
		for(size_t i = 0; i < gpuCount; i++)
			gpus.PushBack(sanitize_double(s.per_gpu_hs[i]), alloc);
		entry.AddMember("gpus", gpus, alloc);

		arr.PushBack(entry, alloc);
	}
	doc.AddMember("samples", arr, alloc);

	out = json_to_string(doc);
}

void executor::api_config_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	// Pool configuration (sanitized — no passwords)
	Value poolsArr(kArrayType);
	uint64_t poolCount = jconf::inst()->GetPoolCount();
	for(uint64_t i = 0; i < poolCount; i++)
	{
		jconf::pool_cfg cfg;
		if(!jconf::inst()->GetPoolConfig(i, cfg))
			continue;

		Value pool(kObjectType);
		Value addr(cfg.sPoolAddr, alloc);
		pool.AddMember("address", addr, alloc);

		// Wallet: show first 8 + last 4 chars, mask the rest
		std::string wallet(cfg.sWalletAddr);
		if(wallet.length() > 16)
		{
			std::string masked = wallet.substr(0, 8) + "..." + wallet.substr(wallet.length() - 4);
			Value wv(masked.c_str(), alloc);
			pool.AddMember("wallet", wv, alloc);
		}
		else
		{
			Value wv(cfg.sWalletAddr, alloc);
			pool.AddMember("wallet", wv, alloc);
		}

		Value rigid(cfg.sRigId, alloc);
		pool.AddMember("rig_id", rigid, alloc);
		pool.AddMember("tls", cfg.tls, alloc);
		pool.AddMember("nicehash", cfg.nicehash, alloc);
		pool.AddMember("weight", cfg.raw_weight, alloc);
		// Password deliberately omitted for security

		poolsArr.PushBack(pool, alloc);
	}
	doc.AddMember("pools", poolsArr, alloc);

	// HTTP config
	doc.AddMember("httpd_port", jconf::inst()->GetHttpdPort(), alloc);

	// Mining info
	doc.AddMember("algorithm", "cryptonight_gpu", alloc);

	// Backend flags
	Value backends(kObjectType);
	backends.AddMember("amd", n0s::params::inst().useAMD, alloc);
	backends.AddMember("nvidia", n0s::params::inst().useNVIDIA, alloc);
	doc.AddMember("backends", backends, alloc);

	out = json_to_string(doc);
}

void executor::api_autotune_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	// Try to load cached autotune results
	std::string atFile = n0s::params::inst().autotune_file;
	n0s::autotune::AutotuneResult result;
	bool loaded = n0s::autotune::loadAutotuneResult(result, atFile);

	doc.AddMember("available", loaded, alloc);

	if(loaded)
	{
		Value mv(result.miner_version.c_str(), alloc);
		doc.AddMember("miner_version", mv, alloc);
		Value ts(result.timestamp.c_str(), alloc);
		doc.AddMember("timestamp", ts, alloc);

		Value devices(kArrayType);
		for(const auto& state : result.devices)
		{
			Value dev(kObjectType);

			// Fingerprint
			Value gpuName(state.fingerprint.gpu_name.c_str(), alloc);
			dev.AddMember("gpu_name", gpuName, alloc);
			Value gpuArch(state.fingerprint.gpu_architecture.c_str(), alloc);
			dev.AddMember("gpu_architecture", gpuArch, alloc);
			dev.AddMember("compute_units", state.fingerprint.compute_units, alloc);
			dev.AddMember("device_index", state.device_index, alloc);
			Value backend(state.fingerprint.backend == n0s::autotune::BackendType::OpenCL ? "opencl" : "cuda", alloc);
			dev.AddMember("backend", backend, alloc);

			// Status
			dev.AddMember("completed", state.completed, alloc);
			dev.AddMember("candidates_tested", static_cast<uint64_t>(state.candidates.size()), alloc);
			dev.AddMember("elapsed_seconds", state.total_elapsed_seconds, alloc);

			// Best result
			if(state.best_candidate_id >= 0 && static_cast<size_t>(state.best_candidate_id) < state.candidates.size())
			{
				const auto& best = state.candidates[state.best_candidate_id];
				Value winner(kObjectType);
				winner.AddMember("hashrate", best.metrics.avg_hashrate, alloc);
				winner.AddMember("stability_cv", best.metrics.cv_percent, alloc);
				winner.AddMember("score", best.score.final_score, alloc);

				if(state.fingerprint.backend == n0s::autotune::BackendType::OpenCL)
				{
					winner.AddMember("intensity", static_cast<uint64_t>(best.amd.intensity), alloc);
					winner.AddMember("worksize", static_cast<uint64_t>(best.amd.worksize), alloc);
				}
				else
				{
					winner.AddMember("threads", best.nvidia.threads, alloc);
					winner.AddMember("blocks", best.nvidia.blocks, alloc);
					winner.AddMember("bfactor", best.nvidia.bfactor, alloc);
				}
				dev.AddMember("best", winner, alloc);
			}

			devices.PushBack(dev, alloc);
		}
		doc.AddMember("devices", devices, alloc);
	}

	out = json_to_string(doc);
}

void executor::api_version_report(std::string& out)
{
	Document doc(kObjectType);
	auto& alloc = doc.GetAllocator();

	Value version(get_version_str().c_str(), alloc);
	doc.AddMember("version", version, alloc);

	Value versionShort(get_version_str_short().c_str(), alloc);
	doc.AddMember("version_short", versionShort, alloc);

	// Backends enabled
	Value backends(kObjectType);
#ifndef CONF_NO_CUDA
	backends.AddMember("cuda", true, alloc);
#else
	backends.AddMember("cuda", false, alloc);
#endif
#ifndef CONF_NO_OPENCL
	backends.AddMember("opencl", true, alloc);
#else
	backends.AddMember("opencl", false, alloc);
#endif
	doc.AddMember("backends", backends, alloc);

	// Algorithm
	doc.AddMember("algorithm", "cryptonight_gpu", alloc);

	out = json_to_string(doc);
}
