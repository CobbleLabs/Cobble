#pragma once

#include <string>
#include <string_view>

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
	void initialize() {}
	void shutdown() {}
	void print(const message_type, std::string_view, std::string_view) {}
	void writeRaw(const std::string&) {}
};

inline auto logger = c_logger();

#define log_debug(...)	((void)0)
#define log_ok(...)		((void)0)
#define log_err(...)	((void)0)
#define log_prompt(...)	((void)0)
#define log_warn(...)	((void)0)
#define log_raw(...)	((void)0)
