/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  */

// OclCryptonightR_gen removed — CryptonightR support stripped
#include "gpu.hpp"
#include "gpu_device.hpp"
#include "gpu_platform.hpp"
#include "gpu_utils.hpp"

#include "n0s/backend/cryptonight.hpp"
#include "n0s/jconf.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/net/msgstruct.hpp"
#include "n0s/params.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Namespace-scoped functions now in separate files:
// - gpu_utils: string_replace_all, create_directory, port_sleep, LoadTextFile
// - gpu_platform: getNumPlatforms, getAMDDevices, getAMDPlatformIdx
// - gpu_device: InitOpenCLGpu, updateTimings, interleaveAdjustDelay

namespace n0s {
namespace amd {

// InitOpenCL: Initialize OpenCL context and compile kernels for all requested GPUs
// Returns ERR_SUCCESS on success, ERR_STUPID_PARAMS or ERR_OCL_API on failure
size_t InitOpenCL(GpuContext* ctx, size_t num_gpus, size_t platform_idx)
{
	cl_int ret;
	cl_uint entries;

	if((ret = clGetPlatformIDs(0, nullptr, &entries)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clGetPlatformIDs for number of platforms.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// The number of platforms naturally is the index of the last platform plus one.
	if(entries <= platform_idx)
	{
		printer::inst()->print_msg(L1, "Selected OpenCL platform index %d doesn't exist.", platform_idx);
		return ERR_STUPID_PARAMS;
	}

	cl_platform_id PlatformIDList[entries];
	if((ret = clGetPlatformIDs(entries, PlatformIDList, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clGetPlatformIDs for platform ID information.", err_to_str(ret));
		return ERR_OCL_API;
	}

	size_t infoSize;
	clGetPlatformInfo(PlatformIDList[platform_idx], CL_PLATFORM_VENDOR, 0, nullptr, &infoSize);
	std::vector<char> platformNameVec(infoSize);
	clGetPlatformInfo(PlatformIDList[platform_idx], CL_PLATFORM_VENDOR, infoSize, platformNameVec.data(), nullptr);
	std::string platformName(platformNameVec.data());
	if(n0s::params::inst().openCLVendor == "AMD" && platformName.find("Advanced Micro Devices") == std::string::npos)
	{
		printer::inst()->print_msg(L1, "WARNING: using non AMD device: %s", platformName.c_str());
	}

	if((ret = clGetDeviceIDs(PlatformIDList[platform_idx], CL_DEVICE_TYPE_GPU, 0, nullptr, &entries)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clGetDeviceIDs for number of devices.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Same as the platform index sanity check, except we must check all requested device indexes
	for(size_t i = 0; i < num_gpus; ++i)
	{
		if(ctx[i].deviceIdx >= entries)
		{
			printer::inst()->print_msg(L1, "Selected OpenCL device index %lu doesn't exist.\n", ctx[i].deviceIdx);
			return ERR_STUPID_PARAMS;
		}
	}

	cl_device_id DeviceIDList[entries];
	if((ret = clGetDeviceIDs(PlatformIDList[platform_idx], CL_DEVICE_TYPE_GPU, entries, DeviceIDList, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clGetDeviceIDs for device ID information.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Indexes sanity checked above
	std::vector<cl_device_id> TempDeviceList(num_gpus, nullptr);

	printer::inst()->print_msg(LDEBUG, "Number of OpenCL GPUs %d", entries);
	for(size_t i = 0; i < num_gpus; ++i)
	{
		ctx[i].DeviceID = DeviceIDList[ctx[i].deviceIdx];
		TempDeviceList[i] = DeviceIDList[ctx[i].deviceIdx];
	}

	cl_context opencl_ctx = clCreateContext(nullptr, num_gpus, TempDeviceList.data(), nullptr, nullptr, &ret);
	if(ret != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clCreateContext.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// fastIntMathV2CL and fastDivHeavyCL removed — dead kernels for removed algorithms
	const char* cryptonightCL =
#include "./opencl/cryptonight.cl"
		;
	const char* wolfAesCL =
#include "./opencl/wolf-aes.cl"
		;
	const char* cryptonight_gpu =
#include "./opencl/cryptonight_gpu.cl"
		;

	std::string source_code(cryptonightCL);
	string_replace_all(source_code, "N0S_INCLUDE_WOLF_AES", wolfAesCL);
	string_replace_all(source_code, "N0S_INCLUDE_CN_GPU", cryptonight_gpu);

	// create a directory  for the OpenCL compile cache
	const std::string cache_dir = n0s::params::inst().rootAMDCacheDir;
	create_directory(cache_dir);

	std::vector<std::shared_ptr<InterleaveData>> interleaveData(num_gpus, nullptr);

	for(size_t i = 0; i < num_gpus; ++i)
	{
		printer::inst()->print_msg(LDEBUG, "OpenCL Init device %d", ctx[i].deviceIdx);
		const size_t devIdx = ctx[i].deviceIdx;
		if(interleaveData.size() <= devIdx)
		{
			interleaveData.resize(devIdx + 1u, nullptr);
		}
		if(!interleaveData[devIdx])
		{
			interleaveData[devIdx] = std::make_unique<InterleaveData>();
			interleaveData[devIdx]->lastRunTimeStamp = get_timestamp_ms();
		}
		ctx[i].idWorkerOnDevice = interleaveData[devIdx]->numThreadsOnGPU;
		++interleaveData[devIdx]->numThreadsOnGPU;
		ctx[i].interleaveData = interleaveData[devIdx];
		ctx[i].interleaveData->adjustThreshold = static_cast<double>(ctx[i].interleave) / 100.0;
		ctx[i].interleaveData->startAdjustThreshold = ctx[i].interleaveData->adjustThreshold;
		ctx[i].opencl_ctx = opencl_ctx;

		if((ret = InitOpenCLGpu(ctx->opencl_ctx, &ctx[i], source_code.c_str())) != ERR_SUCCESS)
		{
			return ret;
		}
	}

	return ERR_SUCCESS;
}

[[nodiscard]] size_t XMRSetJob(GpuContext* ctx, uint8_t* input, size_t input_len, uint64_t target)
{

	auto& Kernels = ctx->Kernels;

	cl_int ret;

	if(input_len > 124)
		return ERR_STUPID_PARAMS;

	input[input_len] = 0x01;
	memset(input + input_len + 1, 0, 128 - input_len - 1);

	cl_uint numThreads = ctx->rawIntensity;

	if((ret = clEnqueueWriteBuffer(ctx->CommandQueues, ctx->InputBuffer, CL_TRUE, 0, 128, input, 0, nullptr, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clEnqueueWriteBuffer to fill input buffer.", err_to_str(ret));
		return ERR_OCL_API;
	}

	if((ret = clSetKernelArg(Kernels[0], 0, sizeof(cl_mem), &ctx->InputBuffer)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 0, argument 0.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Scratchpads
	if((ret = clSetKernelArg(Kernels[0], 1, sizeof(cl_mem), ctx->ExtraBuffers + 0)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 0, argument 1.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// States
	if((ret = clSetKernelArg(Kernels[0], 2, sizeof(cl_mem), ctx->ExtraBuffers + 1)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 0, argument 2.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Threads
	if((ret = clSetKernelArg(Kernels[0], 3, sizeof(cl_uint), &numThreads)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 0, argument 3.", err_to_str(ret));
		return (ERR_OCL_API);
	}

	// cn_gpu additional scratchpad preparation kernel (Kernel 7)
	if((ret = clSetKernelArg(Kernels[3], 0, sizeof(cl_mem), ctx->ExtraBuffers + 0)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 7, argument 0.", err_to_str(ret));
		return ERR_OCL_API;
	}

	if((ret = clSetKernelArg(Kernels[3], 1, sizeof(cl_mem), ctx->ExtraBuffers + 1)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 7, argument 1.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// CN1 Kernel

	// Scratchpads
	if((ret = clSetKernelArg(Kernels[1], 0, sizeof(cl_mem), ctx->ExtraBuffers + 0)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 1, argument 0.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// States
	if((ret = clSetKernelArg(Kernels[1], 1, sizeof(cl_mem), ctx->ExtraBuffers + 1)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 1, argument 1.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Threads
	if((ret = clSetKernelArg(Kernels[1], 2, sizeof(cl_uint), &numThreads)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 1, argument 2.", err_to_str(ret));
		return (ERR_OCL_API);
	}

	// CN3 Kernel
	// Scratchpads
	if((ret = clSetKernelArg(Kernels[2], 0, sizeof(cl_mem), ctx->ExtraBuffers + 0)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 2, argument 0.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// States
	if((ret = clSetKernelArg(Kernels[2], 1, sizeof(cl_mem), ctx->ExtraBuffers + 1)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 2, argument 1.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// cn_gpu: CN3 kernel gets Output, Target, Threads
	// Output
	if((ret = clSetKernelArg(Kernels[2], 2, sizeof(cl_mem), &ctx->OutputBuffer)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 2, argument 2.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Target
	if((ret = clSetKernelArg(Kernels[2], 3, sizeof(cl_ulong), &target)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 2, argument 3.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Threads
	if((ret = clSetKernelArg(Kernels[2], 4, sizeof(cl_uint), &numThreads)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clSetKernelArg for kernel 2, argument 4.", err_to_str(ret));
		return (ERR_OCL_API);
	}

	return ERR_SUCCESS;
}

[[nodiscard]] size_t XMRRunJob(GpuContext* ctx, cl_uint* HashOutput)
{
	const auto& Kernels = ctx->Kernels;

	cl_int ret;
	cl_uint zero = 0;

	size_t g_intensity = ctx->rawIntensity;
	size_t w_size = ctx->workSize;
	size_t g_thd = g_intensity;

	if(ctx->compMode)
	{
		// round up to next multiple of w_size
		g_thd = ((g_intensity + w_size - 1u) / w_size) * w_size;
		assert(g_thd % w_size == 0);
	}

	// Branch buffer zeroing removed — cn_gpu doesn't use branch dispatch

	if((ret = clEnqueueWriteBuffer(ctx->CommandQueues, ctx->OutputBuffer, CL_FALSE, sizeof(cl_uint) * 0xFF, sizeof(cl_uint), &zero, 0, nullptr, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clEnqueueWriteBuffer to fetch results.", err_to_str(ret));
		return ERR_OCL_API;
	}

	size_t Nonce[2] = {ctx->Nonce, 1}, gthreads[2] = {g_thd, 8}, lthreads[2] = {8, 8};
	if((ret = clEnqueueNDRangeKernel(ctx->CommandQueues, Kernels[0], 2, Nonce, gthreads, lthreads, 0, nullptr, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clEnqueueNDRangeKernel for kernel %d.", err_to_str(ret), 0);
		return ERR_OCL_API;
	}

	// cn_gpu: run scratchpad preparation kernel (Kernel 7), then main kernel (Kernel 1)
	{
		size_t thd = 64;
		size_t intens = g_intensity * thd;
		if((ret = clEnqueueNDRangeKernel(ctx->CommandQueues, Kernels[3], 1, 0, &intens, &thd, 0, nullptr, nullptr)) != CL_SUCCESS)
		{
			printer::inst()->print_msg(L1, "Error %s when calling clEnqueueNDRangeKernel for kernel %d.", err_to_str(ret), 7);
			return ERR_OCL_API;
		}

		size_t w_size_cn_gpu = w_size * 16;
		size_t g_thd_cn_gpu = g_thd * 16;

		if((ret = clEnqueueNDRangeKernel(ctx->CommandQueues, Kernels[1], 1, 0, &g_thd_cn_gpu, &w_size_cn_gpu, 0, nullptr, nullptr)) != CL_SUCCESS)
		{
			printer::inst()->print_msg(L1, "Error %s when calling clEnqueueNDRangeKernel for kernel %d.", err_to_str(ret), 1);
			return ERR_OCL_API;
		}
	}

	size_t  NonceT[2] = {0, ctx->Nonce}, gthreadsT[2] = {8, g_thd}, lthreadsT[2] = {8 , w_size};
	if((ret = clEnqueueNDRangeKernel(ctx->CommandQueues, Kernels[2], 2, NonceT, gthreadsT, lthreadsT, 0, nullptr, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clEnqueueNDRangeKernel for kernel %d.", err_to_str(ret), 2);
			return ERR_OCL_API;
	}

	// cn_gpu does not use branch kernels (Kernels 3-6) — final hash done in Kernel 2

	// this call is blocking therefore the access to the results without cl_finish is fine
	if((ret = clEnqueueReadBuffer(ctx->CommandQueues, ctx->OutputBuffer, CL_TRUE, 0, sizeof(cl_uint) * 0x100, HashOutput, 0, nullptr, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clEnqueueReadBuffer to fetch results.", err_to_str(ret));
		return ERR_OCL_API;
	}

	auto& numHashValues = HashOutput[0xFF];
	// avoid out of memory read, we have only storage for 0xFF results
	if(numHashValues > 0xFF)
		numHashValues = 0xFF;
	ctx->Nonce += g_intensity;

	return ERR_SUCCESS;
}

} // namespace amd
} // namespace n0s
