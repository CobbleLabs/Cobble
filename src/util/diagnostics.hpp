#pragma once

// Debug diagnostics for freeze/crash detection
// Active in dev builds only (COBBLE_DEV_BUILD).

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <DbgHelp.h>
#include <Windows.h>

#pragma comment(lib, "DbgHelp.lib")

#include "logger.hpp"
#include "inject_trace.hpp"

namespace Diagnostics {

// Current operation being performed (for freeze detection)
inline std::atomic<const char*> g_currentOperation{"idle"};
inline std::atomic<uint64_t> g_lastTickTime{0};
inline std::atomic<bool> g_watchdogRunning{false};
inline std::atomic<bool> g_watchdogExited{true};
inline std::atomic<bool> g_inWorldChange{false};

// Freeze threshold in milliseconds
constexpr int FREEZE_THRESHOLD_MS = 3000;  // 3 seconds without tick = freeze

// RAII scope marker for operations
class ScopeMarker {
public:
    ScopeMarker(const char* op) : prev_(g_currentOperation.load()) { g_currentOperation.store(op); }
    ~ScopeMarker() { g_currentOperation.store(prev_); }

private:
    const char* prev_;
};

#ifdef COBBLE_DEV_BUILD
#define DIAG_SCOPE(name) Diagnostics::ScopeMarker _scope_##__LINE__(name)
#else
#define DIAG_SCOPE(name) ((void)0)  // No-op in Pack builds
#endif

// Update tick timestamp (call from main loop)
inline void tick() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    g_lastTickTime.store(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

// Mark world change start/end
inline void beginWorldChange() {
    g_inWorldChange.store(true);
    log_warn("[DIAG] World change started");
}

inline void endWorldChange() {
    g_inWorldChange.store(false);
    log_ok("[DIAG] World change completed");
}

#ifdef COBBLE_DEV_BUILD

// Watchdog thread - detects freezes
inline void watchdogThread() {
    log_ok("[DIAG] Watchdog thread started");

    while (g_watchdogRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto now = std::chrono::steady_clock::now().time_since_epoch();
        uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
        uint64_t lastTick = g_lastTickTime.load();

        if (lastTick > 0) {
            uint64_t delta = nowMs - lastTick;
            if (delta > FREEZE_THRESHOLD_MS) {
                // FREEZE DETECTED!
                const char* op = g_currentOperation.load();
                bool inWorldChange = g_inWorldChange.load();

                log_err("========================================");
                log_err("[DIAG] FREEZE DETECTED!");
                log_err("  Last tick: %I64u ms ago", delta);
                log_err("  Current operation: %s", op ? op : "unknown");
                log_err("  In world change: %s", inWorldChange ? "YES" : "NO");
                log_err("========================================");

                // Reset to avoid spam
                g_lastTickTime.store(nowMs);
            }
        }
    }

    log_ok("[DIAG] Watchdog thread stopped");
    g_watchdogExited.store(true);
}

// Start watchdog
inline void startWatchdog() {
    if (g_watchdogRunning.load())
        return;

    g_watchdogExited.store(false);
    g_watchdogRunning.store(true);
    std::thread(watchdogThread).detach();
}

// Stop watchdog and wait for the detached thread to leave DLL code; the caller
// unloads the module right after shutdown.
inline void stopWatchdog() {
    g_watchdogRunning.store(false);
    while (!g_watchdogExited.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

// SEH exception filter for crash reporting
inline LONG WINAPI crashHandler(EXCEPTION_POINTERS* ep) {
    const char* op = g_currentOperation.load();
    bool inWorldChange = g_inWorldChange.load();

    // The dev console is disabled (it crashed Minecraft), so log_err() below
    // goes nowhere. Mirror the essentials into %TEMP%\cobble_trace.log using the
    // crash-safe Win32-only tracer so the failure is actually recoverable.
    // Use only wsprintfA + GetModuleHandleEx here (no CRT, no allocation).
    {
        char tbuf[1024];
        wsprintfA(tbuf, "CRASH! code=0x%08X addr=0x%p op=%s worldChange=%d",
                  (unsigned)ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress,
                  op ? op : "unknown", inWorldChange ? 1 : 0);
        COBBLE_TRACE(tbuf);

        if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
            ULONG_PTR at = ep->ExceptionRecord->ExceptionInformation[0];
            char abuf[256];
            wsprintfA(abuf, "CRASH! access=%s faultAddr=0x%p", at == 0 ? "READ" : (at == 1 ? "WRITE" : "EXEC"),
                      (void*)ep->ExceptionRecord->ExceptionInformation[1]);
            COBBLE_TRACE(abuf);
        }

        // Resolve which module the faulting instruction lives in — tells us if
        // the crash is inside Cobble.dll, the GL driver, javaw, etc.
        HMODULE faultMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)ep->ExceptionRecord->ExceptionAddress, &faultMod) &&
            faultMod) {
            char modPath[MAX_PATH];
            if (GetModuleFileNameA(faultMod, modPath, MAX_PATH)) {
                char mbuf[MAX_PATH + 64];
                wsprintfA(mbuf, "CRASH! module=%s base=0x%p", modPath, (void*)faultMod);
                COBBLE_TRACE(mbuf);
            }
        } else {
            COBBLE_TRACE("CRASH! module=<unknown / not in a loaded module>");
        }

#ifdef _WIN64
        char rbuf[256];
        wsprintfA(rbuf, "CRASH! RIP=0x%p RSP=0x%p RBP=0x%p", (void*)ep->ContextRecord->Rip,
                  (void*)ep->ContextRecord->Rsp, (void*)ep->ContextRecord->Rbp);
        COBBLE_TRACE(rbuf);
#endif
    }

