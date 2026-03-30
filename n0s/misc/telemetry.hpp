#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace n0s
{

class telemetry
{
  public:
	telemetry(size_t iThd);
	void push_perf_value(size_t iThd, uint64_t iHashCount, uint64_t iTimestamp);
	double calc_telemetry_data(size_t iLastMillisec, size_t iThread);

  private:
	constexpr static size_t iBucketSize = 2 << 11; //Power of 2 to simplify calculations
	constexpr static size_t iBucketMask = iBucketSize - 1;
	std::vector<uint32_t> iBucketTop;
	std::vector<std::vector<uint64_t>> ppHashCounts;
	std::vector<std::vector<uint64_t>> ppTimestamps;
};

} // namespace n0s
