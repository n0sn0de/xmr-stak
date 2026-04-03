#pragma once

/// GPU telemetry data — temperature, power, fan, clocks
/// NVIDIA: NVML direct API (runtime loaded), nvidia-smi fallback
/// AMD: sysfs (Linux), ADL SDK (Windows — future)

#include <cstdint>
#include <string>

namespace n0s
{

struct GpuTelemetry
{
	std::string name;       // GPU device name (e.g., "AMD Radeon RX 9070 XT")
	int temp_c = -1;        // Temperature in °C (-1 = unavailable)
	int power_w = -1;       // Power draw in watts (-1 = unavailable)
	int fan_rpm = -1;       // Fan speed in RPM (-1 = unavailable)
	int fan_pct = -1;       // Fan speed in percent (-1 = unavailable)
	int gpu_clock_mhz = -1; // GPU clock in MHz
	int mem_clock_mhz = -1; // Memory clock in MHz
};

/// Query AMD GPU telemetry via sysfs
/// @param device_index  GPU index (maps to /sys/class/drm/cardN)
/// @param telem         [out] Telemetry data
/// @return true if at least some data was collected
bool queryAmdTelemetry(uint32_t device_index, GpuTelemetry& telem);

/// Query NVIDIA GPU telemetry via NVML direct API (preferred) or nvidia-smi fallback.
/// NVML is runtime-loaded (dlopen) — no compile-time dependency.
/// @param device_index  GPU index
/// @param telem         [out] Telemetry data
/// @return true if at least some data was collected
bool queryNvidiaTelemetry(uint32_t device_index, GpuTelemetry& telem);

} // namespace n0s
