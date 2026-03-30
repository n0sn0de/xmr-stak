#pragma once

#include "iBackend.hpp"
#include "miner_work.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace n0s
{

struct BackendConnector
{
	static std::vector<std::unique_ptr<iBackend>> thread_starter(miner_work& pWork);
	static bool self_test();
};

} // namespace n0s
