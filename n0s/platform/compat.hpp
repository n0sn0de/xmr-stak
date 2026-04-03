/**
 * compat.hpp — Cross-platform POSIX compatibility shims
 *
 * Provides portable wrappers for functions that differ between
 * POSIX (Linux/macOS) and Windows (MSVC/MinGW):
 *
 *   - n0s_strcasecmp / n0s_strncasecmp  (case-insensitive string compare)
 *   - n0s_mkdir                          (directory creation)
 *   - n0s_sleep_sec                      (sleep in seconds)
 *   - n0s_popen / n0s_pclose             (process pipe)
 *   - n0s_mkstemp                        (temp file creation)
 *   - n0s_sysconf_nproc                  (CPU core count)
 *
 * Include this header instead of using POSIX functions directly.
 *
 * Note: MinGW provides most POSIX functions (strcasecmp, popen, etc.)
 * so we only need MSVC-specific shims when _MSC_VER is defined.
 */

#pragma once

#include <cstddef>
#include <cstring>
#include <string>

#if defined(_MSC_VER)
// MSVC: use Windows-specific alternatives
#include <direct.h>   // _mkdir
#include <io.h>       // _mktemp_s, _sopen_s
#include <fcntl.h>    // _O_CREAT, etc.
#include <share.h>    // _SH_DENYNO
#include <process.h>  // _popen, _pclose
#include <windows.h>  // Sleep, GetSystemInfo
#elif defined(_WIN32)
// MinGW: has POSIX-like functions but needs Windows headers for some things
#include <sys/stat.h>
#include <unistd.h>
#include <windows.h>  // Sleep, GetSystemInfo
#else
// Linux/POSIX
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace n0s
{
namespace compat
{

// ─── Case-insensitive string comparison ──────────────────────────────────────

inline int strcasecmp(const char* a, const char* b)
{
#if defined(_MSC_VER)
	return _stricmp(a, b);
#else
	return ::strcasecmp(a, b);  // POSIX + MinGW
#endif
}

inline int strncasecmp(const char* a, const char* b, size_t n)
{
#if defined(_MSC_VER)
	return _strnicmp(a, b, n);
#else
	return ::strncasecmp(a, b, n);  // POSIX + MinGW
#endif
}

// ─── Directory creation ──────────────────────────────────────────────────────

inline int mkdir(const char* path)
{
#if defined(_MSC_VER)
	return _mkdir(path);
#elif defined(_WIN32)
	// MinGW: has ::mkdir but only takes one arg on Windows
	return ::mkdir(path);
#else
	return ::mkdir(path, 0744);
#endif
}

// ─── Sleep (seconds) ─────────────────────────────────────────────────────────

inline void sleep_sec(unsigned int seconds)
{
#if defined(_WIN32)
	Sleep(seconds * 1000);  // Both MSVC and MinGW have windows.h Sleep()
#else
	::sleep(seconds);
#endif
}

// ─── Process pipes ───────────────────────────────────────────────────────────

inline FILE* popen(const char* cmd, const char* mode)
{
#if defined(_MSC_VER)
	return _popen(cmd, mode);
#else
	return ::popen(cmd, mode);  // POSIX + MinGW
#endif
}

inline int pclose(FILE* stream)
{
#if defined(_MSC_VER)
	return _pclose(stream);
#else
	return ::pclose(stream);  // POSIX + MinGW
#endif
}

// ─── Temporary file ──────────────────────────────────────────────────────────

/// Create a temporary file. Returns fd on success, -1 on failure.
/// On MSVC, uses _mktemp_s + _sopen_s.
/// On MinGW/POSIX, uses mkstemp().
/// Template must end with "XXXXXX".
inline int mkstemp(char* tmpl)
{
#if defined(_MSC_VER)
	if (_mktemp_s(tmpl, strlen(tmpl) + 1) != 0)
		return -1;
	int fd;
	errno_t err = _sopen_s(&fd, tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
		_SH_DENYNO, _S_IREAD | _S_IWRITE);
	return (err == 0) ? fd : -1;
#else
	return ::mkstemp(tmpl);  // POSIX + MinGW
#endif
}

// ─── CPU core count ──────────────────────────────────────────────────────────

inline long sysconf_nproc()
{
#if defined(_WIN32)
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return static_cast<long>(si.dwNumberOfProcessors);
#else
	return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

} // namespace compat
} // namespace n0s

// ─── Convenience macros ──────────────────────────────────────────────────────
// Drop-in replacements for POSIX functions — use in existing code with
// minimal diff. Prefixed to avoid collisions.

#define n0s_strcasecmp   n0s::compat::strcasecmp
#define n0s_strncasecmp  n0s::compat::strncasecmp
