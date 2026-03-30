#pragma once

#include "jconf.hpp"

#include "n0s/backend/cryptonight.hpp"

namespace {
// Extract bits [h:l] from val (inclusive)
constexpr int32_t get_masked(int32_t val, int32_t h, int32_t l)
{
	return (val >> l) & ((1 << (h - l + 1)) - 1);
}
}
#include "n0s/jconf.hpp"
#include "n0s/misc/configEditor.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/params.hpp"
#include <string>

#include <unistd.h>

namespace n0s
{
namespace cpu
{

class autoAdjust
{
  public:
	bool printConfig()
	{
		const size_t hashMemSize = ::jconf::inst()->GetMiningMemSize();
		const size_t hashMemSizeKB = hashMemSize / 1024u;

		const size_t halfHashMemSizeKB = hashMemSizeKB / 2u;

		configEditor configTpl{};

		// load the template of the backend config into a char variable
		const char* tpl =
#include "./config.tpl"
			;
		configTpl.set(std::string(tpl));

		std::string conf;

		// CPU mining disabled for cryptonight_gpu (not suitable for CPU)
		constexpr bool useCryptonight_gpu = true;

		if(useCryptonight_gpu)
		{
			printer::inst()->print_msg(L0, "WARNING: CPU mining will be disabled because cryptonight_gpu is not suitable for CPU mining. You can uncomment the auto generated config in %s to enable CPU mining.", std::string("cpu.txt").c_str());
			conf += "/*\n//CPU config is disabled by default because cryptonight_gpu is not suitable for CPU mining.\n";
		}
		if(!detectL3Size() || L3KB_size < halfHashMemSizeKB || L3KB_size > (halfHashMemSizeKB * 2048u))
		{
			if(L3KB_size < halfHashMemSizeKB || L3KB_size > (halfHashMemSizeKB * 2048))
				printer::inst()->print_msg(L0, "Autoconf failed: L3 size sanity check failed - %u KB.", L3KB_size);

			conf += std::string("    { \"low_power_mode\" : false, \"no_prefetch\" : true,  \"asm\" : \"off\", \"affine_to_cpu\" : false },\n");
			printer::inst()->print_msg(L0, "Autoconf FAILED. Create config for a single thread. Please try to add new ones until the hashrate slows down.");
		}
		else
		{
			printer::inst()->print_msg(L0, "Autoconf L3 size detected at %u KB.", L3KB_size);

			detectCPUConf();

			printer::inst()->print_msg(L0, "Autoconf core count detected as %u on %s.", corecnt,
				linux_layout ? "Linux" : "Windows");

			uint32_t aff_id = 0;
			for(uint32_t i = 0; i < corecnt; i++)
			{
				bool double_mode;

				if(L3KB_size <= 0)
					break;

				double_mode = L3KB_size / hashMemSizeKB > static_cast<size_t>(corecnt - i);

				conf += std::string("    { \"low_power_mode\" : ");
				conf += std::string(double_mode ? "true" : "false");
				conf += std::string(", \"no_prefetch\" : true, \"asm\" : \"auto\", \"affine_to_cpu\" : ");
				conf += std::to_string(aff_id);
				conf += std::string(" },\n");

				if(!linux_layout || old_amd)
				{
					aff_id += 2;

					if(aff_id >= corecnt)
						aff_id = 1;
				}
				else
					aff_id++;

				if(double_mode)
					L3KB_size -= hashMemSizeKB * 2u;
				else
					L3KB_size -= hashMemSizeKB;
			}
		}

		if(useCryptonight_gpu)
			conf += "*/\n";

		configTpl.replace("CPUCONFIG", conf);
		configTpl.write(std::string("cpu.txt"));
		printer::inst()->print_msg(L0, "CPU configuration stored in file '%s'", std::string("cpu.txt").c_str());

		return true;
	}

  private:
	bool detectL3Size()
	{
		int32_t cpu_info[4];
		char cpustr[13] = {0};

		::jconf::cpuid(0, 0, cpu_info);
		memcpy(cpustr, &cpu_info[1], 4);
		memcpy(cpustr + 4, &cpu_info[3], 4);
		memcpy(cpustr + 8, &cpu_info[2], 4);

		if(strcmp(cpustr, "GenuineIntel") == 0)
		{
			::jconf::cpuid(4, 3, cpu_info);

			if(get_masked(cpu_info[0], 7, 5) != 3)
			{
				printer::inst()->print_msg(L0, "Autoconf failed: Couldn't find L3 cache page.");
				return false;
			}

			L3KB_size = ((get_masked(cpu_info[1], 31, 22) + 1) * (get_masked(cpu_info[1], 21, 12) + 1) *
							(get_masked(cpu_info[1], 11, 0) + 1) * (cpu_info[2] + 1)) /
						1024;

			return true;
		}
		else if(strcmp(cpustr, "AuthenticAMD") == 0)
		{
			::jconf::cpuid(0x80000006, 0, cpu_info);

			L3KB_size = get_masked(cpu_info[3], 31, 18) * 512;

			::jconf::cpuid(1, 0, cpu_info);

			// Detect pre-Zen AMD (family < 0x17) via CPUID
			uint32_t family = ((cpu_info[0] >> 8) & 0xF);
			uint32_t ext_family = ((cpu_info[0] >> 20) & 0xFF);
			if(family == 0xF) family += ext_family;
			if(family < 0x17)
				old_amd = true;

			return true;
		}
		else
		{
			printer::inst()->print_msg(L0, "Autoconf failed: Unknown CPU type: %s.", cpustr);
			return false;
		}
	}

	void detectCPUConf()
	{
		corecnt = sysconf(_SC_NPROCESSORS_ONLN);
		linux_layout = true;
	}

	size_t L3KB_size = 0;
	uint32_t corecnt;
	bool old_amd = false;
	bool linux_layout;
};

} // namespace cpu
} // namespace n0s
