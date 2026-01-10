#pragma once

#include <cstdio>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

// Set to 1 to enable debug console allocation and stdout logging
#ifndef DEBUG_CONSOLE
#define DEBUG_CONSOLE 0
#endif

enum class message_type : unsigned int {
	none = 0,
	success = 10,
	debug = 9,
	warning = 14,
	prompt = 11,
	error = 12
};

#if DEBUG_CONSOLE

class c_logger {
public:
	static void initialize() {
		AllocConsole();
		FILE* f;
		freopen_s(&f, "CONOUT$", "w", stdout);
		freopen_s(&f, "CONOUT$", "w", stderr);
		SetConsoleTitleA("Cobble Debug Console");
	}
	
	static void shutdown() {
		FreeConsole();
	}
	
	static void print(const message_type type, const char* prefix, const char* text) {
		HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		
		switch (type) {
			case message_type::success: SetConsoleTextAttribute(hConsole, 10); break;
			case message_type::debug:   SetConsoleTextAttribute(hConsole, 9);  break;
			case message_type::warning: SetConsoleTextAttribute(hConsole, 14); break;
			case message_type::error:   SetConsoleTextAttribute(hConsole, 12); break;
			default: SetConsoleTextAttribute(hConsole, 7); break;
		}
		
		if (prefix && *prefix) printf("[%s] ", prefix);
		printf("%s\n", text);
		
		SetConsoleTextAttribute(hConsole, 7);
	}
	
	static void writeRaw(const char* text) {
		printf("%s\n", text);
	}
};

#define log_debug(fmt, ...) do { char buf[1024]; snprintf(buf, 1024, fmt, ##__VA_ARGS__); c_logger::print(message_type::debug, "DEBUG", buf); } while(0)
#define log_ok(fmt, ...)    do { char buf[1024]; snprintf(buf, 1024, fmt, ##__VA_ARGS__); c_logger::print(message_type::success, "OK", buf); } while(0)
#define log_err(fmt, ...)   do { char buf[1024]; snprintf(buf, 1024, fmt, ##__VA_ARGS__); c_logger::print(message_type::error, "ERROR", buf); } while(0)
#define log_warn(fmt, ...)  do { char buf[1024]; snprintf(buf, 1024, fmt, ##__VA_ARGS__); c_logger::print(message_type::warning, "WARN", buf); } while(0)
#define log_raw(fmt, ...)   do { char buf[1024]; snprintf(buf, 1024, fmt, ##__VA_ARGS__); c_logger::writeRaw(buf); } while(0)

#else

// No-op when debug console disabled
class c_logger {
public:
	static void initialize() {}
	static void shutdown() {}
};

#define log_debug(fmt, ...) ((void)0)
#define log_ok(fmt, ...)    ((void)0)
#define log_err(fmt, ...)   ((void)0)
#define log_warn(fmt, ...)  ((void)0)
#define log_raw(fmt, ...)   ((void)0)

#endif
