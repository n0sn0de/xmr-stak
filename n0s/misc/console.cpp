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

#include "n0s/misc/console.hpp"
#include "n0s/misc/nvml_wrapper.hpp"
#include "n0s/platform/platform.hpp"

#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

int get_key()
{
	return n0s::platform::getKey();
}

void set_colour(out_colours cl)
{
	switch(cl)
	{
	case K_RED:
		fputs("\x1B[1;31m", stdout);
		break;
	case K_GREEN:
		fputs("\x1B[1;32m", stdout);
		break;
	case K_BLUE:
		fputs("\x1B[1;34m", stdout);
		break;
	case K_YELLOW:
		fputs("\x1B[1;33m", stdout);
		break;
	case K_CYAN:
		fputs("\x1B[1;36m", stdout);
		break;
	case K_MAGENTA:
		fputs("\x1B[1;35m", stdout);
		break;
	case K_WHITE:
		fputs("\x1B[1;37m", stdout);
		break;
	case K_BRIGHT_BLUE:
		fputs("\x1B[94m", stdout);
		break;
	case K_BRIGHT_CYAN:
		fputs("\x1B[96m", stdout);
		break;
	case K_BRIGHT_WHITE:
		fputs("\x1B[97m", stdout);
		break;
	case K_BRIGHT_GREEN:
		fputs("\x1B[92m", stdout);
		break;
	case K_BRIGHT_YELLOW:
		fputs("\x1B[93m", stdout);
		break;
	case K_BRIGHT_RED:
		fputs("\x1B[91m", stdout);
		break;
	case K_DIM:
		fputs("\x1B[2m", stdout);
		break;
	case K_BOLD:
		fputs("\x1B[1m", stdout);
		break;
	default:
		break;
	}
}

void reset_colour()
{
	fputs("\x1B[0m", stdout);
}

printer::printer()
{
	verbose_level = LINF;
	logfile = nullptr;
	setvbuf(stdout, nullptr, _IOFBF, BUFSIZ);

	// Enable ANSI colors on Windows
	n0s::platform::enableConsoleColors();
}

bool printer::open_logfile(const char* file)
{
	logfile = fopen(file, "ab+");
	return logfile != nullptr;
}

void printer::print_msg(verbosity verbose, const char* fmt, ...)
{
	if(verbose > verbose_level)
		return;

	char buf[1024];
	size_t bpos;

	time_t now = time(nullptr);
	n0s::platform::formatLocalTime(buf, sizeof(buf), "[%F %T] : ", static_cast<int64_t>(now));
	bpos = strlen(buf);

	va_list args;
	va_start(args, fmt);
	vsnprintf(buf + bpos, sizeof(buf) - bpos, fmt, args);
	va_end(args);
	bpos = strlen(buf);

	if(bpos + 2 >= sizeof(buf))
		return;

	buf[bpos] = '\n';
	buf[bpos + 1] = '\0';

	print_str(buf);
}

void printer::print_str(const char* str)
{
	std::unique_lock<std::mutex> lck(print_mutex);
	fputs(str, stdout);
	fflush(stdout);

	if(logfile != nullptr)
	{
		fputs(str, logfile);
		fflush(logfile);
	}
}

void printer::print_str_color(out_colours cl, const char* str)
{
	std::unique_lock<std::mutex> lck(print_mutex);
	set_colour(cl);
	fputs(str, stdout);
	reset_colour();
	fflush(stdout);

	// Write uncolored to logfile
	if(logfile != nullptr)
	{
		fputs(str, logfile);
		fflush(logfile);
	}
}

void n0s_exit(int code)
{
	// Clean up NVML if loaded (courtesy shutdown)
	n0s::nvml::unloadNvml();
	n0s::platform::sockCleanup();
	std::exit(code);
}
