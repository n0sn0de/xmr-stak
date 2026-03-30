#pragma once

#include "n0s/misc/environment.hpp"
#include <cstdio>

#include <mutex>

enum out_colours
{
	K_RED,
	K_GREEN,
	K_BLUE,
	K_YELLOW,
	K_CYAN,
	K_MAGENTA,
	K_WHITE,
	K_NONE
};

// Warning - on Linux get_key will detect control keys.
// We will only use it for alphanum keys anyway.
int get_key();

void set_colour(out_colours cl);
void reset_colour();

enum verbosity : size_t
{
	L0 = 0,
	L1 = 1,
	L2 = 2,
	L3 = 3,
	L4 = 4,
	LDEBUG = 10,
	LINF = 100
};

class printer
{
  public:
	static inline printer* inst()
	{
		auto& env = n0s::environment::inst();
		if(env.pPrinter == nullptr)
		{
			std::unique_lock<std::mutex> lck(env.update);
			if(env.pPrinter == nullptr)
				env.pPrinter = new printer;
		}
		return env.pPrinter;
	};

	inline void set_verbose_level(size_t level) { verbose_level = (verbosity)level; }
	void print_msg(verbosity verbose, const char* fmt, ...);
	void print_str(const char* str);
	bool open_logfile(const char* file);

  private:
	printer();

	std::mutex print_mutex;
	verbosity verbose_level;
	FILE* logfile;
};

/// Clean exit — replaces the old win_exit() Windows-ism
void n0s_exit(int code = 1);
