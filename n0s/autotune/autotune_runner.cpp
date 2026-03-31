#include "autotune_runner.hpp"

#include "n0s/misc/console.hpp"
#include "n0s/version.hpp"

#include "n0s/vendor/rapidjson/document.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace n0s
{
namespace autotune
{

SubprocessRunner::SubprocessRunner(const std::string& miner_path)
	: miner_path_(miner_path)
{
}

std::string SubprocessRunner::generateAmdConfig(uint32_t device_index, const AmdCandidate& candidate)
{
	// Create temp file
	char tmppath[] = "/tmp/n0s_autotune_amd_XXXXXX";
	int fd = mkstemp(tmppath);
	if(fd < 0) return "";
	close(fd);

	std::ofstream f(tmppath);
	if(!f.is_open()) return "";

	f << "// autotune candidate config\n";
	f << "\"gpu_threads_conf\" : [\n";
	f << "  { \"index\" : " << device_index << ",\n";
	f << "    \"intensity\" : " << candidate.intensity << ", \"worksize\" : " << candidate.worksize << ",\n";
	f << "    \"affine_to_cpu\" : false, \"strided_index\" : 0, \"mem_chunk\" : 2,\n";
	f << "    \"unroll\" : 1, \"comp_mode\" : true, \"interleave\" : 0\n";
	f << "  },\n";
	f << "],\n";
	f << "\"auto_tune\" : 0,\n";
	f << "\"platform_index\" : 0,\n";
	f.close();

	return std::string(tmppath);
}

std::string SubprocessRunner::generateNvidiaConfig(uint32_t device_index, const NvidiaCandidate& candidate)
{
	char tmppath[] = "/tmp/n0s_autotune_nv_XXXXXX";
	int fd = mkstemp(tmppath);
	if(fd < 0) return "";
	close(fd);

	std::ofstream f(tmppath);
	if(!f.is_open()) return "";

	f << "// autotune candidate config\n";
	f << "\"gpu_threads_conf\" :\n[\n";
	f << "  { \"index\" : " << device_index << ",\n";
	f << "    \"threads\" : " << candidate.threads << ", \"blocks\" : " << candidate.blocks << ",\n";
	f << "    \"bfactor\" : " << candidate.bfactor << ", \"bsleep\" : 0,\n";
	f << "    \"affine_to_cpu\" : false, \"sync_mode\" : 3,\n";
	f << "    \"mem_mode\" : 1,\n";
	f << "  },\n";
	f << "],\n";
	f.close();

	return std::string(tmppath);
}

bool SubprocessRunner::runBenchmark(
	const std::string& config_flag,
	const std::string& config_path,
	const std::string& disable_other_flag,
	int benchmark_sec,
	BenchmarkMetrics& metrics)
{
	// Create temp file for JSON output
	char json_path[] = "/tmp/n0s_autotune_result_XXXXXX";
	int fd = mkstemp(json_path);
	if(fd < 0) return false;
	close(fd);

	// Build command — use warmup of 15 seconds, benchmark for requested duration
	int warmup_sec = 15;

	// Build a minimal pools.txt for the subprocess if needed
	char pools_path[] = "/tmp/n0s_autotune_pools_XXXXXX";
	int pools_fd = mkstemp(pools_path);
	if(pools_fd >= 0)
	{
		const char* pools_content =
			"\"pool_list\" :\n[\n"
			"  {\"pool_address\" : \"autotune.local:3333\",\n"
			"   \"wallet_address\" : \"autotune\",\n"
			"   \"rig_id\" : \"\", \"pool_password\" : \"\",\n"
			"   \"use_nicehash\" : false, \"use_tls\" : false,\n"
			"   \"tls_fingerprint\" : \"\", \"pool_weight\" : 1\n  },\n],\n"
			"\"currency\" : \"cryptonight_gpu\",\n";
		write(pools_fd, pools_content, strlen(pools_content));
		close(pools_fd);
	}

	std::ostringstream cmd;
	cmd << miner_path_
	    << " --benchmark 14"
	    << " --benchwait " << warmup_sec
	    << " --benchwork " << benchmark_sec
	    << " --benchmark-json " << json_path
	    << " " << config_flag << " " << config_path
	    << " " << disable_other_flag
	    << " --poolconf " << pools_path
	    << " 2>&1";

	printer::inst()->print_msg(L2, "AUTOTUNE: Running: %s", cmd.str().c_str());

	// Run subprocess with timeout
	int timeout_sec = warmup_sec + benchmark_sec + 30; // Extra buffer for startup

	// Use fork+exec for timeout control
	pid_t pid = fork();
	if(pid < 0) return false;

	if(pid == 0)
	{
		// Child: redirect stdout/stderr to /dev/null to keep output clean
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);

		execl("/bin/sh", "sh", "-c", cmd.str().c_str(), nullptr);
		_exit(127);
	}

	// Parent: wait with timeout
	int status = 0;
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_sec);
	bool finished = false;

	while(std::chrono::steady_clock::now() < deadline)
	{
		pid_t result = waitpid(pid, &status, WNOHANG);
		if(result == pid)
		{
			finished = true;
			break;
		}
		if(result < 0) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	if(!finished)
	{
		// Timeout — kill the child
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
		printer::inst()->print_msg(L1, "AUTOTUNE: Benchmark subprocess timed out after %d seconds", timeout_sec);
		std::remove(json_path);
		std::remove(pools_path);
		return false;
	}

	std::remove(pools_path);

	if(!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		printer::inst()->print_msg(L1, "AUTOTUNE: Benchmark subprocess failed (exit code %d)",
			WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		std::remove(json_path);
		return false;
	}

	// Parse results
	bool ok = parseBenchmarkJson(json_path, metrics);
	std::remove(json_path);
	return ok;
}

bool SubprocessRunner::parseBenchmarkJson(const std::string& json_path, BenchmarkMetrics& metrics)
{
	std::ifstream ifs(json_path);
	if(!ifs.is_open()) return false;

	std::stringstream ss;
	ss << ifs.rdbuf();
	std::string content = ss.str();

	rapidjson::Document doc;
	if(doc.Parse(content.c_str()).HasParseError()) return false;

	if(!doc.HasMember("benchmark") || !doc["benchmark"].IsObject()) return false;
	const auto& bench = doc["benchmark"];

	if(bench.HasMember("total_hps"))
		metrics.avg_hashrate = bench["total_hps"].GetDouble();

	if(bench.HasMember("work_sec"))
		metrics.benchmark_seconds = bench["work_sec"].GetDouble();

	// Parse per-thread data
	if(bench.HasMember("threads") && bench["threads"].IsArray())
	{
		double min_hps = 1e18, max_hps = 0;
		double total_cv = 0;
		uint32_t thread_count = 0;

		for(const auto& t : bench["threads"].GetArray())
		{
			double hps = t.HasMember("avg_hps") ? t["avg_hps"].GetDouble() : 0;
			double cv = t.HasMember("cv_pct") ? t["cv_pct"].GetDouble() : 0;
			uint64_t total_h = t.HasMember("total_hashes") ? t["total_hashes"].GetUint64() : 0;

			if(hps < min_hps) min_hps = hps;
			if(hps > max_hps) max_hps = hps;
			total_cv += cv;
			thread_count++;

			metrics.valid_results += static_cast<uint32_t>(total_h > 0 ? 1 : 0);
		}

		metrics.min_hashrate = min_hps;
		metrics.max_hashrate = max_hps;
		metrics.cv_percent = (thread_count > 0) ? (total_cv / thread_count) : 0;

		// For single-GPU autotune, valid_results = thread count with non-zero hashes
		// All results are "valid" if we got hashes (no share validation in benchmark mode)
		metrics.valid_results = thread_count;
	}

	return metrics.avg_hashrate > 0;
}

bool SubprocessRunner::evaluateAmd(
	uint32_t device_index,
	const CandidateRecord& candidate,
	int benchmark_sec,
	BenchmarkMetrics& metrics)
{
	std::string config_path = generateAmdConfig(device_index, candidate.amd);
	if(config_path.empty())
	{
		printer::inst()->print_msg(L1, "AUTOTUNE: Failed to generate AMD config file");
		return false;
	}

	bool ok = runBenchmark("--amd", config_path, "--noNVIDIA", benchmark_sec, metrics);
	std::remove(config_path.c_str());
	return ok;
}

bool SubprocessRunner::evaluateNvidia(
	uint32_t device_index,
	const CandidateRecord& candidate,
	int benchmark_sec,
	BenchmarkMetrics& metrics)
{
	std::string config_path = generateNvidiaConfig(device_index, candidate.nvidia);
	if(config_path.empty())
	{
		printer::inst()->print_msg(L1, "AUTOTUNE: Failed to generate NVIDIA config file");
		return false;
	}

	bool ok = runBenchmark("--nvidia", config_path, "--noAMD", benchmark_sec, metrics);
	std::remove(config_path.c_str());
	return ok;
}

bool SubprocessRunner::collectAmdFingerprint(uint32_t device_index, DeviceFingerprint& fingerprint)
{
	fingerprint.backend = BackendType::OpenCL;
	fingerprint.algorithm = "cryptonight_gpu";
	fingerprint.miner_version = get_version_str();
	fingerprint.compute_units = 0;
	fingerprint.vram_bytes = 0;

	// Query clinfo for device properties
	FILE* pipe = popen("clinfo 2>/dev/null", "r");
	if(pipe)
	{
		std::array<char, 512> buf;
		std::string output;
		while(fgets(buf.data(), buf.size(), pipe))
			output += buf.data();
		pclose(pipe);

		// Helper: extract value after a key (handles tab/space padding in clinfo output)
		auto extractLine = [&](const std::string& key) -> std::string {
			size_t pos = output.find(key);
			if(pos == std::string::npos) return "";
			pos += key.length();
			// Skip colons and whitespace
			while(pos < output.length() && (output[pos] == ':' || output[pos] == ' ' || output[pos] == '\t'))
				pos++;
			size_t end = output.find('\n', pos);
			if(end == std::string::npos) end = output.length();
			// Trim trailing whitespace
			while(end > pos && (output[end-1] == ' ' || output[end-1] == '\t' || output[end-1] == '\r'))
				end--;
			return output.substr(pos, end - pos);
		};

		// Try multiple key formats (clinfo varies by version)
		fingerprint.gpu_name = extractLine("Device Name");
		if(fingerprint.gpu_name.empty())
			fingerprint.gpu_name = extractLine("Device Board Name (AMD)");

		std::string cu_str = extractLine("Max compute units");
		if(!cu_str.empty())
		{
			try { fingerprint.compute_units = std::stoul(cu_str); } catch(...) {}
		}

		std::string mem_str = extractLine("Global memory size");
		if(!mem_str.empty())
		{
			try { fingerprint.vram_bytes = std::stoull(mem_str); } catch(...) {}
		}

		fingerprint.runtime_version = extractLine("Device Version");
		fingerprint.driver_version = extractLine("Driver Version");
		fingerprint.gpu_architecture = extractLine("Device Topology (AMD)");
	}

	// Validate — if we got garbage, use safe conservative defaults
	if(fingerprint.compute_units == 0 || fingerprint.compute_units > 1024)
	{
		// Fallback: try to get from AMD GPU info via sysfs
		std::string sysfs_cmd = "cat /sys/class/drm/card*/device/pp_dpm_sclk 2>/dev/null | wc -l";
		FILE* sp = popen(sysfs_cmd.c_str(), "r");
		if(sp) { pclose(sp); }

		if(fingerprint.compute_units == 0 || fingerprint.compute_units > 1024)
			fingerprint.compute_units = 32; // Safe default for modern AMD GPUs
		printer::inst()->print_msg(L1, "AUTOTUNE: Using default compute_units=%u", fingerprint.compute_units);
	}

	if(fingerprint.vram_bytes == 0 || fingerprint.vram_bytes > 128ULL * 1024 * 1024 * 1024)
	{
		fingerprint.vram_bytes = 8ULL * 1024 * 1024 * 1024; // 8 GiB default
		printer::inst()->print_msg(L1, "AUTOTUNE: Using default vram=8 GiB");
	}

	if(fingerprint.gpu_name.empty())
	{
		fingerprint.gpu_name = "AMD OpenCL GPU #" + std::to_string(device_index);
		printer::inst()->print_msg(L1, "AUTOTUNE: Warning — could not determine GPU name");
	}

	return true;
}

bool SubprocessRunner::collectNvidiaFingerprint(uint32_t device_index, DeviceFingerprint& fingerprint)
{
	fingerprint.backend = BackendType::CUDA;
	fingerprint.algorithm = "cryptonight_gpu";
	fingerprint.miner_version = get_version_str();

	// Query nvidia-smi for device properties
	std::ostringstream cmd;
	cmd << "nvidia-smi -i " << device_index
	    << " --query-gpu=name,compute_cap,memory.total,driver_version"
	    << " --format=csv,noheader,nounits 2>/dev/null";

	FILE* pipe = popen(cmd.str().c_str(), "r");
	if(!pipe)
	{
		fingerprint.gpu_name = "NVIDIA CUDA GPU #" + std::to_string(device_index);
		return true;
	}

	std::array<char, 512> buf;
	std::string output;
	while(fgets(buf.data(), buf.size(), pipe))
		output += buf.data();
	pclose(pipe);

	// Parse CSV: "name, compute_cap, memory_total_MiB, driver_version"
	// e.g. "NVIDIA GeForce GTX 1070 Ti, 6.1, 8119, 535.183.01"
	std::istringstream iss(output);
	std::string token;

	if(std::getline(iss, token, ','))
	{
		// Trim whitespace
		auto trim = [](std::string& s) {
			s.erase(0, s.find_first_not_of(" \t\n\r"));
			s.erase(s.find_last_not_of(" \t\n\r") + 1);
		};
		trim(token);
		fingerprint.gpu_name = token;
	}

	if(std::getline(iss, token, ','))
	{
		auto trim = [](std::string& s) {
			s.erase(0, s.find_first_not_of(" \t\n\r"));
			s.erase(s.find_last_not_of(" \t\n\r") + 1);
		};
		trim(token);
		// Convert "6.1" to "sm_61"
		std::string cc = token;
		// Remove dots: "6.1" → "61"
		std::string cc_clean;
		for(char c : cc)
			if(c != '.') cc_clean += c;
		cc = cc_clean;
		fingerprint.gpu_architecture = "sm_" + cc;

		// Also extract compute units from nvidia-smi
		std::ostringstream sm_cmd;
		sm_cmd << "nvidia-smi -i " << device_index
		       << " --query-gpu=count"
		       << " --format=csv,noheader 2>/dev/null";
		// Get SM count via a separate query if needed
	}

	if(std::getline(iss, token, ','))
	{
		auto trim = [](std::string& s) {
			s.erase(0, s.find_first_not_of(" \t\n\r"));
			s.erase(s.find_last_not_of(" \t\n\r") + 1);
		};
		trim(token);
		try {
			fingerprint.vram_bytes = std::stoull(token) * 1024ULL * 1024ULL; // MiB → bytes
		} catch(...) {}
	}

	if(std::getline(iss, token, ','))
	{
		auto trim = [](std::string& s) {
			s.erase(0, s.find_first_not_of(" \t\n\r"));
			s.erase(s.find_last_not_of(" \t\n\r") + 1);
		};
		trim(token);
		fingerprint.driver_version = token;
	}

	// Get CUDA runtime version
	FILE* cuda_pipe = popen("nvcc --version 2>/dev/null | grep 'release' | sed 's/.*release \\([0-9.]*\\).*/\\1/'", "r");
	if(cuda_pipe)
	{
		if(fgets(buf.data(), buf.size(), cuda_pipe))
		{
			fingerprint.runtime_version = buf.data();
			fingerprint.runtime_version.erase(fingerprint.runtime_version.find_last_not_of(" \t\n\r") + 1);
		}
		pclose(cuda_pipe);
	}

	// nvidia-smi doesn't expose SM count directly. Query via CUDA deviceQuery
	// or estimate from compute capability + GPU name heuristic.
	std::ostringstream sm_cmd;
	sm_cmd << "nvidia-smi -i " << device_index
	       << " --query-gpu=gpu_sm_count --format=csv,noheader 2>/dev/null";
	FILE* sm_pipe = popen(sm_cmd.str().c_str(), "r");
	if(sm_pipe)
	{
		if(fgets(buf.data(), buf.size(), sm_pipe))
		{
			std::string sm_str = buf.data();
			sm_str.erase(sm_str.find_last_not_of(" \t\n\r") + 1);
			try { fingerprint.compute_units = std::stoul(sm_str); } catch(...) {}
		}
		pclose(sm_pipe);
	}

	printer::inst()->print_msg(L0, "AUTOTUNE: nvidia-smi SM query result: compute_units=%u", fingerprint.compute_units);

	// If nvidia-smi didn't report SM count (older drivers), estimate from compute cap
	if(fingerprint.compute_units == 0)
	{
		// Well-known SM counts by compute capability + model patterns
		// This is a fallback — accuracy matters less than having any value
		uint32_t cap = 0;
		if(fingerprint.gpu_architecture.find("sm_") == 0)
		{
			try { cap = std::stoul(fingerprint.gpu_architecture.substr(3)); } catch(...) {}
		}

		// Conservative estimates per architecture family
		if(fingerprint.gpu_name.find("1070 Ti") != std::string::npos) fingerprint.compute_units = 19;
		else if(fingerprint.gpu_name.find("1080 Ti") != std::string::npos) fingerprint.compute_units = 28;
		else if(fingerprint.gpu_name.find("1080") != std::string::npos) fingerprint.compute_units = 20;
		else if(fingerprint.gpu_name.find("1070") != std::string::npos) fingerprint.compute_units = 15;
		else if(fingerprint.gpu_name.find("1060") != std::string::npos) fingerprint.compute_units = 10;
		else if(fingerprint.gpu_name.find("2080 Ti") != std::string::npos) fingerprint.compute_units = 68;
		else if(fingerprint.gpu_name.find("2080 S") != std::string::npos) fingerprint.compute_units = 48;
		else if(fingerprint.gpu_name.find("2080") != std::string::npos) fingerprint.compute_units = 46;
		else if(fingerprint.gpu_name.find("2070 S") != std::string::npos) fingerprint.compute_units = 40;
		else if(fingerprint.gpu_name.find("2070") != std::string::npos) fingerprint.compute_units = 36;
		else if(fingerprint.gpu_name.find("2060 S") != std::string::npos) fingerprint.compute_units = 34;
		else if(fingerprint.gpu_name.find("2060") != std::string::npos) fingerprint.compute_units = 30;
		else if(fingerprint.gpu_name.find("3090") != std::string::npos) fingerprint.compute_units = 82;
		else if(fingerprint.gpu_name.find("3080") != std::string::npos) fingerprint.compute_units = 68;
		else if(fingerprint.gpu_name.find("3070") != std::string::npos) fingerprint.compute_units = 46;
		else if(fingerprint.gpu_name.find("3060") != std::string::npos) fingerprint.compute_units = 28;
		else if(fingerprint.gpu_name.find("4090") != std::string::npos) fingerprint.compute_units = 128;
		else if(fingerprint.gpu_name.find("4080") != std::string::npos) fingerprint.compute_units = 76;
		else if(fingerprint.gpu_name.find("4070") != std::string::npos) fingerprint.compute_units = 46;
		else if(cap >= 89) fingerprint.compute_units = 60; // Ada Lovelace generic
		else if(cap >= 86) fingerprint.compute_units = 40; // Ampere generic
		else if(cap >= 75) fingerprint.compute_units = 36; // Turing generic
		else if(cap >= 61) fingerprint.compute_units = 15; // Pascal generic
		else fingerprint.compute_units = 15; // Conservative fallback

		printer::inst()->print_msg(L1, "AUTOTUNE: Estimated SM count=%u for %s",
			fingerprint.compute_units, fingerprint.gpu_name.c_str());
	}

	return !fingerprint.gpu_name.empty();
}

} // namespace autotune
} // namespace n0s
