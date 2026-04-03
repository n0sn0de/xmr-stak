/**
 * nvml_wrapper.hpp — Runtime dynamic NVML loading
 *
 * Loads libnvidia-ml.so.1 (Linux) or nvml.dll (Windows) at runtime
 * via dlopen/LoadLibrary. Zero compile-time dependency on NVML headers
 * or libraries — the binary works on AMD-only systems.
 *
 * Functions are resolved via dlsym/GetProcAddress on first use.
 * If NVML is unavailable, all queries gracefully fail.
 */

#pragma once

#include <cstdint>
#include <string>

namespace n0s
{
namespace nvml
{

// ─── Minimal NVML type replicas (avoid #include <nvml.h>) ───────────────────

using nvmlReturn_t = unsigned int;
using nvmlDevice_t = void*;

constexpr nvmlReturn_t NVML_SUCCESS = 0;
constexpr unsigned int NVML_TEMPERATURE_GPU = 0;
constexpr unsigned int NVML_CLOCK_GRAPHICS = 0;
constexpr unsigned int NVML_CLOCK_MEM = 2;
constexpr unsigned int NVML_DEVICE_NAME_BUFFER_SIZE = 96;

// ─── Function pointer types ─────────────────────────────────────────────────

using fn_nvmlInit_v2 = nvmlReturn_t (*)();
using fn_nvmlShutdown = nvmlReturn_t (*)();
using fn_nvmlDeviceGetCount_v2 = nvmlReturn_t (*)(unsigned int*);
using fn_nvmlDeviceGetHandleByIndex_v2 = nvmlReturn_t (*)(unsigned int, nvmlDevice_t*);
using fn_nvmlDeviceGetName = nvmlReturn_t (*)(nvmlDevice_t, char*, unsigned int);
using fn_nvmlDeviceGetTemperature = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);
using fn_nvmlDeviceGetPowerUsage = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
using fn_nvmlDeviceGetFanSpeed = nvmlReturn_t (*)(nvmlDevice_t, unsigned int*);
using fn_nvmlDeviceGetClockInfo = nvmlReturn_t (*)(nvmlDevice_t, unsigned int, unsigned int*);

// ─── NVML runtime state ─────────────────────────────────────────────────────

struct NvmlLib
{
	void* handle = nullptr;  // dlopen/LoadLibrary handle
	bool initialized = false;
	bool load_attempted = false;
	bool load_failed = false;

	fn_nvmlInit_v2 Init = nullptr;
	fn_nvmlShutdown Shutdown = nullptr;
	fn_nvmlDeviceGetCount_v2 DeviceGetCount = nullptr;
	fn_nvmlDeviceGetHandleByIndex_v2 DeviceGetHandleByIndex = nullptr;
	fn_nvmlDeviceGetName DeviceGetName = nullptr;
	fn_nvmlDeviceGetTemperature DeviceGetTemperature = nullptr;
	fn_nvmlDeviceGetPowerUsage DeviceGetPowerUsage = nullptr;
	fn_nvmlDeviceGetFanSpeed DeviceGetFanSpeed = nullptr;
	fn_nvmlDeviceGetClockInfo DeviceGetClockInfo = nullptr;
};

/// Try to load NVML. Safe to call multiple times (no-op after first attempt).
/// Returns true if NVML is available and initialized.
bool loadNvml();

/// Query if NVML is loaded and ready.
bool isNvmlAvailable();

/// Get the global NVML library handle (for direct function pointer calls).
/// Returns nullptr if NVML is not loaded.
NvmlLib* getNvml();

/// Shutdown NVML and unload the library.
void unloadNvml();

} // namespace nvml
} // namespace n0s
