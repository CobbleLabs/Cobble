#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

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