    log_err("========================================");
    log_err("[DIAG] CRASH DETECTED!");
    log_err("  Exception code: 0x%08X", ep->ExceptionRecord->ExceptionCode);
    log_err("  Exception address: 0x%p", ep->ExceptionRecord->ExceptionAddress);
    log_err("  Current operation: %s", op ? op : "unknown");
    log_err("  In world change: %s", inWorldChange ? "YES" : "NO");

    // Get exception name
    const char* excName = "UNKNOWN";
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            excName = "ACCESS_VIOLATION";
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            excName = "ARRAY_BOUNDS_EXCEEDED";
            break;
        case EXCEPTION_BREAKPOINT:
            excName = "BREAKPOINT";
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            excName = "DATATYPE_MISALIGNMENT";
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            excName = "FLT_DENORMAL_OPERAND";
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            excName = "FLT_DIVIDE_BY_ZERO";
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            excName = "FLT_INEXACT_RESULT";
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            excName = "FLT_INVALID_OPERATION";
            break;
        case EXCEPTION_FLT_OVERFLOW:
            excName = "FLT_OVERFLOW";
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            excName = "FLT_STACK_CHECK";
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            excName = "FLT_UNDERFLOW";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            excName = "ILLEGAL_INSTRUCTION";
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            excName = "IN_PAGE_ERROR";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            excName = "INT_DIVIDE_BY_ZERO";
            break;
        case EXCEPTION_INT_OVERFLOW:
            excName = "INT_OVERFLOW";
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            excName = "INVALID_DISPOSITION";
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            excName = "NONCONTINUABLE_EXCEPTION";
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            excName = "PRIV_INSTRUCTION";
            break;
        case EXCEPTION_SINGLE_STEP:
            excName = "SINGLE_STEP";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            excName = "STACK_OVERFLOW";
            break;
    }
    log_err("  Exception type: %s", excName);

    // Access violation details
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        ULONG_PTR accessType = ep->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR accessAddr = ep->ExceptionRecord->ExceptionInformation[1];
        log_err("  Access type: %s", accessType == 0 ? "READ" : (accessType == 1 ? "WRITE" : "EXECUTE"));
        log_err("  Access address: 0x%p", (void*)accessAddr);
    }

    // Registers
    CONTEXT* ctx = ep->ContextRecord;
    log_err("  Registers:");
#ifdef _WIN64
    // NOTE: log_err uses wvsprintfA which doesn't support %llX. Use %I64X.
    log_err("    RAX=0x%016I64X RBX=0x%016I64X", ctx->Rax, ctx->Rbx);
    log_err("    RCX=0x%016I64X RDX=0x%016I64X", ctx->Rcx, ctx->Rdx);
    log_err("    RSP=0x%016I64X RBP=0x%016I64X", ctx->Rsp, ctx->Rbp);
    log_err("    RSI=0x%016I64X RDI=0x%016I64X", ctx->Rsi, ctx->Rdi);
    log_err("    RIP=0x%016I64X", ctx->Rip);
#else
    log_err("    EAX=0x%08X EBX=0x%08X", ctx->Eax, ctx->Ebx);
    log_err("    ECX=0x%08X EDX=0x%08X", ctx->Ecx, ctx->Edx);
    log_err("    ESP=0x%08X EBP=0x%08X", ctx->Esp, ctx->Ebp);
    log_err("    ESI=0x%08X EDI=0x%08X", ctx->Esi, ctx->Edi);
    log_err("    EIP=0x%08X", ctx->Eip);
#endif

    log_err("========================================");

    // Let default handler run (shows dialog, creates dump, etc.)
    return EXCEPTION_CONTINUE_SEARCH;
}

// Install crash handler
inline void installCrashHandler() {
    SetUnhandledExceptionFilter(crashHandler);
    log_ok("[DIAG] Crash handler installed");
}

#else  // !COBBLE_DEV_BUILD

inline void startWatchdog() {}
inline void stopWatchdog() {}
inline void installCrashHandler() {}

#endif  // COBBLE_DEV_BUILD

}  // namespace Diagnostics
