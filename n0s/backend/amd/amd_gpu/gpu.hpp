#pragma once

#include "n0s/jconf.hpp"
#include "n0s/misc/console.hpp"

#include <CL/cl.h>

#include <array>
#include <memory>
#include <mutex>
#include <cstdint>
#include <string>
#include <vector>

#define ERR_SUCCESS (0)
#define ERR_OCL_API (2)
#define ERR_STUPID_PARAMS (1)

struct InterleaveData
{
	std::mutex mutex;

	double adjustThreshold = 0.4;
	double startAdjustThreshold = 0.4;
	double avgKernelRuntime = 0.0;
	uint64_t lastRunTimeStamp = 0;
	uint32_t numThreadsOnGPU = 0;
};

struct GpuContext
{
	/*Input vars*/
	size_t deviceIdx;
	size_t rawIntensity;
	size_t maxRawIntensity;
	size_t workSize;
	int unroll = 0;
	bool isNVIDIA = false;
	bool isAMD = false;
	int compMode;

	/*Output vars*/
	cl_device_id DeviceID;
	cl_command_queue CommandQueues;
	cl_mem InputBuffer;
	cl_mem OutputBuffer;
	cl_mem ExtraBuffers[2];  // [0]=scratchpad, [1]=states (200 bytes per thread)
	cl_context opencl_ctx = nullptr;
	cl_program Program = nullptr;
	std::array<cl_kernel, 4> Kernels = {};  // [0]=cn0, [1]=cn1, [2]=cn2, [3]=cn00
	size_t freeMem;
	size_t maxMemPerAlloc;
	int computeUnits;
	std::string name;
	std::shared_ptr<InterleaveData> interleaveData;
	uint32_t idWorkerOnDevice = 0u;
	int interleave = 40;
	uint64_t lastDelay = 0;

	uint32_t Nonce;
};

namespace
{
[[maybe_unused]] const char* err_to_str(cl_int ret)
{
	switch(ret)
	{
	case CL_SUCCESS:
		return "CL_SUCCESS";
	case CL_DEVICE_NOT_FOUND:
		return "CL_DEVICE_NOT_FOUND";
	case CL_DEVICE_NOT_AVAILABLE:
		return "CL_DEVICE_NOT_AVAILABLE";
	case CL_COMPILER_NOT_AVAILABLE:
		return "CL_COMPILER_NOT_AVAILABLE";
	case CL_MEM_OBJECT_ALLOCATION_FAILURE:
		return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
	case CL_OUT_OF_RESOURCES:
		return "CL_OUT_OF_RESOURCES";
	case CL_OUT_OF_HOST_MEMORY:
		return "CL_OUT_OF_HOST_MEMORY";
	case CL_PROFILING_INFO_NOT_AVAILABLE:
		return "CL_PROFILING_INFO_NOT_AVAILABLE";
	case CL_MEM_COPY_OVERLAP:
		return "CL_MEM_COPY_OVERLAP";
	case CL_IMAGE_FORMAT_MISMATCH:
		return "CL_IMAGE_FORMAT_MISMATCH";
	case CL_IMAGE_FORMAT_NOT_SUPPORTED:
		return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
	case CL_BUILD_PROGRAM_FAILURE:
		return "CL_BUILD_PROGRAM_FAILURE";
	case CL_MAP_FAILURE:
		return "CL_MAP_FAILURE";
	case CL_MISALIGNED_SUB_BUFFER_OFFSET:
		return "CL_MISALIGNED_SUB_BUFFER_OFFSET";
	case CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST:
		return "CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST";
#ifdef CL_VERSION_1_2
	case CL_COMPILE_PROGRAM_FAILURE:
		return "CL_COMPILE_PROGRAM_FAILURE";
	case CL_LINKER_NOT_AVAILABLE:
		return "CL_LINKER_NOT_AVAILABLE";
	case CL_LINK_PROGRAM_FAILURE:
		return "CL_LINK_PROGRAM_FAILURE";
	case CL_DEVICE_PARTITION_FAILED:
		return "CL_DEVICE_PARTITION_FAILED";
	case CL_KERNEL_ARG_INFO_NOT_AVAILABLE:
		return "CL_KERNEL_ARG_INFO_NOT_AVAILABLE";
#endif
	case CL_INVALID_VALUE:
		return "CL_INVALID_VALUE";
	case CL_INVALID_DEVICE_TYPE:
		return "CL_INVALID_DEVICE_TYPE";
	case CL_INVALID_PLATFORM:
		return "CL_INVALID_PLATFORM";
	case CL_INVALID_DEVICE:
		return "CL_INVALID_DEVICE";
	case CL_INVALID_CONTEXT:
		return "CL_INVALID_CONTEXT";
	case CL_INVALID_QUEUE_PROPERTIES:
		return "CL_INVALID_QUEUE_PROPERTIES";
	case CL_INVALID_COMMAND_QUEUE:
		return "CL_INVALID_COMMAND_QUEUE";
	case CL_INVALID_HOST_PTR:
		return "CL_INVALID_HOST_PTR";
	case CL_INVALID_MEM_OBJECT:
		return "CL_INVALID_MEM_OBJECT";
	case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR:
		return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
	case CL_INVALID_IMAGE_SIZE:
		return "CL_INVALID_IMAGE_SIZE";
	case CL_INVALID_SAMPLER:
		return "CL_INVALID_SAMPLER";
	case CL_INVALID_BINARY:
		return "CL_INVALID_BINARY";
	case CL_INVALID_BUILD_OPTIONS:
		return "CL_INVALID_BUILD_OPTIONS";
	case CL_INVALID_PROGRAM:
		return "CL_INVALID_PROGRAM";
	case CL_INVALID_PROGRAM_EXECUTABLE:
		return "CL_INVALID_PROGRAM_EXECUTABLE";
	case CL_INVALID_KERNEL_NAME:
		return "CL_INVALID_KERNEL_NAME";
	case CL_INVALID_KERNEL_DEFINITION:
		return "CL_INVALID_KERNEL_DEFINITION";
	case CL_INVALID_KERNEL:
		return "CL_INVALID_KERNEL";
	case CL_INVALID_ARG_INDEX:
		return "CL_INVALID_ARG_INDEX";
	case CL_INVALID_ARG_VALUE:
		return "CL_INVALID_ARG_VALUE";
	case CL_INVALID_ARG_SIZE:
		return "CL_INVALID_ARG_SIZE";
	case CL_INVALID_KERNEL_ARGS:
		return "CL_INVALID_KERNEL_ARGS";
	case CL_INVALID_WORK_DIMENSION:
		return "CL_INVALID_WORK_DIMENSION";
	case CL_INVALID_WORK_GROUP_SIZE:
		return "CL_INVALID_WORK_GROUP_SIZE";
	case CL_INVALID_WORK_ITEM_SIZE:
		return "CL_INVALID_WORK_ITEM_SIZE";
	case CL_INVALID_GLOBAL_OFFSET:
		return "CL_INVALID_GLOBAL_OFFSET";
	case CL_INVALID_EVENT_WAIT_LIST:
		return "CL_INVALID_EVENT_WAIT_LIST";
	case CL_INVALID_EVENT:
		return "CL_INVALID_EVENT";
	case CL_INVALID_OPERATION:
		return "CL_INVALID_OPERATION";
	case CL_INVALID_GL_OBJECT:
		return "CL_INVALID_GL_OBJECT";
	case CL_INVALID_BUFFER_SIZE:
		return "CL_INVALID_BUFFER_SIZE";
	case CL_INVALID_MIP_LEVEL:
		return "CL_INVALID_MIP_LEVEL";
	case CL_INVALID_GLOBAL_WORK_SIZE:
		return "CL_INVALID_GLOBAL_WORK_SIZE";
	case CL_INVALID_PROPERTY:
		return "CL_INVALID_PROPERTY";
#ifdef CL_VERSION_1_2
	case CL_INVALID_IMAGE_DESCRIPTOR:
		return "CL_INVALID_IMAGE_DESCRIPTOR";
	case CL_INVALID_COMPILER_OPTIONS:
		return "CL_INVALID_COMPILER_OPTIONS";
	case CL_INVALID_LINKER_OPTIONS:
		return "CL_INVALID_LINKER_OPTIONS";
	case CL_INVALID_DEVICE_PARTITION_COUNT:
		return "CL_INVALID_DEVICE_PARTITION_COUNT";
#endif
#if defined(CL_VERSION_2_0) && !defined(CONF_ENFORCE_OpenCL_1_2)
	case CL_INVALID_PIPE_SIZE:
		return "CL_INVALID_PIPE_SIZE";
	case CL_INVALID_DEVICE_QUEUE:
		return "CL_INVALID_DEVICE_QUEUE";
#endif
	default:
		return "UNKNOWN_ERROR";
	}
}
} // namespace

