#include "banner.hpp"
#include "console.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace n0s
{

// ANSI escape helpers
#define RST  "\x1B[0m"
#define BOLD "\x1B[1m"
#define DIM  "\x1B[2m"

// RYO color palette (blue → cyan gradient)
#define RYO_DARK   "\x1B[38;5;25m"   // Deep blue
#define RYO_MED    "\x1B[38;5;33m"   // Medium blue
#define RYO_LIGHT  "\x1B[38;5;39m"   // Bright blue
#define RYO_CYAN   "\x1B[38;5;44m"   // Blue-cyan
#define RYO_BCYAN  "\x1B[38;5;51m"   // Bright cyan
#define RYO_WHITE  "\x1B[97m"        // Bright white

// Status colors
#define CLR_GREEN  "\x1B[92m"
#define CLR_RED    "\x1B[91m"
#define CLR_YELLOW "\x1B[93m"
#define CLR_CYAN   "\x1B[96m"
#define CLR_BLUE   "\x1B[94m"
#define CLR_DIM    "\x1B[2;37m"
#define CLR_WHITE  "\x1B[97m"

void print_banner()
{
	printer* p = printer::inst();

	p->print_str("\n");

	// Gradient ASCII art — n0s-ryo-miner branded
	// Uses the RYO blue→cyan color theme
	// Inner width = 63 visible chars between ║ delimiters
	// N0S-RYO ASCII art with blue→cyan gradient
	// All inner lines are exactly 63 visible chars between ║ delimiters
	p->print_str(
		RYO_DARK  "   ╔═══════════════════════════════════════════════════════════════╗\n" RST
		RYO_DARK  "   ║" "                                                               " RYO_DARK "║\n" RST
		RYO_DARK  "   ║ " RST
			RYO_MED  "███╗   ██╗ ██████╗ ███████╗" RYO_DARK "━━━━━━" RYO_CYAN "██████╗ ██╗   ██╗ ██████╗    " RYO_DARK  "║\n" RST
		RYO_DARK  "   ║ " RST
			RYO_MED  "████╗  ██║██╔═══██╗██╔════╝" RYO_DARK "      " RYO_CYAN "██╔══██╗╚██╗ ██╔╝██╔═══██╗   " RYO_DARK  "║\n" RST
		RYO_DARK  "   ║ " RST
			RYO_LIGHT "██╔██╗ ██║██║   ██║███████╗" RYO_BCYAN "█████╗██████╔╝ ╚████╔╝ ██║   ██║   " RYO_DARK  "║\n" RST
		RYO_DARK  "   ║ " RST
			RYO_LIGHT "██║╚██╗██║██║   ██║╚════██║" RYO_BCYAN "╚════╝██╔══██╗  ╚██╔╝  ██║   ██║   " RYO_DARK  "║\n" RST
		RYO_DARK  "   ║ " RST
			RYO_CYAN  "██║ ╚████║╚██████╔╝███████║" RYO_DARK "      " RYO_BCYAN "██║  ██║   ██║   ╚██████╔╝   " RYO_DARK  "║\n" RST
		RYO_DARK  "   ║ " RST
			RYO_CYAN  "╚═╝  ╚═══╝ ╚═════╝ ╚══════╝" RYO_DARK "      " RYO_BCYAN "╚═╝  ╚═╝   ╚═╝    ╚═════╝    " RYO_DARK  "║\n" RST
		RYO_DARK  "   ║" "                                                               " RYO_DARK "║\n" RST
		RYO_DARK  "   ║" CLR_DIM "     GPU Miner for RYO Currency • CryptoNight-GPU • v3.3.0     " RYO_DARK  "║\n" RST
		RYO_DARK  "   ╚═══════════════════════════════════════════════════════════════╝\n" RST
	);

	p->print_str("\n");
}

void print_separator()
{
	printer::inst()->print_str(
		RYO_DARK "   ═════════════════════════════════════════════════════════════════\n" RST
	);
}

void print_share_accepted(const char* backend, uint32_t gpu_index, const char* pool_addr)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		CLR_GREEN "  ✓ " RST "%s GPU %u" CLR_DIM " share accepted" RST " • " CLR_DIM "%s" RST "\n",
		backend, gpu_index, pool_addr);
	printer::inst()->print_str(buf);
}

void print_share_rejected(const char* backend, uint32_t gpu_index, const char* pool_addr)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
		CLR_RED "  ✗ " RST "%s GPU %u" CLR_RED " share REJECTED" RST " • " CLR_DIM "%s" RST "\n",
		backend, gpu_index, pool_addr);
	printer::inst()->print_str(buf);
}

std::string format_gpu_telemetry(
	const char* backend, uint32_t gpu_index,
	double hashrate, int temp_c, int power_w,
	int fan_pct, int gpu_clock_mhz, int mem_clock_mhz)
{
	char buf[512];
	std::string result;

	// GPU name + hashrate
	const char* hr_color = (hashrate > 0) ? CLR_GREEN : CLR_RED;
	snprintf(buf, sizeof(buf), "    %s GPU%u : " "%s%.1f H/s" RST,
		backend, gpu_index, hr_color, hashrate);
	result += buf;

	// Temperature with color coding
	if(temp_c > 0)
	{
		const char* temp_color;
		if(temp_c >= 85) temp_color = CLR_RED;
		else if(temp_c >= 70) temp_color = CLR_YELLOW;
		else temp_color = CLR_CYAN;

		snprintf(buf, sizeof(buf), "  %s%d°C" RST, temp_color, temp_c);
		result += buf;
	}

	// Power
	if(power_w > 0)
	{
		snprintf(buf, sizeof(buf), "  " CLR_YELLOW "%dW" RST, power_w);
		result += buf;

		// H/W efficiency
		if(hashrate > 0)
		{
			double hpw = hashrate / power_w;
			snprintf(buf, sizeof(buf), "  " CLR_DIM "%.1f H/W" RST, hpw);
			result += buf;
		}
	}

	// Fan
	if(fan_pct >= 0)
	{
		const char* fan_color = (fan_pct > 80) ? CLR_RED : CLR_DIM;
		snprintf(buf, sizeof(buf), "  " "%sFAN:%d%%" RST, fan_color, fan_pct);
		result += buf;
	}

	// Clocks
	if(gpu_clock_mhz > 0)
	{
		snprintf(buf, sizeof(buf), "  " CLR_DIM "CC:%dMHz" RST, gpu_clock_mhz);
		result += buf;
	}
	if(mem_clock_mhz > 0)
	{
		snprintf(buf, sizeof(buf), "  " CLR_DIM "MC:%dMHz" RST, mem_clock_mhz);
		result += buf;
	}

	result += "\n";
	return result;
}

std::string format_hashrate_colored(double hps)
{
	char buf[64];
	if(hps >= 1000.0)
		snprintf(buf, sizeof(buf), CLR_GREEN BOLD "%.1f" RST, hps);
	else if(hps > 0.0)
		snprintf(buf, sizeof(buf), CLR_YELLOW "%.1f" RST, hps);
	else
		snprintf(buf, sizeof(buf), CLR_DIM "0.0" RST);
	return std::string(buf);
}

} // namespace n0s
