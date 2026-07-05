#pragma once

// ============================================================================
// Cobble Logger
//
// Dev builds: all log macros write to %TEMP%\cobble_trace.log via the
// crash-safe Win32 tracer (inject_trace.hpp). This replaces the previous
// printf-to-nowhere approach (the dev console is disabled).
//
// Pack builds: all macros expand to ((void)0), zero overhead.
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdio>

#ifdef COBBLE_DEV_BUILD

#include "inject_trace.hpp"

namespace cobble_log {

// Formats a log line with level prefix and writes to the trace file.
// Uses a simple static buffer with a spinlock since thread_local can be
// unreliable in injected DLLs (TLS slots may not be properly initialized).
inline void write(const char* level, const char* fmt, ...) {
    static volatile LONG g_logLock = 0;

    // Spinlock acquire
    while (InterlockedCompareExchange(&g_logLock, 1, 0) != 0) {
        Sleep(0);
    }

    static char buf[2048];

    // Write level prefix
    int prefixLen = wsprintfA(buf, "[%s] ", level);
    if (prefixLen < 0) prefixLen = 0;

    // Format user message with va_list
    va_list args;
    va_start(args, fmt);
    int msgLen = wvsprintfA(buf + prefixLen, fmt, args);
    va_end(args);

    if (msgLen < 0) msgLen = 0;

    // Null-terminate
    buf[prefixLen + msgLen] = '\0';

    // Write to trace file (crash-safe, append, flush, close)
    cobble_trace::write_step(buf);

    // Spinlock release
    InterlockedExchange(&g_logLock, 0);
}

}  // namespace cobble_log

class c_logger {
public:
    static void initialize() {
        // Trace file is already active from DllMain (COBBLE_TRACE_RESET).
        // Nothing else needed — no console allocation.
        cobble_trace::write_step("=== Logger initialized ===");
    }
    static void shutdown() {
        cobble_trace::write_step("=== Logger shutdown ===");
    }
};

#define log_debug(fmt, ...) ::cobble_log::write("DEBUG", fmt, ##__VA_ARGS__)
#define log_ok(fmt, ...) ::cobble_log::write(" OK  ", fmt, ##__VA_ARGS__)
#define log_err(fmt, ...) ::cobble_log::write("ERROR", fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) ::cobble_log::write("WARN ", fmt, ##__VA_ARGS__)
#define log_raw(fmt, ...) ::cobble_log::write(" --- ", fmt, ##__VA_ARGS__)

#else  // !COBBLE_DEV_BUILD

class c_logger {
public:
    static void initialize() {}
    static void shutdown() {}
};

#define log_debug(fmt, ...) ((void)0)
#define log_ok(fmt, ...) ((void)0)
#define log_err(fmt, ...) ((void)0)
#define log_warn(fmt, ...) ((void)0)
#define log_raw(fmt, ...) ((void)0)

#endif
