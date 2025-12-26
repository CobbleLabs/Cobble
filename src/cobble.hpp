#pragma once

#include "jni/java.h"
#include "util/logger.hpp"
#include "sdk/minecraft.h"

#include <thread>

namespace cobble {
	struct context {
		static void initialize();
		static void shutdown();
		static void fastShutdown();

		static inline bool is_running;
	};
}
