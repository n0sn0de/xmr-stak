/**
 * nvml_wrapper.cpp — Runtime dynamic NVML loading implementation
 *
 * Loads libnvidia-ml.so.1 (Linux) or nvml.dll (Windows) at runtime.
 * Falls back gracefully if NVML is unavailable (AMD-only systems, containers).
 */

#include "nvml_wrapper.hpp"
#include "n0s/misc/console.hpp"

#include <mutex>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace n0s
{
namespace nvml
{

namespace
{

NvmlLib g_nvml;
std::mutex g_nvmlMutex;

void* loadSymbol(void* handle, const char* name)
{
#ifdef _WIN32
	return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
	return dlsym(handle, name);
#endif
}

void* openLib(const char* name)
{
#ifdef _WIN32
	return static_cast<void*>(LoadLibraryA(name));
#else
	return dlopen(name, RTLD_LAZY);
#endif
}

void closeLib(void* handle)
{
	if(!handle) return;
#ifdef _WIN32
	FreeLibrary(static_cast<HMODULE>(handle));
#else
	dlclose(handle);
#endif
}

} // anonymous namespace

bool loadNvml()
{
	std::lock_guard<std::mutex> lock(g_nvmlMutex);

	if(g_nvml.initialized) return true;
	if(g_nvml.load_attempted) return false;

	g_nvml.load_attempted = true;

	// Try to load the NVML shared library
#ifdef _WIN32
	// On Windows, nvml.dll ships with NVIDIA driver
	const char* libNames[] = {"nvml.dll", nullptr};
#else
	// On Linux, libnvidia-ml.so.1 ships with NVIDIA driver
	const char* libNames[] = {
		"libnvidia-ml.so.1",
		"libnvidia-ml.so",
		nullptr
	};
#endif

	for(int i = 0; libNames[i] != nullptr; i++)
	{
		g_nvml.handle = openLib(libNames[i]);
		if(g_nvml.handle) break;
	}

	if(!g_nvml.handle)
	{
		g_nvml.load_failed = true;
		return false;
	}

	// Resolve function pointers
	// Note: nvmlInit_v2 and nvmlDeviceGetCount_v2 are the modern API names.
	// Older NVML versions may only have nvmlInit / nvmlDeviceGetCount.
	g_nvml.Init = reinterpret_cast<fn_nvmlInit_v2>(
		loadSymbol(g_nvml.handle, "nvmlInit_v2"));
	if(!g_nvml.Init)
	{
		g_nvml.Init = reinterpret_cast<fn_nvmlInit_v2>(
			loadSymbol(g_nvml.handle, "nvmlInit"));
	}

	g_nvml.Shutdown = reinterpret_cast<fn_nvmlShutdown>(
		loadSymbol(g_nvml.handle, "nvmlShutdown"));

	g_nvml.DeviceGetCount = reinterpret_cast<fn_nvmlDeviceGetCount_v2>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetCount_v2"));
	if(!g_nvml.DeviceGetCount)
	{
		g_nvml.DeviceGetCount = reinterpret_cast<fn_nvmlDeviceGetCount_v2>(
			loadSymbol(g_nvml.handle, "nvmlDeviceGetCount"));
	}

	g_nvml.DeviceGetHandleByIndex = reinterpret_cast<fn_nvmlDeviceGetHandleByIndex_v2>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetHandleByIndex_v2"));
	if(!g_nvml.DeviceGetHandleByIndex)
	{
		g_nvml.DeviceGetHandleByIndex = reinterpret_cast<fn_nvmlDeviceGetHandleByIndex_v2>(
			loadSymbol(g_nvml.handle, "nvmlDeviceGetHandleByIndex"));
	}

	g_nvml.DeviceGetName = reinterpret_cast<fn_nvmlDeviceGetName>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetName"));

	g_nvml.DeviceGetTemperature = reinterpret_cast<fn_nvmlDeviceGetTemperature>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetTemperature"));

	g_nvml.DeviceGetPowerUsage = reinterpret_cast<fn_nvmlDeviceGetPowerUsage>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetPowerUsage"));

	g_nvml.DeviceGetFanSpeed = reinterpret_cast<fn_nvmlDeviceGetFanSpeed>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetFanSpeed"));

	g_nvml.DeviceGetClockInfo = reinterpret_cast<fn_nvmlDeviceGetClockInfo>(
		loadSymbol(g_nvml.handle, "nvmlDeviceGetClockInfo"));

	// Minimum required: Init + DeviceGetHandleByIndex + at least temperature
	if(!g_nvml.Init || !g_nvml.DeviceGetHandleByIndex)
	{
		printer::inst()->print_msg(L1, "NVML: loaded library but missing required symbols");
		closeLib(g_nvml.handle);
		g_nvml.handle = nullptr;
		g_nvml.load_failed = true;
		return false;
	}

	// Initialize NVML
	nvmlReturn_t ret = g_nvml.Init();
	if(ret != NVML_SUCCESS)
	{
		printer::inst()->print_msg(L1, "NVML: nvmlInit failed (error %u)", ret);
		closeLib(g_nvml.handle);
		g_nvml.handle = nullptr;
		g_nvml.load_failed = true;
		return false;
	}

	g_nvml.initialized = true;
	printer::inst()->print_msg(L1, "NVML: initialized successfully (direct API telemetry)");
	return true;
}

bool isNvmlAvailable()
{
	std::lock_guard<std::mutex> lock(g_nvmlMutex);
	return g_nvml.initialized;
}

NvmlLib* getNvml()
{
	std::lock_guard<std::mutex> lock(g_nvmlMutex);
	if(!g_nvml.initialized) return nullptr;
	return &g_nvml;
}

void unloadNvml()
{
	std::lock_guard<std::mutex> lock(g_nvmlMutex);
	if(g_nvml.initialized && g_nvml.Shutdown)
	{
		g_nvml.Shutdown();
	}
	if(g_nvml.handle)
	{
		closeLib(g_nvml.handle);
	}
	g_nvml = NvmlLib{};
}

} // namespace nvml
} // namespace n0s
