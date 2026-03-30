#pragma once

#include "n0s/params.hpp"

#include <cstdlib>
#include <string>

namespace n0s
{
namespace cpu
{

class jconf
{
  public:
	static jconf* inst()
	{
		if(oInst == nullptr)
			oInst = new jconf;
		return oInst;
	};

	bool parse_config(const char* sFilename = "cpu.txt");

	struct thd_cfg
	{
		int iMultiway;
		bool bNoPrefetch;
		long long iCpuAff;
	};

	size_t GetThreadCount();
	bool GetThreadConfig(size_t id, thd_cfg& cfg);
	bool NeedsAutoconf();

  private:
	jconf();
	static jconf* oInst;

	struct opaque_private;
	opaque_private* prv;
};

} // namespace cpu
} // namespace n0s
