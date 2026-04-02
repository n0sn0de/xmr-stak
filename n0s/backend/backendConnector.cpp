/*
  * This program is free software: you can redistribute it and/or modify
  * it under the terms of the GNU General Public License as published by
  * the Free Software Foundation, either version 3 of the License, or
  * any later version.
  *
  * This program is distributed in the hope that it will be useful,
  * but WITHOUT ANY WARRANTY; without even the implied warranty of
  * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  * GNU General Public License for more details.
  *
  * You should have received a copy of the GNU General Public License
  * along with this program.  If not, see <http://www.gnu.org/licenses/>.
  *
  * Additional permission under GNU GPL version 3 section 7
  *
  * If you modify this Program, or any covered work, by linking or combining
  * it with OpenSSL (or a modified version of that library), containing parts
  * covered by the terms of OpenSSL License and SSLeay License, the licensors
  * of this Program grant you additional permission to convey the resulting work.
  *
  */

#include "backendConnector.hpp"
#include "globalStates.hpp"
#include "iBackend.hpp"
#include "miner_work.hpp"
#include "n0s/misc/console.hpp"
#include "n0s/misc/environment.hpp"
#include "n0s/params.hpp"

#include "cpu/minethd.hpp"
#ifndef CONF_NO_CUDA
#include "nvidia/minethd.hpp"
#endif
#ifndef CONF_NO_OPENCL
#include "amd/minethd.hpp"
#endif

#include <chrono>
#include <cstring>
#include <thread>

namespace n0s
{

bool BackendConnector::self_test()
{
	return cpu::minethd::self_test();
}

std::vector<std::unique_ptr<iBackend>> BackendConnector::thread_starter(miner_work& pWork)
{
	std::vector<std::unique_ptr<iBackend>> pvThreads;

#ifndef CONF_NO_OPENCL
	if(params::inst().useAMD)
	{
		const std::string backendName = n0s::params::inst().openCLVendor;
		auto amdThreads = opencl::minethd::thread_starter(static_cast<uint32_t>(pvThreads.size()), pWork);
		if(!amdThreads.empty())
		{
			for(auto& t : amdThreads)
				pvThreads.push_back(std::move(t));
		}
		else
		{
			printer::inst()->print_msg(L0, "WARNING: backend %s (OpenCL) disabled.", backendName.c_str());
		}
	}
#endif

#ifndef CONF_NO_CUDA
	if(params::inst().useNVIDIA)
	{
		printer::inst()->print_msg(L0, "NVIDIA: initializing CUDA backend...");
		auto nvidiaThreads = cuda::minethd::thread_starter(static_cast<uint32_t>(pvThreads.size()), pWork);
		if(!nvidiaThreads.empty())
		{
			printer::inst()->print_msg(L0, "NVIDIA: CUDA backend started with %zu GPU(s).", nvidiaThreads.size());
			for(auto& t : nvidiaThreads)
				pvThreads.push_back(std::move(t));
		}
		else
		{
			printer::inst()->print_msg(L0, "WARNING: backend NVIDIA disabled.");
		}
	}
#endif

	globalStates::inst().iThreadCount = pvThreads.size();
	return pvThreads;
}

} // namespace n0s
