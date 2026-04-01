/**
 * cuda_device.cu — CUDA device management
 *
 * Device enumeration, capability checking, and memory initialization.
 */

#include "cuda_device.hpp"
#include "cuda_extra.hpp"
#include "n0s/jconf.hpp"

#include <cuda.h>
#include <cuda_runtime.h>
#include <sstream>
#include <algorithm>
#include <string>
#include <vector>
#include <cstring>

extern "C" int cuda_get_devicecount(int* deviceCount)
{
	cudaError_t err;
	*deviceCount = 0;
	err = cudaGetDeviceCount(deviceCount);
	if(err != cudaSuccess)
	{
		if(err == cudaErrorNoDevice)
			printf("ERROR: NVIDIA no CUDA device found!\n");
		else if(err == cudaErrorInsufficientDriver)
			printf("WARNING: NVIDIA Insufficient driver!\n");
		else
			printf("WARNING: NVIDIA Unable to query number of CUDA devices!\n");
		return 0;
	}

	return 1;
}

extern "C" int cuda_get_deviceinfo(nvid_ctx* ctx)
{
	cudaError_t err;
	int version;

	err = cudaDriverGetVersion(&version);
	if(err != cudaSuccess)
	{
		printf("Unable to query CUDA driver version! Is an nVidia driver installed?\n");
		return 1;
	}

	// Check CUDA driver/runtime version compatibility
	// Allow minor version forward compat within same major (e.g. driver 12.2 + toolkit 12.6)
	// since we only use basic CUDA APIs (no 12.3+ features)
	const int driverMajor = version / 1000;
	const int runtimeMajor = CUDART_VERSION / 1000;

	if(driverMajor < runtimeMajor)
	{
		printf("ERROR: Driver CUDA major version %d.%d < compiled CUDA %d.%d. Update your NVIDIA driver!\n",
			version / 1000, (version % 1000 / 10),
			CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10);
		return 1;
	}

	if(version < CUDART_VERSION)
	{
		printf("NOTE: Driver CUDA %d.%d < compiled CUDA %d.%d (minor version forward compat mode)\n",
			version / 1000, (version % 1000 / 10),
			CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10);
		// Continue — minor version compat should work for basic CUDA APIs
	}

	int GPU_N;
	if(cuda_get_devicecount(&GPU_N) == 0)
	{
		printf("WARNING: CUDA claims zero devices?\n");
		return 1;
	}

	if(ctx->device_id >= GPU_N)
	{
		printf("WARNING: Invalid device ID '%i'!\n", ctx->device_id);
		return 1;
	}

	cudaDeviceProp props;
	err = cudaGetDeviceProperties(&props, ctx->device_id);
	if(err != cudaSuccess)
	{
		printf("\nGPU %d: %s\n%s line %d\n", ctx->device_id, cudaGetErrorString(err), __FILE__, __LINE__);
		return 1;
	}

	ctx->device_name = strdup(props.name);
	ctx->device_mpcount = props.multiProcessorCount;
	ctx->device_arch[0] = props.major;
	ctx->device_arch[1] = props.minor;
	ctx->device_maxThreadsPerBlock = props.maxThreadsPerBlock;

	const int gpuArch = ctx->device_arch[0] * 10 + ctx->device_arch[1];

	ctx->name = std::string(props.name);

	printf("CUDA [%d.%d/%d.%d] GPU#%d, device architecture %d: \"%s\"...\n",
		version / 1000, (version % 1000 / 10),
		CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10,
		ctx->device_id, gpuArch, ctx->device_name);

	std::vector<int> arch;
#define N0S_PP_TOSTRING1(str) #str
#define N0S_PP_TOSTRING(str) N0S_PP_TOSTRING1(str)
	char const* archStringList = N0S_PP_TOSTRING(N0S_CUDA_ARCH_LIST);
#undef N0S_PP_TOSTRING
#undef N0S_PP_TOSTRING1
	std::stringstream ss(archStringList);

	//transform string list separated with `+` into a vector of integers
	int tmpArch;
	while(ss >> tmpArch)
		arch.push_back(tmpArch);

	// Minimum supported architecture: Pascal (sm_60)
	if(gpuArch < 60)
	{
		printf("WARNING: skip device — GPU architecture sm_%d is below minimum (sm_60 / Pascal)\n", gpuArch);
		return 5;
	}

	// Verify binary contains a compatible architecture
	{
		int minSupportedArch = 0;
		for(const auto a : arch)
			if(a >= 60 && (minSupportedArch == 0 || a < minSupportedArch))
				minSupportedArch = a;
		if(minSupportedArch == 0 || gpuArch < minSupportedArch)
		{
			printf("WARNING: skip device — binary does not contain architecture for sm_%d (min compiled: sm_%d)\n",
				gpuArch, minSupportedArch);
			return 5;
		}
	}

	// Only cryptonight_gpu is supported
	constexpr bool useCryptonight_gpu = true;

	// set all device option those marked as auto (-1) to a valid value
	if(ctx->device_blocks == -1)
	{
		ctx->device_blocks = props.multiProcessorCount * 3;

		// use 8 blocks per SM for cryptonight_gpu (all supported GPUs are >= Pascal)
		if(useCryptonight_gpu)
			ctx->device_blocks = props.multiProcessorCount * 8;

		// increase bfactor for low end devices to avoid that the miner is killed by the OS
		if(props.multiProcessorCount <= 6)
			ctx->device_bfactor += 2;
	}

	// for the most algorithms we are using 8 threads per hash
	uint32_t threadsPerHash = 8;

	if(ctx->device_threads == -1)
	{
		// All supported GPUs (>= Pascal) support 1024 threads per block
		const uint32_t maxThreadsPerBlock = 1024;

		// phase2_gpu uses 16 threads per hash
		if(useCryptonight_gpu)
			threadsPerHash = 16;

		ctx->device_threads = maxThreadsPerBlock / threadsPerHash;
		constexpr size_t byteToMiB = 1024u * 1024u;

		// Memory limits by GPU class
		size_t maxMemUsage = byteToMiB * byteToMiB; // default 1TiB (no limit)
		if(props.major == 6)
		{
			// Pascal: limit based on SM count
			if(props.multiProcessorCount < 15)
				maxMemUsage = size_t(2048u) * byteToMiB;  // < GTX1070
			else if(props.multiProcessorCount <= 20)
				maxMemUsage = size_t(4096u) * byteToMiB;  // GTX1070/1080
		}
		if(props.multiProcessorCount <= 6)
			maxMemUsage = size_t(1024u) * byteToMiB;  // low-end GPUs

		int* tmp;
		cudaError_t err;
#define MSG_CUDA_FUNC_FAIL "WARNING: skip device - %s failed\n"
		// a device must be selected to get the right memory usage later on
		err = cudaSetDevice(ctx->device_id);
		if(err != cudaSuccess)
		{
			printf(MSG_CUDA_FUNC_FAIL, "cudaSetDevice");
			return 2;
		}
		// trigger that a context on the gpu will be allocated
		err = cudaMalloc(&tmp, 256);
		if(err != cudaSuccess)
		{
			printf(MSG_CUDA_FUNC_FAIL, "cudaMalloc");
			return 3;
		}

		size_t freeMemory = 0;
		size_t totalMemory = 0;
		CUDA_CHECK(ctx->device_id, cudaMemGetInfo(&freeMemory, &totalMemory));

		CUDA_CHECK(ctx->device_id, cudaFree(tmp));
		// delete created context on the gpu
		CUDA_CHECK(ctx->device_id, cudaDeviceReset());

		ctx->total_device_memory = totalMemory;
		ctx->free_device_memory = freeMemory;

		const size_t hashMemSize = ::jconf::inst()->GetMiningMemSize();

		// keep 128MiB memory free (value is randomly chosen)
		// 200byte are meta data memory (result nonce, ...)
		size_t availableMem = freeMemory - (128u * byteToMiB) - 200u;
		size_t limitedMemory = std::min(availableMem, maxMemUsage);
		// up to 16kibyte extra memory is used per thread for some kernel (lmem/local memory)
		// 680bytes are extra meta data memory per hash
		size_t perThread = hashMemSize + 16192u + 680u;

		size_t max_intensity = limitedMemory / perThread;
		ctx->device_threads = max_intensity / ctx->device_blocks;
		// use only odd number of threads
		ctx->device_threads = ctx->device_threads & 0xFFFFFFFE;

		if(ctx->device_threads > maxThreadsPerBlock / threadsPerHash)
		{
			ctx->device_threads = maxThreadsPerBlock / threadsPerHash;
		}

		// cn_gpu specific optimizations
		if(useCryptonight_gpu)
		{
			constexpr size_t threads = 8;
			// Optimal block count by architecture
			// Empirically determined via profiling sweep (Session 48):
			//   Pascal:  6×SMs optimal — Phase 4 hits memory cliff at 7× (+4.7% vs 7×)
			//   Turing+: 8×SMs optimal — handles higher intensity well (+1.7% vs 6×)
			size_t blockOptimal;
			if(gpuArch / 10 == 6)       // Pascal (sm_6x)
				blockOptimal = 6 * ctx->device_mpcount;
			else                         // Turing+ (sm_7x and above)
				blockOptimal = 8 * ctx->device_mpcount;

			if(blockOptimal * threads * hashMemSize < limitedMemory)
				ctx->device_blocks = blockOptimal;
			else
				ctx->device_blocks = limitedMemory / hashMemSize / threads;
			ctx->device_threads = threads;
		}
	}

	if(ctx->device_threads * threadsPerHash > ctx->device_maxThreadsPerBlock)
	{
		// by default cryptonight CUDA implementations uses 8 threads per thread for some kernel
		ctx->device_threads = ctx->device_maxThreadsPerBlock / threadsPerHash;
		printf("WARNING: 'threads' configuration to large, value adjusted to %i\n", ctx->device_threads);
	}
	printf("device init succeeded\n");

	return 0;
}

