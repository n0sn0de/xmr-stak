#pragma once

#include "n0s/misc/environment.hpp"
#include "n0s/misc/home_dir.hpp"

#include <string>

namespace n0s
{

struct params
{

	static inline params& inst()
	{
		auto& env = environment::inst();
		if(env.pParams == nullptr)
		{
			std::unique_lock<std::mutex> lck(env.update);
			if(env.pParams == nullptr)
				env.pParams = new params;
		}
		return *env.pParams;
	}

	std::string executablePrefix;
	std::string binaryName;
	bool useAMD;
	bool AMDCache;
	bool useNVIDIA;
	std::string amdGpus;
	std::string nvidiaGpus;
	// user selected OpenCL vendor
	std::string openCLVendor;

	bool poolUseTls = false;
	std::string poolURL;
	bool userSetPwd = false;
	std::string poolPasswd;
	bool userSetRigid = false;
	std::string poolRigid;
	std::string poolUsername;
	bool nicehashMode = false;

	static constexpr int32_t httpd_port_unset = -1;
	static constexpr int32_t httpd_port_disabled = 0;
	int32_t httpd_port = httpd_port_unset;

	std::string currency = "cryptonight_gpu";

	std::string configFile;
	std::string configFilePools;
	std::string configFileAMD;
	std::string rootAMDCacheDir;
	std::string configFileNVIDIA;

	std::string outputFile;
	int h_print_time = -1;

	std::string minerArg0;
	std::string minerArgs;

	// block_version >= 0 enable benchmark
	int benchmark_block_version = -1;
	int benchmark_wait_sec = 30;
	int benchmark_work_sec = 60;
	std::string benchmark_json;
	bool profileKernels = false;

	// Autotune parameters
	bool autotune = false;
	std::string autotune_mode = "balanced";     // quick, balanced, exhaustive
	std::string autotune_backend = "all";       // amd, nvidia, all
	std::string autotune_gpus;                  // comma-separated GPU indices (empty = all)
	bool autotune_reset = false;                // Ignore cached results
	bool autotune_resume = false;               // Resume interrupted run
	int autotune_benchmark_sec = 30;
	int autotune_stability_sec = 60;
	std::string autotune_target = "hashrate";   // hashrate, efficiency, balanced
	std::string autotune_export;                // Optional export path
	std::string autotune_file = "autotune.json";

	params() :
		executablePrefix(""),
		binaryName("n0s-ryo-miner"),
		useAMD(true),
		AMDCache(true),
		useNVIDIA(true),
		openCLVendor("AMD"),
		configFile("config.txt"),
		configFilePools("pools.txt"),
		configFileAMD("amd.txt"),
		rootAMDCacheDir(get_home() + "/.openclcache/"),
		configFileNVIDIA("nvidia.txt")
	{
	}
};

} // namespace n0s
