#pragma once

#include <Windows.h>
#include <winternl.h>
#include <cstdint>

#include "../util/aes_string.hpp"

// ============================================================================
// RUNTIME ANTI-ATTACH
// Runs while the DLL is alive (heartbeat thread + module hot paths). Catches
// debuggers attached AFTER startup, which the one-shot LoaderValidator misses.
// Each check is intentionally cheap so it can run frequently without showing
// up as a periodic spike. Failures don't crash directly — they flip a poison
// flag that the SDK state machine will use to make modules misbehave.
// ============================================================================

namespace AntiAttach {

// Cached function pointer; resolved once and used directly so the call site
// looks like a regular indirect call rather than a string lookup. Resolving it
// lazily means the import is not statically visible in the IAT.
using NtQueryInformationProcess_t = NTSTATUS(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

inline NtQueryInformationProcess_t resolveNtQip() {
    static NtQueryInformationProcess_t fn = nullptr;
    if (fn)
        return fn;
    HMODULE hNtdll = GetModuleHandleW(OBFUSCATE_WSTRING(L"ntdll.dll").c_str());
    if (!hNtdll)
        return nullptr;
    fn = reinterpret_cast<NtQueryInformationProcess_t>(
        GetProcAddress(hNtdll, OBFUSCATE_STRING("NtQueryInformationProcess").c_str()));
    return fn;
}

// ProcessDebugPort = 7. Returns non-zero handle iff a userland debugger is
// attached. Bypassed by hooking NtQueryInformationProcess but the next check
// catches that path.
inline bool hasDebugPort() {
    auto fn = resolveNtQip();
    if (!fn)
        return false;
    DWORD_PTR debugPort = 0;
    NTSTATUS s = fn(GetCurrentProcess(), 7 /*ProcessDebugPort*/, &debugPort, sizeof(debugPort), nullptr);
    return (s == 0 /*STATUS_SUCCESS*/) && (debugPort != 0);
}

// ProcessDebugObjectHandle = 0x1E. Set by Win10+ debuggers; independent of
// ProcessDebugPort, so hooking only one is not enough. Returns non-zero
// handle iff a debugger object exists for the current process.
inline bool hasDebugObject() {
    auto fn = resolveNtQip();
    if (!fn)
        return false;
    HANDLE dbgObj = nullptr;
    NTSTATUS s = fn(GetCurrentProcess(), 0x1E /*ProcessDebugObjectHandle*/, &dbgObj, sizeof(dbgObj), nullptr);
    return (s == 0) && (dbgObj != nullptr);
}

// ProcessDebugFlags = 0x1F. Returns 0 iff a debugger is attached
// (Inversed semantics — Microsoft API design, not a bug.)
inline bool hasDebugFlags() {
    auto fn = resolveNtQip();
    if (!fn)
        return false;
    ULONG noDebug = 1;
    NTSTATUS s = fn(GetCurrentProcess(), 0x1F /*ProcessDebugFlags*/, &noDebug, sizeof(noDebug), nullptr);
    return (s == 0) && (noDebug == 0);
}

// Hardware breakpoint check: x64dbg/IDA's "trace" mode often uses Dr0-Dr3.
// Snapshot every running thread and inspect debug registers. If any thread
// has a non-zero Dr register, someone set a hardware breakpoint.
// Cheap path: only check the calling thread; for thorough sweeps the heartbeat
// can iterate all threads.
inline bool hasHardwareBreakpoint() {
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    if (!GetThreadContext(GetCurrentThread(), &ctx))
        return false;
    return (ctx.Dr0 | ctx.Dr1 | ctx.Dr2 | ctx.Dr3) != 0;
}

// PEB BeingDebugged byte — trivial to bypass by clearing the byte, but cheap
// to check and forces the cracker to do at least one patch.
inline bool isBeingDebugged() {
#ifdef _WIN64
    PPEB pPeb = reinterpret_cast<PPEB>(__readgsqword(0x60));
#else
    PPEB pPeb = reinterpret_cast<PPEB>(__readfsdword(0x30));
#endif
    return pPeb && pPeb->BeingDebugged;
}

// Composite cheap check — call from hot paths. Returns true if ANY of the
// quick userland indicators fire. Each indicator stands alone, so a cracker
// must defeat all of them, not just the first.
inline bool quickCheck() {
    if (isBeingDebugged())
        return true;
    if (hasHardwareBreakpoint())
        return true;
    if (hasDebugPort())
        return true;
    if (hasDebugObject())
        return true;
    if (hasDebugFlags())
        return true;
    return false;
}

}  // namespace AntiAttach