extern "C" int cryptonight_extra_cpu_init(nvid_ctx* ctx)
{
	cudaError_t err;
	err = cudaSetDevice(ctx->device_id);
	if(err != cudaSuccess)
	{
		printf("GPU %d: %s", ctx->device_id, cudaGetErrorString(err));
		return 0;
	}

	CUDA_CHECK(ctx->device_id, cudaDeviceReset());
	switch(ctx->syncMode)
	{
	case 0:
		CUDA_CHECK(ctx->device_id, cudaSetDeviceFlags(cudaDeviceScheduleAuto));
		break;
	case 1:
		CUDA_CHECK(ctx->device_id, cudaSetDeviceFlags(cudaDeviceScheduleSpin));
		break;
	case 2:
		CUDA_CHECK(ctx->device_id, cudaSetDeviceFlags(cudaDeviceScheduleYield));
		break;
	case 3:
		CUDA_CHECK(ctx->device_id, cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync));
		break;
	};

	// prefer shared memory over L1 cache
	CUDA_CHECK(ctx->device_id, cudaDeviceSetCacheConfig(cudaFuncCachePreferShared));

	const size_t hashMemSize = ::jconf::inst()->GetMiningMemSize();

	size_t wsize = ctx->device_blocks * ctx->device_threads;
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_ctx_state, 50 * sizeof(uint32_t) * wsize));
	// get the cudaRT context
	CU_CHECK(ctx->device_id, cuCtxGetCurrent(&ctx->cuContext));

	// cn_gpu uses standard 4 * uint32_t ctx_b
	size_t ctx_b_size = 4 * sizeof(uint32_t) * wsize;
	// No state2 needed (cn_gpu uses cn_explode_gpu, not phase1)
	ctx->d_ctx_state2 = ctx->d_ctx_state;

	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_ctx_key1, 40 * sizeof(uint32_t) * wsize));
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_ctx_key2, 40 * sizeof(uint32_t) * wsize));
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_ctx_text, 32 * sizeof(uint32_t) * wsize));
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_ctx_a, 4 * sizeof(uint32_t) * wsize));
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_ctx_b, ctx_b_size));
	// POW block format http://monero.wikia.com/wiki/PoW_Block_Header_Format
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_input, 32 * sizeof(uint32_t)));
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_result_count, sizeof(uint32_t)));
	CUDA_CHECK(ctx->device_id, cudaMalloc(&ctx->d_result_nonce, 10 * sizeof(uint32_t)));
	CUDA_CHECK_MSG(
		ctx->device_id,
		"\n**suggestion: Try to reduce the value of the attribute 'threads' in the NVIDIA config file.**",
		cudaMalloc(&ctx->d_long_state, hashMemSize * wsize));
	return 1;
}
