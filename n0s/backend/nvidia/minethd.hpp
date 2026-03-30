#pragma once

#include "jconf.hpp"
#include "nvcc_code/cuda_context.hpp"
#include "n0s/jconf.hpp"

#include "n0s/backend/cpu/minethd.hpp"
#include "n0s/backend/iBackend.hpp"
#include "n0s/misc/environment.hpp"

#include <atomic>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

namespace n0s
{
namespace cuda
{

class minethd : public iBackend
{
  public:
	static std::vector<std::unique_ptr<iBackend>> thread_starter(uint32_t threadOffset, miner_work& pWork);
	static bool self_test();

	typedef void (*cn_hash_fun)(const void*, size_t, void*, cryptonight_ctx**, const n0s_algo&);

	minethd(miner_work& pWork, size_t iNo, const jconf::thd_cfg& cfg);

  private:
	void start_mining();

	void work_main();

	static std::atomic<uint64_t> iGlobalJobNo;
	static std::atomic<uint64_t> iConsumeCnt;
	static uint64_t iThreadCount;
	uint64_t iJobNo;

	miner_work oWork;

	std::promise<void> numa_promise;
	std::promise<void> thread_work_promise;
	std::mutex thd_aff_set;

	// block thread until all NVIDIA GPUs are initialized
	std::future<void> thread_work_guard;

	std::thread oWorkThd;
	int64_t affinity;

	nvid_ctx ctx;

	bool bQuit;
};

} // namespace cuda
} // namespace n0s
