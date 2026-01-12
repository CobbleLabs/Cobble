#pragma once
#include <intrin.h>
#include <Windows.h>
#include <cstdint>

class AntiDebug {
private:
    static inline uint64_t baseline_ = 0;

    // Read CPU timestamp counter
    static inline uint64_t rdtsc() { return __rdtsc(); }

public:
    // Calibrate timing baseline (call once at startup)
    static void calibrate() {
        // Measure 100 iterations and average
        uint64_t total = 0;
        for (int i = 0; i < 100; i++) {
            uint64_t start = rdtsc();
            _mm_pause();  // Minimal operation
            uint64_t end = rdtsc();
            total += (end - start);
        }
        baseline_ = total / 100;
    }

    // Execute function and check if timing is suspicious
    template <typename Func>
    static bool timingTrap(Func&& func) {
        uint64_t start = rdtsc();
        func();
        uint64_t end = rdtsc();

        // If operation takes >50x baseline, debugger is present
        return (end - start) > (baseline_ * 50);
    }

    // Detect hardware breakpoints via debug registers
    static bool hasHardwareBP() {
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

        if (!GetThreadContext(GetCurrentThread(), &ctx)) {
            return false;
        }

        // Check if any debug registers are set
        return (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0);
    }

    // Check for common debugger windows
    static bool hasDebuggerWindows() {
        // Common debugger window classes
        const char* debuggers[] = {
            "Qt5QWindowIcon",    // Cheat Engine
            "OLLYDBG",           // OllyDbg
            "WinDbgFrameClass",  // WinDbg
            "ID",                // Immunity Debugger
            "Zeta Debugger",     // x64dbg
            "Rock Debugger",     // x32dbg
            "Qt5152QWindowIcon"  // Newer CE versions
        };

        for (const char* className : debuggers) {
            if (FindWindowA(className, nullptr)) {
                return true;
            }
        }

        return false;
    }

    // Combined check - returns true if debugger detected
    static bool isDebugged() {
        // Basic PEB check
        if (IsDebuggerPresent()) {
            return true;
        }

        // Hardware breakpoints
        if (hasHardwareBP()) {
            return true;
        }

        // Debugger windows
        if (hasDebuggerWindows()) {
            return true;
        }

        // Timing trap on simple operation
        bool slow = timingTrap([] {
            volatile int x = 0;
            for (int i = 0; i < 10; i++) {
                x++;
            }
        });

        return slow;
    }

    // Smoke test - inline check that's harder to patch
    static inline void smokeTest() {
        if (baseline_ == 0)
            return;  // Not calibrated yet

        // Quick timing check
        uint64_t start = rdtsc();
        volatile int dummy = 0;
        dummy++;
        uint64_t end = rdtsc();

        // If even this simple op is slow, bail
        if ((end - start) > (baseline_ * 100)) {
            *(int*)nullptr = 0;  // Silent crash
        }
    }
};
