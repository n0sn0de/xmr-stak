#pragma once

#include <cstdint>
#include <string>

namespace n0s
{

/// Print the colorized n0s-ryo-miner startup banner
/// Uses ANSI 256-color codes for the RYO blue→cyan gradient
void print_banner();

/// Print a colorized separator line
void print_separator();

/// Print a colorized share accepted message
void print_share_accepted(const char* backend, uint32_t gpu_index, const char* pool_addr);

/// Print a colorized share rejected message
void print_share_rejected(const char* backend, uint32_t gpu_index, const char* pool_addr);

/// Format GPU telemetry line with colors
/// Returns a string with ANSI color codes
std::string format_gpu_telemetry(
	const char* backend, uint32_t gpu_index,
	double hashrate, int temp_c, int power_w,
	int fan_pct, int gpu_clock_mhz, int mem_clock_mhz);

/// Format hashrate with color based on value
std::string format_hashrate_colored(double hps);

} // namespace n0s
