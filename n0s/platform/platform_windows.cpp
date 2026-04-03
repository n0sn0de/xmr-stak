/**
 * platform_windows.cpp — Windows implementations of platform abstraction
 *
 * Stub file for Pillar 3 (Windows Support).
 * Compiles only on Windows (_WIN32 defined).
 * Each function will be implemented during the Windows support phase.
 */

#ifdef _WIN32

#include "platform.hpp"

// winsock2.h MUST come before windows.h to avoid redefinition errors
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <cstdio>
#include <ctime>

// MinGW may not have _kbhit/_getch in <conio.h> — check
#if defined(_MSC_VER)
#include <conio.h>
#else
// MinGW: use Windows console input APIs directly
#endif

#if defined(_MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "shell32.lib")
#endif

namespace n0s
{
namespace platform
{

// ─── Filesystem Paths ────────────────────────────────────────────────────────

std::string getHomePath()
{
	// %USERPROFILE% (e.g. C:\Users\miner)
	char buf[MAX_PATH];
	DWORD len = GetEnvironmentVariableA("USERPROFILE", buf, sizeof(buf));
	if(len > 0 && len < sizeof(buf))
		return std::string(buf, len);
	return "C:\\";
}

static std::string ensureDir(const std::string& path)
{
	CreateDirectoryA(path.c_str(), nullptr); // ignore if exists
	return path;
}

std::string getConfigDir()
{
	// %APPDATA%\n0s (e.g. C:\Users\miner\AppData\Roaming\n0s)
	char buf[MAX_PATH];
	if(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, buf) == S_OK)
		return ensureDir(std::string(buf) + "\\n0s");
	return ensureDir(getHomePath() + "\\.n0s");
}

std::string getCacheDir()
{
	// %LOCALAPPDATA%\n0s (e.g. C:\Users\miner\AppData\Local\n0s)
	char buf[MAX_PATH];
	if(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, buf) == S_OK)
		return ensureDir(std::string(buf) + "\\n0s");
	return ensureDir(getHomePath() + "\\.n0s\\cache");
}

// ─── Console ─────────────────────────────────────────────────────────────────

int getKey()
{
#if defined(_MSC_VER)
	if(_kbhit())
		return _getch();
#else
	// MinGW: use Windows Console API
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD avail = 0;
	INPUT_RECORD ir;
	if(GetNumberOfConsoleInputEvents(hStdin, &avail) && avail > 0)
	{
		DWORD read = 0;
		if(PeekConsoleInputA(hStdin, &ir, 1, &read) && read > 0 &&
		   ir.EventType == KEY_EVENT && ir.Event.KeyEvent.bKeyDown)
		{
			ReadConsoleInputA(hStdin, &ir, 1, &read);
			return ir.Event.KeyEvent.uChar.AsciiChar;
		}
	}
#endif
	return -1;
}

void enableConsoleColors()
{
	// Enable virtual terminal processing on Windows 10+
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if(hOut == INVALID_HANDLE_VALUE) return;

	DWORD mode = 0;
	if(!GetConsoleMode(hOut, &mode)) return;

	mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(hOut, mode);
}

void formatLocalTime(char* buf, size_t bufLen, const char* fmt, int64_t t)
{
	time_t tt = static_cast<time_t>(t);
	struct tm stime;
	localtime_s(&stime, &tt); // Windows: args reversed vs POSIX
	strftime(buf, bufLen, fmt, &stime);
}

// ─── Signals ─────────────────────────────────────────────────────────────────

void disableSigpipe()
{
	// No SIGPIPE on Windows — TCP send failures return SOCKET_ERROR
}

static void (*s_shutdownHandler)(int) = nullptr;

static BOOL WINAPI consoleCtrlHandler(DWORD ctrlType)
{
	if(s_shutdownHandler != nullptr &&
		(ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || ctrlType == CTRL_CLOSE_EVENT))
	{
		s_shutdownHandler(static_cast<int>(ctrlType));
		return TRUE;
	}
	return FALSE;
}

void installShutdownHandler(void (*handler)(int))
{
	s_shutdownHandler = handler;
	SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
}

// ─── Process / Browser ───────────────────────────────────────────────────────

void openBrowser(const char* url)
{
	ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

int spawnProcess(const char* path, const char* const argv[])
{
	// Build command line from argv
	std::string cmdLine;
	for(int i = 0; argv[i] != nullptr; i++)
	{
		if(i > 0) cmdLine += ' ';
		// Quote args containing spaces
		std::string arg(argv[i]);
		if(arg.find(' ') != std::string::npos)
			cmdLine += '"' + arg + '"';
		else
			cmdLine += arg;
	}

	STARTUPINFOA si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};

	if(!CreateProcessA(path, const_cast<char*>(cmdLine.c_str()),
		nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
	{
		return -1;
	}

	DWORD pid = pi.dwProcessId;
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return static_cast<int>(pid);
}

// ─── Threading ───────────────────────────────────────────────────────────────

void setThreadName(const char* name)
{
	// SetThreadDescription requires Win10 1607+ — may not be in older MinGW headers
	// Use runtime dynamic loading to be safe
	typedef HRESULT(WINAPI* SetThreadDescriptionFn)(HANDLE, PCWSTR);
	static auto fn = reinterpret_cast<SetThreadDescriptionFn>(
		GetProcAddress(GetModuleHandleA("kernel32.dll"), "SetThreadDescription"));
	if(!fn) return;

	int wLen = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
	if(wLen <= 0) return;
	std::wstring wname(wLen, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, name, -1, &wname[0], wLen);
	fn(GetCurrentThread(), wname.c_str());
}

// ─── Sockets ─────────────────────────────────────────────────────────────────

void sockInit()
{
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
}

void sockCleanup()
{
	WSACleanup();
}

// ─── Platform Detection ──────────────────────────────────────────────────────

const char* platformName()
{
	return "windows";
}

} // namespace platform
} // namespace n0s

#endif // _WIN32
