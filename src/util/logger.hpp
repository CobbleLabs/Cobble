#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdio>

#ifdef COBBLE_DEV_BUILD

class c_logger {
public:
    static void initialize() {
        // Console disabled - was crashing Minecraft.
        // COBBLE_DEV_BUILD stays on for VMProtect stubs & plaintext strings.
    }
    static void shutdown() {}
};

#define log_debug(fmt, ...) std::printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#define log_ok(fmt, ...) std::printf("[ OK  ] " fmt "\n", ##__VA_ARGS__)
#define log_err(fmt, ...) std::printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#define log_warn(fmt, ...) std::printf("[WARN ] " fmt "\n", ##__VA_ARGS__)
#define log_raw(fmt, ...) std::printf(fmt "\n", ##__VA_ARGS__)

#else // !COBBLE_DEV_BUILD

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
