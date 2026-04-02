#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace n0s
{

/// Ring buffer for hashrate time-series data.
/// Records per-GPU and total hashrate at 1-second resolution.
/// Thread-safe: lock-free single-producer (executor tick), mutex-protected reader (HTTP).
class HashrateHistory
{
  public:
	static constexpr size_t MAX_GPUS = 16;
	static constexpr size_t CAPACITY = 3600; // 1 hour at 1s resolution

	struct Sample
	{
		uint64_t timestamp_ms = 0;       // Unix timestamp in milliseconds
		double total_hs = 0.0;           // Total hashrate H/s
		double per_gpu_hs[MAX_GPUS] = {}; // Per-GPU hashrate
	};

	HashrateHistory() = default;

	/// Record a new sample (called from executor tick, ~every 1s)
	void push(uint64_t timestamp_ms, double total_hs, const double* per_gpu, size_t gpu_count)
	{
		std::lock_guard<std::mutex> lck(mtx_);

		auto& s = samples_[head_];
		s.timestamp_ms = timestamp_ms;
		s.total_hs = total_hs;

		size_t n = gpu_count < MAX_GPUS ? gpu_count : MAX_GPUS;
		for(size_t i = 0; i < n; i++)
			s.per_gpu_hs[i] = per_gpu[i];
		for(size_t i = n; i < MAX_GPUS; i++)
			s.per_gpu_hs[i] = 0.0;

		head_ = (head_ + 1) % CAPACITY;
		if(count_ < CAPACITY)
			count_++;
	}

	/// Get all samples in chronological order (oldest first).
	/// Returns actual number of GPUs seen in the most recent sample.
	std::vector<Sample> get_all(size_t& out_gpu_count) const
	{
		std::lock_guard<std::mutex> lck(mtx_);

		std::vector<Sample> result;
		result.reserve(count_);

		if(count_ == 0)
		{
			out_gpu_count = 0;
			return result;
		}

		// Determine active GPU count from most recent sample
		size_t start = (head_ + CAPACITY - count_) % CAPACITY;
		out_gpu_count = 0;
		if(count_ > 0)
		{
			const auto& latest = samples_[(head_ + CAPACITY - 1) % CAPACITY];
			for(size_t i = 0; i < MAX_GPUS; i++)
			{
				if(latest.per_gpu_hs[i] > 0.0 || (i == 0 && latest.total_hs > 0.0))
					out_gpu_count = i + 1;
			}
		}

		for(size_t i = 0; i < count_; i++)
		{
			result.push_back(samples_[(start + i) % CAPACITY]);
		}

		return result;
	}

	size_t size() const
	{
		std::lock_guard<std::mutex> lck(mtx_);
		return count_;
	}

  private:
	mutable std::mutex mtx_;
	std::array<Sample, CAPACITY> samples_{};
	size_t head_ = 0;  // Next write position
	size_t count_ = 0; // Number of valid samples
};

} // namespace n0s
