#include "gpu_telemetry.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <string>

namespace n0s
{

namespace
{

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
	while((entry = readdir(dir)) != nullptr)
	{
		if(strncmp(entry->d_name, "hwmon", 5) == 0)
		{
			closedir(dir);
			return base + entry->d_name + "/";
		}
	}
	closedir(dir);
	return "";
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

} // anonymous namespace

bool queryAmdTelemetry(uint32_t device_index, GpuTelemetry& telem)
{
	// Try card indices 0-7 (skip card0 which is often integrated GPU)
	// AMD discrete GPUs typically start at card1
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

	return (telem.temp_c > 0 || telem.power_w > 0);
}

bool queryNvidiaTelemetry(uint32_t device_index, GpuTelemetry& telem)
{
	char cmd[256];
	snprintf(cmd, sizeof(cmd),
		"nvidia-smi -i %u --query-gpu=temperature.gpu,power.draw,fan.speed,"
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

	// Parse CSV: "temp, power, fan%, gpu_clock, mem_clock"
	// e.g. "65, 120.50, 60, 1800, 7000"
	float power_f = 0;
	int temp = -1, fan = -1, gpu_clk = -1, mem_clk = -1;

	if(sscanf(output.c_str(), "%d, %f, %d, %d, %d",
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

} // namespace n0s
