#pragma once

#include "amd_gpu/gpu.hpp"
#include "jconf.hpp"
#include "n0s/backend/cpu/crypto/cryptonight.h"
#include "n0s/backend/iBackend.hpp"
#include "n0s/backend/miner_work.hpp"
#include "n0s/misc/environment.hpp"

#include <atomic>
#include <future>
#include <thread>

namespace n0s
{
namespace opencl
{

class minethd : public iBackend
{
  public:
	static std::vector<std::unique_ptr<iBackend>> thread_starter(uint32_t threadOffset, miner_work& pWork);
	static bool init_gpus();

	typedef void (*cn_hash_fun)(const void*, size_t, void*, cryptonight_ctx**, const n0s_algo&);

	minethd(miner_work& pWork, size_t iNo, GpuContext* ctx, const jconf::thd_cfg cfg);

  private:

	void work_main();

	uint64_t iJobNo;

	miner_work oWork;

	std::promise<void> order_fix;
	std::mutex thd_aff_set;

	std::thread oWorkThd;
	int64_t affinity;
	uint32_t autoTune;

	bool bQuit;
	bool bNoPrefetch;

	//Mutable ptr to vector below, different for each thread
	GpuContext* pGpuCtx;

	// WARNING - this vector (but not its contents) must be immutable
	// once the threads are started
	static std::vector<GpuContext> vGpuData;
};

} // namespace opencl
} // namespace n0s
