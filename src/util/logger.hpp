#pragma once

#include <string>
#include <string_view>
#include <Windows.h>
#include <shared_mutex>

enum class message_type : std::uint32_t {
	none = 0,
	success = 10,
	debug = 9,
	warning = 14,
	prompt = 11,
	error = 12
};

class c_logger {
public:
	void initialize() {
		AllocConsole();
		
		// Create a new console screen buffer for our output only
		consoleHandle = CreateConsoleScreenBuffer(
			GENERIC_READ | GENERIC_WRITE,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr,
			CONSOLE_TEXTMODE_BUFFER,
			nullptr
		);
		
		if (consoleHandle != INVALID_HANDLE_VALUE) {
			// Make our buffer the active one
			SetConsoleActiveScreenBuffer(consoleHandle);
		}
		
		// Set console title
		SetConsoleTitleA("Cobble Client");
		
		// Print header
		writeRaw("\n");
		writeRaw("   COBBLE CLIENT\n");
		writeRaw("\n");
	}

	void shutdown() {
		if (consoleHandle && consoleHandle != INVALID_HANDLE_VALUE) {
			CloseHandle(consoleHandle);
		}
		consoleHandle = nullptr;
		FreeConsole();
	}

	void print(const message_type type, std::string_view func, std::string_view message) {
		if (!consoleHandle || consoleHandle == INVALID_HANDLE_VALUE) return;
		
		std::unique_lock<decltype(mutex)> lock(mutex);

		if (type != message_type::none) {
			SetConsoleTextAttribute(consoleHandle, 7);
			writeRawUnlocked("[ ");

			SetConsoleTextAttribute(consoleHandle, (WORD)(type));
			writeRawUnlocked(getTypeChar(type));

			SetConsoleTextAttribute(consoleHandle, 7);
			writeRawUnlocked(" ] ");
		}

		SetConsoleTextAttribute(consoleHandle, 7);
		writeRawUnlocked(std::string(message));
		writeRawUnlocked("\n");
	}
	
	void writeRaw(const std::string& text) {
		if (!consoleHandle || consoleHandle == INVALID_HANDLE_VALUE) return;
		std::unique_lock<decltype(mutex)> lock(mutex);
		writeRawUnlocked(text);
	}

private:
	HANDLE consoleHandle = nullptr;
	std::shared_timed_mutex mutex{};
	
	void writeRawUnlocked(const std::string& text) {
		DWORD written;
		WriteConsoleA(consoleHandle, text.c_str(), (DWORD)text.size(), &written, nullptr);
	}
	
	const char* getTypeChar(message_type type) {
		switch (type) {
			case message_type::debug:   return ".";
			case message_type::success: return "+";
			case message_type::error:   return "!";
			case message_type::prompt:  return ">";
			case message_type::warning: return "*";
			default: return "";
		}
	}
};

inline auto logger = c_logger();

#define log_debug(...)	logger.print(message_type::debug,   __FUNCTION__, __VA_ARGS__)
#define log_ok(...)		logger.print(message_type::success, __FUNCTION__, __VA_ARGS__)
#define log_err(...)	logger.print(message_type::error,   __FUNCTION__, __VA_ARGS__)
#define log_prompt(...)	logger.print(message_type::prompt,  __FUNCTION__, __VA_ARGS__)
#define log_warn(...)	logger.print(message_type::warning, __FUNCTION__, __VA_ARGS__)
#define log_raw(...)	logger.print(message_type::none,    __FUNCTION__, __VA_ARGS__)
