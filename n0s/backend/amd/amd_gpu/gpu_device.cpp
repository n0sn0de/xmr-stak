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

#include "gpu_device.hpp"
#include "gpu_utils.hpp"
#include "n0s/backend/cryptonight.hpp"
#include "n0s/jconf.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/misc/executor.hpp"
#include "n0s/params.hpp"
#include "n0s/vendor/picosha2/picosha2.hpp"
#include "n0s/version.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace n0s
{
namespace amd
{

size_t InitOpenCLGpu(cl_context opencl_ctx, GpuContext* ctx, const char* source_code)
{
	size_t MaximumWorkSize;
	cl_int ret;

	if((ret = clGetDeviceInfo(ctx->DeviceID, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &MaximumWorkSize, nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when querying a device's max worksize using clGetDeviceInfo.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// cn_gpu uses 16 threads per hash
	{
		MaximumWorkSize /= 16;
	}
	printer::inst()->print_msg(L1, "Device %lu work size %lu / %lu.", ctx->deviceIdx, ctx->workSize, MaximumWorkSize);

	if(ctx->workSize > MaximumWorkSize)
	{
		ctx->workSize = MaximumWorkSize;
		printer::inst()->print_msg(L1, "Device %lu work size to large, reduce to %lu / %lu.", ctx->deviceIdx, ctx->workSize, MaximumWorkSize);
	}

	const std::string backendName = n0s::params::inst().openCLVendor;

#if defined(CL_VERSION_2_0) && !defined(CONF_ENFORCE_OpenCL_1_2)
	const cl_queue_properties CommandQueueProperties[] = {0, 0, 0};
	ctx->CommandQueues = clCreateCommandQueueWithProperties(opencl_ctx, ctx->DeviceID, CommandQueueProperties, &ret);
#else
	const cl_command_queue_properties CommandQueueProperties = {0};
	ctx->CommandQueues = clCreateCommandQueue(opencl_ctx, ctx->DeviceID, CommandQueueProperties, &ret);
#endif

	if(ret != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clCreateCommandQueueWithProperties.", err_to_str(ret));
		return ERR_OCL_API;
	}

	if((ret = clGetDeviceInfo(ctx->DeviceID, CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(int), &(ctx->computeUnits), nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "WARNING: %s when calling clGetDeviceInfo to get CL_DEVICE_MAX_COMPUTE_UNITS for device %u.", err_to_str(ret), static_cast<uint32_t>(ctx->deviceIdx));
		return ERR_OCL_API;
	}

	ctx->InputBuffer = clCreateBuffer(opencl_ctx, CL_MEM_READ_ONLY, 128, nullptr, &ret);
	if(ret != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clCreateBuffer to create input buffer.", err_to_str(ret));
		return ERR_OCL_API;
	}

	const size_t scratchPadSize = ::jconf::inst()->GetMiningMemSize();

	size_t g_thd = ctx->rawIntensity;
	ctx->ExtraBuffers[0] = clCreateBuffer(opencl_ctx, CL_MEM_READ_WRITE, scratchPadSize * g_thd, nullptr, &ret);
	if(ret != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clCreateBuffer to create hash scratchpads buffer.", err_to_str(ret));
		return ERR_OCL_API;
	}

	ctx->ExtraBuffers[1] = clCreateBuffer(opencl_ctx, CL_MEM_READ_WRITE, 200 * g_thd, nullptr, &ret);
	if(ret != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clCreateBuffer to create hash states buffer.", err_to_str(ret));
		return ERR_OCL_API;
	}

	// Branch buffers removed — cn_gpu doesn't use Blake/Groestl/JH/Skein branch dispatch

	// Assume we may find up to 0xFF nonces in one run - it's reasonable
	ctx->OutputBuffer = clCreateBuffer(opencl_ctx, CL_MEM_READ_WRITE, sizeof(cl_uint) * 0x100, nullptr, &ret);
	if(ret != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "Error %s when calling clCreateBuffer to create output buffer.", err_to_str(ret));
		return ERR_OCL_API;
	}

	std::vector<char> devNameVec(1024);
	if((ret = clGetDeviceInfo(ctx->DeviceID, CL_DEVICE_NAME, devNameVec.size(), devNameVec.data(), nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "WARNING: %s when calling clGetDeviceInfo to get CL_DEVICE_NAME for device %u.", err_to_str(ret), ctx->deviceIdx);
		return ERR_OCL_API;
	}

	std::vector<char> openCLDriverVer(1024);
	if((ret = clGetDeviceInfo(ctx->DeviceID, CL_DRIVER_VERSION, openCLDriverVer.size(), openCLDriverVer.data(), nullptr)) != CL_SUCCESS)
	{
		printer::inst()->print_msg(L1, "WARNING: %s when calling clGetDeviceInfo to get CL_DRIVER_VERSION for device %u.", err_to_str(ret), ctx->deviceIdx);
		return ERR_OCL_API;
	}

	{
		// Only one algorithm: cryptonight_gpu
		const auto miner_algo = ::jconf::inst()->GetMiningAlgo();
		const size_t hashMemSize = miner_algo.Mem();
		const int threadMemMask = miner_algo.Mask();
		const int hashIterations = miner_algo.Iter();

		// if intensity is a multiple of worksize then comp mode is not needed
		int needCompMode = ctx->compMode && ctx->rawIntensity % ctx->workSize != 0 ? 1 : 0;

		std::string options;
		options += " -DITERATIONS=" + std::to_string(hashIterations);
		options += " -DMASK=" + std::to_string(threadMemMask) + "U";
		options += " -DWORKSIZE=" + std::to_string(ctx->workSize) + "U";
		options += " -DCOMP_MODE=" + std::to_string(needCompMode);
		options += " -DMEMORY=" + std::to_string(hashMemSize) + "LU";
		options += " -DALGO=" + std::to_string(miner_algo.Id());
		options += " -DCN_UNROLL=" + std::to_string(ctx->unroll);
		options += " -DPHASE2_WORKSIZE=512";
		/* AMD driver output is something like: `1445.5 (VM)`
		 * and is mapped to `14` only. The value is only used for a compiler
		 * workaround.
		 */
		options += " -DOPENCL_DRIVER_MAJOR=" + std::to_string(std::stoi(openCLDriverVer.data()) / 100);

		options += " -DIS_WINDOWS_OS=0"; // always Linux

		// cn_gpu requires IEEE 754 compliant float math
		options += " -cl-fp32-correctly-rounded-divide-sqrt";

		/* create a hash for the compile time cache
		 * used data:
		 *   - source code
		 *   - device name
		 *   - compile parameter
		 */
		std::string src_str(source_code);
		src_str += options;
		src_str += devNameVec.data();
		src_str += get_version_str();
		src_str += openCLDriverVer.data();

		std::string hash_hex_str;
		picosha2::hash256_hex_string(src_str, hash_hex_str);

		const std::string cache_dir = n0s::params::inst().rootAMDCacheDir;

		std::string cache_file = cache_dir + hash_hex_str + ".openclbin";
		std::ifstream clBinFile(cache_file, std::ofstream::in | std::ofstream::binary);
		if(n0s::params::inst().AMDCache == false || !clBinFile.good())
		{
			if(n0s::params::inst().AMDCache)
				printer::inst()->print_msg(L1, "OpenCL device %u - Precompiled code %s not found. Compiling ...", ctx->deviceIdx, cache_file.c_str());
			ctx->Program = clCreateProgramWithSource(opencl_ctx, 1, (const char**)&source_code, nullptr, &ret);
			if(ret != CL_SUCCESS)
			{
				printer::inst()->print_msg(L1, "Error %s when calling clCreateProgramWithSource on the OpenCL miner code", err_to_str(ret));
				return ERR_OCL_API;
			}

			ret = clBuildProgram(ctx->Program, 1, &ctx->DeviceID, options.c_str(), nullptr, nullptr);
			if(ret != CL_SUCCESS)
			{
				size_t len;
				printer::inst()->print_msg(L1, "Error %s when calling clBuildProgram.", err_to_str(ret));

				if((ret = clGetProgramBuildInfo(ctx->Program, ctx->DeviceID, CL_PROGRAM_BUILD_LOG, 0, nullptr, &len)) != CL_SUCCESS)
				{
					printer::inst()->print_msg(L1, "Error %s when calling clGetProgramBuildInfo for length of build log output.", err_to_str(ret));
					return ERR_OCL_API;
				}

				char* BuildLog = static_cast<char*>(malloc(len + 1));
				BuildLog[0] = '\0';

				if((ret = clGetProgramBuildInfo(ctx->Program, ctx->DeviceID, CL_PROGRAM_BUILD_LOG, len, BuildLog, nullptr)) != CL_SUCCESS)
				{
					free(BuildLog);
					printer::inst()->print_msg(L1, "Error %s when calling clGetProgramBuildInfo for build log.", err_to_str(ret));
					return ERR_OCL_API;
				}

				printer::inst()->print_str("Build log:\n");
				std::cerr << BuildLog << std::endl;

				free(BuildLog);
				return ERR_OCL_API;
			}

			cl_uint num_devices;
			clGetProgramInfo(ctx->Program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &num_devices, nullptr);

			std::vector<cl_device_id> devices_ids(num_devices);
			clGetProgramInfo(ctx->Program, CL_PROGRAM_DEVICES, sizeof(cl_device_id) * devices_ids.size(), devices_ids.data(), nullptr);
			int dev_id = 0;
			/* Search for the gpu within the program context.
			 * The id can be different to  ctx->DeviceID.
			 */
			for(auto& ocl_device : devices_ids)
			{
				if(ocl_device == ctx->DeviceID)
					break;
				dev_id++;
			}

			cl_build_status status;
			do
			{
				if((ret = clGetProgramBuildInfo(ctx->Program, ctx->DeviceID, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &status, nullptr)) != CL_SUCCESS)
				{
					printer::inst()->print_msg(L1, "Error %s when calling clGetProgramBuildInfo for status of build.", err_to_str(ret));
					return ERR_OCL_API;
				}
				port_sleep(1);
			} while(status == CL_BUILD_IN_PROGRESS);

			if(n0s::params::inst().AMDCache)
			{
				std::vector<size_t> binary_sizes(num_devices);
				clGetProgramInfo(ctx->Program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t) * binary_sizes.size(), binary_sizes.data(), nullptr);

				std::vector<char*> all_programs(num_devices);
				std::vector<std::vector<char>> program_storage;

				int p_id = 0;
				size_t mem_size = 0;
				// create memory  structure to query all OpenCL program binaries
				for([[maybe_unused]] auto& p : all_programs)
				{
					program_storage.emplace_back(std::vector<char>(binary_sizes[p_id]));
					all_programs[p_id] = program_storage[p_id].data();
					mem_size += binary_sizes[p_id];
					p_id++;
				}

				if((ret = clGetProgramInfo(ctx->Program, CL_PROGRAM_BINARIES, num_devices * sizeof(char*), all_programs.data(), nullptr)) != CL_SUCCESS)
				{
					printer::inst()->print_msg(L1, "Error %s when calling clGetProgramInfo.", err_to_str(ret));
					return ERR_OCL_API;
				}

				std::ofstream file_stream;
				file_stream.open(cache_file, std::ofstream::out | std::ofstream::binary);
				file_stream.write(all_programs[dev_id], binary_sizes[dev_id]);
				file_stream.close();
				printer::inst()->print_msg(L1, "OpenCL device %u - Precompiled code stored in file %s", ctx->deviceIdx, cache_file.c_str());
			}
		}
		else
		{
			printer::inst()->print_msg(L1, "OpenCL device %u - Load precompiled code from file %s", ctx->deviceIdx, cache_file.c_str());
			std::ostringstream ss;
			ss << clBinFile.rdbuf();
			std::string s = ss.str();

			size_t bin_size = s.size();
			auto data_ptr = s.data();

			cl_int clStatus;
			ctx->Program = clCreateProgramWithBinary(
				opencl_ctx, 1, &ctx->DeviceID, &bin_size,
				(const unsigned char**)&data_ptr, &clStatus, &ret);
			if(ret != CL_SUCCESS)
			{
				printer::inst()->print_msg(L1, "Error %s when calling clCreateProgramWithBinary. Try to delete file %s", err_to_str(ret), cache_file.c_str());
				return ERR_OCL_API;
			}
			ret = clBuildProgram(ctx->Program, 1, &ctx->DeviceID, nullptr, nullptr, nullptr);
			if(ret != CL_SUCCESS)
			{
				printer::inst()->print_msg(L1, "Error %s when calling clBuildProgram. Try to delete file %s", err_to_str(ret), cache_file.c_str());
				return ERR_OCL_API;
			}
		}

		// cn_gpu kernel names — explicit, no more JOIN(name,ALGO) indirection
		// [0] Phase 1: Keccak hash of input
		// [1] Phase 3: GPU floating-point computation
		// [2] Phase 4+5: Implode + finalize
		// [3] Phase 2: Scratchpad expansion
		static const char* KernelNames[4] = {
			"cn_gpu_phase1_keccak",
			"cn_gpu_phase3_compute",
			"cn_gpu_phase4_finalize",
			"cn_gpu_phase2_expand",
		};

		for(int i = 0; i < 4; ++i)
		{
			ctx->Kernels[i] = clCreateKernel(ctx->Program, KernelNames[i], &ret);
			if(ret != CL_SUCCESS)
			{
				printer::inst()->print_msg(L1, "Error %s when calling clCreateKernel for %s.", err_to_str(ret), KernelNames[i]);
				return ERR_OCL_API;
			}
		}
	}
	ctx->Nonce = 0;
	return 0;
}

uint64_t updateTimings(GpuContext* ctx, const uint64_t t)
{
	// averagingBias = 1.0 - only the last delta time is taken into account
	// averagingBias = 0.5 - the last delta time has the same weight as all the previous ones combined
	// averagingBias = 0.1 - the last delta time has 10% weight of all the previous ones combined
	const double averagingBias = 0.1;

	int64_t t2 = get_timestamp_ms();
	uint64_t runtime = (t2 - t);
	{
		std::lock_guard<std::mutex> g(ctx->interleaveData->mutex);
		// 20000 mean that something went wrong an we reset the average
		if(ctx->interleaveData->avgKernelRuntime == 0.0 || ctx->interleaveData->avgKernelRuntime > 20000.0)
			ctx->interleaveData->avgKernelRuntime = runtime;
		else
			ctx->interleaveData->avgKernelRuntime = ctx->interleaveData->avgKernelRuntime * (1.0 - averagingBias) + (runtime)*averagingBias;
	}
	return runtime;
}

uint64_t interleaveAdjustDelay(GpuContext* ctx, const bool enableAutoAdjustment)
{
	uint64_t t0 = get_timestamp_ms();

	if(ctx->interleaveData->numThreadsOnGPU > 1 && ctx->interleaveData->adjustThreshold > 0.0)
	{
		t0 = get_timestamp_ms();
		std::unique_lock<std::mutex> g(ctx->interleaveData->mutex);

		int64_t delay = 0;
		double dt = 0.0;

		if(t0 > ctx->interleaveData->lastRunTimeStamp)
			dt = static_cast<double>(t0 - ctx->interleaveData->lastRunTimeStamp);

		const double avgRuntime = ctx->interleaveData->avgKernelRuntime;
		const double optimalTimeOffset = avgRuntime * ctx->interleaveData->adjustThreshold;

		// threshold where the the auto adjustment is disabled
		constexpr uint32_t maxDelay = 10;
		constexpr double maxAutoAdjust = 0.05;

		if((dt > 0) && (dt < optimalTimeOffset))
		{
			delay = static_cast<int64_t>((optimalTimeOffset - dt));

			if(enableAutoAdjustment)
			{
				if(ctx->lastDelay == static_cast<uint64_t>(delay) && static_cast<uint64_t>(delay) > maxDelay)
					ctx->interleaveData->adjustThreshold -= 0.001;
				// if the delay doubled than increase the adjustThreshold
				else if(delay > 1 && ctx->lastDelay * 2 < static_cast<uint64_t>(delay))
					ctx->interleaveData->adjustThreshold += 0.001;
			}
			ctx->lastDelay = static_cast<uint64_t>(delay);

			ctx->interleaveData->adjustThreshold = std::clamp(ctx->interleaveData->adjustThreshold,
				ctx->interleaveData->startAdjustThreshold - maxAutoAdjust,
				ctx->interleaveData->startAdjustThreshold + maxAutoAdjust);

			// avoid that the auto adjustment is disable interleaving
			ctx->interleaveData->adjustThreshold = std::max(
				ctx->interleaveData->adjustThreshold,
				0.001);
		}
		delay = std::max(int64_t(0), delay);

		ctx->interleaveData->lastRunTimeStamp = t0 + delay;

		g.unlock();
		if(delay > 0)
		{
			// do not notify the user anymore if we reach a good delay
			if(delay > maxDelay)
				printer::inst()->print_msg(L1, "OpenCL Interleave %u|%u: %u/%.2lf ms - %.1lf",
					ctx->deviceIdx,
					ctx->idWorkerOnDevice,
					static_cast<uint32_t>(delay),
					avgRuntime,
					ctx->interleaveData->adjustThreshold * 100.);

			std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		}
	}

	return t0;
}

} // namespace amd
} // namespace n0s
