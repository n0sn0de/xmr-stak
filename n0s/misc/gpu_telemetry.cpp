/**
 * gpu_telemetry.cpp — GPU temperature, power, fan, and clock queries
 *
 * NVIDIA: Uses NVML direct API (runtime loaded) for zero-overhead telemetry.
 *         Falls back to nvidia-smi subprocess if NVML is unavailable.
 * AMD:    Uses sysfs (/sys/class/drm/) on Linux, plus amd-smi for GPU names.
 *
 * Both paths are cross-platform ready:
 *   - NVML works on Linux and Windows (nvml.dll / libnvidia-ml.so.1)
 *   - AMD sysfs is Linux-only; Windows will use ADL SDK (future)
 */

#include "gpu_telemetry.hpp"
#include "nvml_wrapper.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <dirent.h>
#endif

namespace n0s
{

namespace
{

/// Cached GPU name lookup (populated on first query per device)
std::map<uint32_t, std::string> amdNameCache;
std::map<uint32_t, std::string> nvidiaNameCache;
std::mutex nameCacheMutex;

// ─── AMD Helpers (Linux sysfs) ───────────────────────────────────────────────

#ifndef _WIN32

/// Query AMD GPU name via amd-smi (cached)
std::string getAmdGpuName(uint32_t device_index)
{
	std::lock_guard<std::mutex> lck(nameCacheMutex);
	auto it = amdNameCache.find(device_index);
	if(it != amdNameCache.end()) return it->second;

	// Try amd-smi
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"amd-smi static --gpu %u 2>/dev/null | grep MARKET_NAME | head -1",
		device_index);
	FILE* pipe = popen(cmd, "r");
	if(pipe)
	{
		char buf[256];
		if(fgets(buf, sizeof(buf), pipe))
		{
			std::string line(buf);
			auto pos = line.find(':');
			if(pos != std::string::npos)
			{
				std::string name = line.substr(pos + 1);
				// Trim
				while(!name.empty() && (name.front() == ' ')) name.erase(0, 1);
				while(!name.empty() && (name.back() == '\n' || name.back() == ' ')) name.pop_back();
				if(!name.empty() && name != "N/A")
				{
					amdNameCache[device_index] = name;
					pclose(pipe);
					return name;
				}
			}
		}
		pclose(pipe);
	}

	// Fallback: try PATH lookup for amd-smi in /opt/rocm*/bin
	snprintf(cmd, sizeof(cmd),
		"ls /opt/rocm*/bin/amd-smi 2>/dev/null | head -1");
	pipe = popen(cmd, "r");
	if(pipe)
	{
		char path[256] = {};
		if(fgets(path, sizeof(path), pipe))
		{
			// Trim newline
			for(char* p = path; *p; p++) if(*p == '\n') *p = '\0';

			char cmd2[512];
			snprintf(cmd2, sizeof(cmd2),
				"%s static --gpu %u 2>/dev/null | grep MARKET_NAME | head -1",
				path, device_index);
			pclose(pipe);

			pipe = popen(cmd2, "r");
			if(pipe)
			{
				char buf2[256];
				if(fgets(buf2, sizeof(buf2), pipe))
				{
					std::string line(buf2);
					auto pos = line.find(':');
					if(pos != std::string::npos)
					{
						std::string name = line.substr(pos + 1);
						while(!name.empty() && name.front() == ' ') name.erase(0, 1);
						while(!name.empty() && (name.back() == '\n' || name.back() == ' ')) name.pop_back();
						if(!name.empty() && name != "N/A")
						{
							amdNameCache[device_index] = name;
							pclose(pipe);
							return name;
						}
					}
				}
				pclose(pipe);
			}
		}
		else
		{
			pclose(pipe);
		}
	}

