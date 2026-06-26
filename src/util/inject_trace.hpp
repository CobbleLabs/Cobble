#pragma once

// Minimal file-based tracer for inject-time debugging.
// Uses ONLY Win32 APIs — no std::*, no CRT formatting — so it works even if
// CRT init / TLS callbacks haven't completed. Each call opens, appends,
// flushes and closes so a hard crash mid-init still leaves a readable log.
//
// Output: %TEMP%\cobble_trace.log
//
// Compiles to nothing in Pack builds: the macro expands to `((void)0)` so
// there's zero overhead and no string literals leaked into the shipped
// binary. Active only in dev builds (COBBLE_DEV_BUILD defined via
// generated/build_flags.h).

#include <Windows.h>

#ifdef COBBLE_DEV_BUILD

namespace cobble_trace {

inline void write_step(const char* step) {
    if (!step) return;

    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 24) return;
    // Append filename
    const char* fname = "\\cobble_trace.log";
    for (DWORD i = 0; fname[i] && n + i < MAX_PATH - 1; ++i) path[n + i] = fname[i];
    path[n + 17] = '\0';

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char buf[2048];
    int len = wsprintfA(buf, "[%02u:%02u:%02u.%03u] tid=%lu pid=%lu  %s\r\n",
                        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                        GetCurrentThreadId(), GetCurrentProcessId(), step);
    if (len > 0) {
        DWORD written = 0;
        WriteFile(h, buf, (DWORD)len, &written, nullptr);
        FlushFileBuffers(h);
    }
    CloseHandle(h);
}

inline void clear() {
    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 24) return;
    const char* fname = "\\cobble_trace.log";
    for (DWORD i = 0; fname[i] && n + i < MAX_PATH - 1; ++i) path[n + i] = fname[i];
    path[n + 17] = '\0';
    DeleteFileA(path);
}

}  // namespace cobble_trace

#define COBBLE_TRACE(step) ::cobble_trace::write_step(step)
#define COBBLE_TRACE_RESET() ::cobble_trace::clear()

// One-shot variant: emits the trace the first time this site is reached, then
// stays silent. Useful for bisection inside a tight per-frame loop where you
// want to know "did we ever reach this point?" without spamming every frame.
#define COBBLE_TRACE_ONCE(step)                  \
    do {                                         \
        static bool _cobble_trace_once = false;  \
        if (!_cobble_trace_once) {               \
            _cobble_trace_once = true;           \
            ::cobble_trace::write_step(step);    \
        }                                        \
    } while (0)

#else  // !COBBLE_DEV_BUILD

#define COBBLE_TRACE(step) ((void)0)
#define COBBLE_TRACE_RESET() ((void)0)
#define COBBLE_TRACE_ONCE(step) ((void)0)

#endif
