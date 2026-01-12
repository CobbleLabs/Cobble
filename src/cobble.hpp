#pragma once

#include "jni/java.h"
#include "sdk/core/minecraft.h"
#include "util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <thread>

namespace cobble {
struct context {
    static void initialize(HMODULE hModule);
    static void shutdown();

    static inline bool is_running;
    static inline HMODULE moduleHandle;
    static inline HANDLE hInstanceMutex;
};
}  // namespace cobble