	amdNameCache[device_index] = "AMD GPU " + std::to_string(device_index);
	return amdNameCache[device_index];
}

/// Read an integer from a sysfs file
int readSysfsInt(const std::string& path)
{
	std::ifstream f(path);
	if(!f.is_open()) return -1;
	int val = -1;
	f >> val;
	return val;
}

/// Find the hwmon path for a DRM card
std::string findHwmonPath(uint32_t card_index)
{
	// AMD GPUs expose hwmon under /sys/class/drm/cardN/device/hwmon/hwmonM/
	std::string base = "/sys/class/drm/card" + std::to_string(card_index) + "/device/hwmon/";
	DIR* dir = opendir(base.c_str());
	if(!dir) return "";

	struct dirent* entry;
	std::string result;
	while((entry = readdir(dir)) != nullptr)
	{
		if(strncmp(entry->d_name, "hwmon", 5) == 0)
		{
			result = base + entry->d_name + "/";
			break;
		}
	}
	closedir(dir);
	return result;
}

/// Parse active clock from pp_dpm_sclk/pp_dpm_mclk
/// Format: "0: 500Mhz\n1: 1000Mhz *\n" — line with * is active
int parseDpmClock(const std::string& path)
{
	std::ifstream f(path);
	if(!f.is_open()) return -1;
	std::string line;
	while(std::getline(f, line))
	{
		if(line.find('*') != std::string::npos)
		{
			// Extract MHz value: "N: XYZMhz *"
			auto colon = line.find(':');
			if(colon == std::string::npos) continue;
			int mhz = -1;
			sscanf(line.c_str() + colon + 1, " %dMhz", &mhz);
			return mhz;
		}
	}
	return -1;
}

#endif // !_WIN32

// ─── NVIDIA Helpers ──────────────────────────────────────────────────────────

/// Get cached NVIDIA GPU name via NVML
std::string getNvidiaGpuNameNvml(nvml::NvmlLib* lib, nvml::nvmlDevice_t device, uint32_t device_index)
{
	std::lock_guard<std::mutex> lck(nameCacheMutex);
	auto it = nvidiaNameCache.find(device_index);
	if(it != nvidiaNameCache.end()) return it->second;

	if(lib->DeviceGetName)
	{
		char nameBuf[nvml::NVML_DEVICE_NAME_BUFFER_SIZE] = {};
		if(lib->DeviceGetName(device, nameBuf, sizeof(nameBuf)) == nvml::NVML_SUCCESS)
		{
			std::string name(nameBuf);
			// Trim trailing whitespace
			while(!name.empty() && (name.back() == ' ' || name.back() == '\n'))
				name.pop_back();
			if(!name.empty())
			{
				nvidiaNameCache[device_index] = name;
				return name;
			}
		}
	}

	nvidiaNameCache[device_index] = "NVIDIA GPU " + std::to_string(device_index);
	return nvidiaNameCache[device_index];
}

/// Query NVIDIA telemetry via NVML direct API
bool queryNvidiaTelemetryNvml(uint32_t device_index, GpuTelemetry& telem)
{
	nvml::NvmlLib* lib = nvml::getNvml();
	if(!lib) return false;

	nvml::nvmlDevice_t device = nullptr;
	if(lib->DeviceGetHandleByIndex(device_index, &device) != nvml::NVML_SUCCESS)
		return false;

	bool gotData = false;

	// GPU name
	telem.name = getNvidiaGpuNameNvml(lib, device, device_index);

	// Temperature (°C)
	if(lib->DeviceGetTemperature)
	{
		unsigned int temp = 0;
		if(lib->DeviceGetTemperature(device, nvml::NVML_TEMPERATURE_GPU, &temp) == nvml::NVML_SUCCESS)
		{
			telem.temp_c = static_cast<int>(temp);
			gotData = true;
		}
	}

	// Power (milliwatts → watts)
	if(lib->DeviceGetPowerUsage)
	{
		unsigned int power_mw = 0;
		if(lib->DeviceGetPowerUsage(device, &power_mw) == nvml::NVML_SUCCESS)
		{
			telem.power_w = static_cast<int>((power_mw + 500) / 1000);
			gotData = true;
		}
	}

	// Fan speed (%)
	if(lib->DeviceGetFanSpeed)
	{
		unsigned int fan = 0;
		if(lib->DeviceGetFanSpeed(device, &fan) == nvml::NVML_SUCCESS)
		{
			telem.fan_pct = static_cast<int>(fan);
			gotData = true;
		}
	}

	// GPU clock (MHz)
	if(lib->DeviceGetClockInfo)
	{
		unsigned int clk = 0;
		if(lib->DeviceGetClockInfo(device, nvml::NVML_CLOCK_GRAPHICS, &clk) == nvml::NVML_SUCCESS)
		{
			telem.gpu_clock_mhz = static_cast<int>(clk);
			gotData = true;
		}
	}

	// Memory clock (MHz)
	if(lib->DeviceGetClockInfo)
	{
		unsigned int clk = 0;
		if(lib->DeviceGetClockInfo(device, nvml::NVML_CLOCK_MEM, &clk) == nvml::NVML_SUCCESS)
		{
			telem.mem_clock_mhz = static_cast<int>(clk);
			gotData = true;
		}
	}

	return gotData;
}

/// Fallback: query NVIDIA telemetry via nvidia-smi subprocess
bool queryNvidiaTelemetrySmi(uint32_t device_index, GpuTelemetry& telem)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"nvidia-smi -i %u --query-gpu=name,temperature.gpu,power.draw,fan.speed,"
		"clocks.current.graphics,clocks.current.memory "
		"--format=csv,noheader,nounits 2>/dev/null",
		device_index);

	FILE* pipe = popen(cmd, "r");
	if(!pipe) return false;

	std::array<char, 256> buf;
	std::string output;
	while(fgets(buf.data(), buf.size(), pipe))
		output += buf.data();
	pclose(pipe);

	// Parse CSV: "name, temp, power, fan%, gpu_clock, mem_clock"
	auto firstComma = output.find(',');
	if(firstComma == std::string::npos) return false;

	telem.name = output.substr(0, firstComma);
	while(!telem.name.empty() && (telem.name.back() == ' ' || telem.name.back() == '\n'))
		telem.name.pop_back();

	const char* numStart = output.c_str() + firstComma + 1;
	float power_f = 0;
	int temp = -1, fan = -1, gpu_clk = -1, mem_clk = -1;

	if(sscanf(numStart, " %d, %f, %d, %d, %d",
		&temp, &power_f, &fan, &gpu_clk, &mem_clk) >= 2)
	{
		telem.temp_c = temp;
		telem.power_w = static_cast<int>(power_f + 0.5f);
		telem.fan_pct = fan;
		telem.gpu_clock_mhz = gpu_clk;
		telem.mem_clock_mhz = mem_clk;
		return true;
	}

	return false;
}

} // anonymous namespace

