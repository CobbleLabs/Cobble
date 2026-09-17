#pragma once

// Minimal file-based tracer for inject-time debugging.
// Uses ONLY Win32 APIs — no std::*, no CRT formatting — so it works even if
// CRT init / TLS callbacks haven't completed.
//
// Output: %TEMP%\cobble_trace.log
//
// Compiles to nothing in Pack builds: the macro expands to `((void)0)` so
// there's zero overhead and no string literals leaked into the shipped
// binary. Active only in dev builds (COBBLE_DEV_BUILD defined via
// generated/build_flags.h).
//
// PERFORMANCE NOTE: earlier versions called FlushFileBuffers() after every
// write, forcing a physical disk sync (5-50 ms per call). When log calls hit
// the Netty read/write thread — as they do inside the incoming-packet
// observer — those 5-50 ms landed directly on the keepalive round-trip and
// pushed player ping from ~90 ms to 200-300 ms while injected. We now let the
// OS write cache handle persistence, which cuts observed per-log latency by
// ~1000×. On an unclean crash we may lose the last few KB of trace, which is
// an acceptable trade for not adding network latency in production.

#include <Windows.h>

#ifdef COBBLE_DEV_BUILD

namespace cobble_trace {

// Global append handle, opened lazily and kept open for the process lifetime.
// Guarded by an interlocked flag so first-open races collapse to one call.
inline HANDLE& trace_handle() {
    static HANDLE h = INVALID_HANDLE_VALUE;
    return h;
}

inline HANDLE open_trace_handle_once() {
    HANDLE& hRef = trace_handle();
    if (hRef != INVALID_HANDLE_VALUE)
        return hRef;

    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 24)
        return INVALID_HANDLE_VALUE;
    const char* fname = "\\cobble_trace.log";
    for (DWORD i = 0; fname[i] && n + i < MAX_PATH - 1; ++i)
        path[n + i] = fname[i];
    path[n + 17] = '\0';

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    // First-arrival wins; if another thread already installed a handle, close
    // ours and reuse theirs.
    HANDLE prev = (HANDLE)InterlockedCompareExchangePointer((PVOID*)&hRef, (PVOID)h, (PVOID)INVALID_HANDLE_VALUE);
    if (prev != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
        return prev;
    }
    return h;
}

inline void write_step(const char* step) {
    if (!step)
        return;

    HANDLE h = open_trace_handle_once();
    if (h == INVALID_HANDLE_VALUE)
        return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    char buf[2048];
    int len = wsprintfA(buf, "[%02u:%02u:%02u.%03u] tid=%lu pid=%lu  %s\r\n", st.wHour, st.wMinute, st.wSecond,
                        st.wMilliseconds, GetCurrentThreadId(), GetCurrentProcessId(), step);
    if (len > 0) {
        DWORD written = 0;
        // WriteFile on an append handle is atomic per call (Windows guarantee),
        // so many threads can log concurrently without a lock. We deliberately
        // do NOT call FlushFileBuffers here — see the perf note at the top.
        WriteFile(h, buf, (DWORD)len, &written, nullptr);
    }
}

inline void clear() {
    // Close the persistent handle first so DeleteFileA can succeed. Next
    // write_step() re-opens the file lazily.
    HANDLE& hRef = trace_handle();
    HANDLE old = (HANDLE)InterlockedExchangePointer((PVOID*)&hRef, (PVOID)INVALID_HANDLE_VALUE);
    if (old != INVALID_HANDLE_VALUE)
        CloseHandle(old);

    char path[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 24)
        return;
    const char* fname = "\\cobble_trace.log";
    for (DWORD i = 0; fname[i] && n + i < MAX_PATH - 1; ++i)
        path[n + i] = fname[i];
    path[n + 17] = '\0';
    DeleteFileA(path);
}

}  // namespace cobble_trace

#define COBBLE_TRACE(step) ::cobble_trace::write_step(step)
#define COBBLE_TRACE_RESET() ::cobble_trace::clear()

// One-shot variant: emits the trace the first time this site is reached, then
// stays silent. Useful for bisection inside a tight per-frame loop where you
// want to know "did we ever reach this point?" without spamming every frame.
#define COBBLE_TRACE_ONCE(step)                 \
    do {                                        \
        static bool _cobble_trace_once = false; \
        if (!_cobble_trace_once) {              \
            _cobble_trace_once = true;          \
            ::cobble_trace::write_step(step);   \
        }                                       \
    } while (0)

#else  // !COBBLE_DEV_BUILD

#define COBBLE_TRACE(step) ((void)0)
#define COBBLE_TRACE_RESET() ((void)0)
#define COBBLE_TRACE_ONCE(step) ((void)0)

#endif