namespace n0s {
namespace amd {

/// Per-phase kernel timing data for profiling
struct KernelProfile
{
	int64_t phase1_us = 0;   ///< Phase 1: Keccak prepare (microseconds)
	int64_t phase2_us = 0;   ///< Phase 2: Scratchpad expand
	int64_t phase3_us = 0;   ///< Phase 3: GPU compute (main loop)
	int64_t phase45_us = 0;  ///< Phase 4+5: Implode + finalize
	int64_t total_us = 0;    ///< Total dispatch time
	int iterations = 0;      ///< Number of dispatch calls profiled

	void print_summary(size_t intensity) const;
	void reset() { *this = KernelProfile{}; }
};

// Platform discovery (see gpu_platform.hpp)
uint32_t getNumPlatforms();
[[nodiscard]] int getAMDPlatformIdx();
std::vector<GpuContext> getAMDDevices(int index);

// Device initialization (see gpu_device.hpp)  
[[nodiscard]] size_t InitOpenCLGpu(cl_context opencl_ctx, GpuContext* ctx, const char* source_code);
uint64_t updateTimings(GpuContext* ctx, uint64_t t);
uint64_t interleaveAdjustDelay(GpuContext* ctx, bool enableAutoAdjustment = true);

// Main OpenCL interface (see gpu.cpp)
[[nodiscard]] size_t InitOpenCL(GpuContext* ctx, size_t num_gpus, size_t platform_idx);
[[nodiscard]] size_t XMRSetJob(GpuContext* ctx, uint8_t* input, size_t input_len, uint64_t target);
[[nodiscard]] size_t XMRRunJob(GpuContext* ctx, cl_uint* HashOutput);
[[nodiscard]] size_t XMRRunJobProfile(GpuContext* ctx, cl_uint* HashOutput, KernelProfile& profile);

} // namespace amd
} // namespace n0s