// ─── Public API ──────────────────────────────────────────────────────────────

bool queryAmdTelemetry(uint32_t device_index, GpuTelemetry& telem)
{
#ifdef _WIN32
	// Windows AMD telemetry via ADL SDK — future implementation
	(void)device_index;
	(void)telem;
	return false;
#else
	// Linux: sysfs + amd-smi
	std::string hwmon;
	std::string drmBase;

	for(uint32_t card = device_index; card <= device_index + 4; ++card)
	{
		hwmon = findHwmonPath(card);
		if(!hwmon.empty())
		{
			drmBase = "/sys/class/drm/card" + std::to_string(card) + "/device/";
			break;
		}
	}

	if(hwmon.empty()) return false;

	// Temperature: temp1_input is in millidegrees
	int temp_mc = readSysfsInt(hwmon + "temp1_input");
	if(temp_mc > 0) telem.temp_c = temp_mc / 1000;

	// Power: power1_average is in microwatts
	int power_uw = readSysfsInt(hwmon + "power1_average");
	if(power_uw > 0) telem.power_w = power_uw / 1000000;

	// Fan: fan1_input is RPM
	telem.fan_rpm = readSysfsInt(hwmon + "fan1_input");

	// Fan %: pwm1 (0-255) or fan1_target
	int pwm = readSysfsInt(hwmon + "pwm1");
	if(pwm >= 0) telem.fan_pct = (pwm * 100) / 255;

	// GPU clock
	telem.gpu_clock_mhz = parseDpmClock(drmBase + "pp_dpm_sclk");

	// Memory clock
	telem.mem_clock_mhz = parseDpmClock(drmBase + "pp_dpm_mclk");

	// GPU name (cached)
	telem.name = getAmdGpuName(device_index);

	return (telem.temp_c > 0 || telem.power_w > 0);
#endif
}

bool queryNvidiaTelemetry(uint32_t device_index, GpuTelemetry& telem)
{
	// Try NVML first (lazy load on first call)
	nvml::loadNvml();

	if(nvml::isNvmlAvailable())
		return queryNvidiaTelemetryNvml(device_index, telem);

	// Fallback to nvidia-smi subprocess
	return queryNvidiaTelemetrySmi(device_index, telem);
}

} // namespace n0s
