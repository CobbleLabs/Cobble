#pragma once

#include "jni/java.h"
#include "sdk/core/minecraft.h"
#include "util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <atomic>
#include <thread>

namespace cobble {
struct context {
    static void initialize(HMODULE hModule);
    // Returns false when native hooks could not be safely detached; the caller must
    // leave the DLL loaded in that case.
    static bool shutdown();

    // is_running is written from the UI render thread (EJECT button) and
    // read from the main loop thread, so it MUST be atomic. A plain `bool`
    // is a data race and lets the read get cached in a register, leaving
    // the loop running after the user clicked EJECT.
    static inline std::atomic<bool> is_running{false};
    static inline HMODULE moduleHandle;
    static inline HANDLE hInstanceMutex;
};
}  // namespace cobble
