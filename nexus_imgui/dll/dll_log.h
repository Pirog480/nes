// ============================================================================
//  dll_log.h — tiny logger for nexus.dll.
//
//  Every line goes to OutputDebugStringA (DebugView / a debugger) AND is
//  appended to %TEMP%\nexus_dll.log, so a misbehaving inject can be diagnosed
//  without attaching anything.
//
//  Header-only on purpose: the DLL is built from a handful of translation
//  units and the log must be usable from all of them.
// ============================================================================
#pragma once

#ifdef _WIN32

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace nexus_log {

namespace detail {

inline void Append(const char* line)
{
    ::OutputDebugStringA(line);
    ::OutputDebugStringA("\n");

    char dir[MAX_PATH] = {};
    const DWORD n = ::GetTempPathA(MAX_PATH, dir);
    if (n == 0 || n >= MAX_PATH)
        return;
    if (n > 0 && (dir[n - 1] != '\\' && dir[n - 1] != '/'))
        std::strncat(dir, "\\", MAX_PATH - 1);
    std::strncat(dir, "nexus_dll.log", MAX_PATH - 1);

    FILE* f = nullptr;
    if (fopen_s(&f, dir, "a") != 0 || !f)
        return;

    SYSTEMTIME st = {};
    ::GetLocalTime(&st);
    std::fprintf(f, "[%04u-%02u-%02u %02u:%02u:%02u.%03u] %s\n",
                 (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay,
                 (unsigned)st.wHour, (unsigned)st.wMinute,
                 (unsigned)st.wSecond, (unsigned)st.wMilliseconds, line);
    std::fclose(f);
}

} // namespace detail

inline void Printf(const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0)
        return;
    detail::Append(buf);
}

} // namespace nexus_log

#define NEXUS_LOG(...) ::nexus_log::Printf(__VA_ARGS__)

#else // !_WIN32 — keep the header includable from unit tests on other hosts
#include <cstdarg>
#include <cstdio>
namespace nexus_log {
inline void Printf(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}
} // namespace nexus_log
#define NEXUS_LOG(...) ::nexus_log::Printf(__VA_ARGS__)
#endif
