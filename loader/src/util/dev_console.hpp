#pragma once

#ifdef COBBLE_DEV_BUILD

#include <Windows.h>
#include <cstdarg>
#include <cstdio>

namespace DevConsole {

inline void Log(const char* format, ...) {
    SYSTEMTIME time = {};
    GetLocalTime(&time);
    std::fprintf(stdout, "[%02u:%02u:%02u.%03u] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);

    va_list args;
    va_start(args, format);
    std::vfprintf(stdout, format, args);
    va_end(args);

    std::fputc('\n', stdout);
    std::fflush(stdout);
}

inline void Initialize() {
    if (!GetConsoleWindow() && !AllocConsole()) {
        return;
    }

    SetConsoleTitleA("Cobble Loader - Developer Console");
    SetConsoleOutputCP(CP_UTF8);

    FILE* stream = nullptr;
    freopen_s(&stream, "CONOUT$", "w", stdout);
    freopen_s(&stream, "CONOUT$", "w", stderr);

    Log("Developer diagnostics enabled");
}

}  // namespace DevConsole

#define COBBLE_DEV_LOG(...) DevConsole::Log(__VA_ARGS__)

#else

namespace DevConsole {
inline void Initialize() {}
}  // namespace DevConsole

#define COBBLE_DEV_LOG(...) ((void)0)

#endif
