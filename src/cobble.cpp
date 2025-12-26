#include "cobble.hpp"

void cobble::context::initialize()
{
	logger.initialize();
	Java::Init();

	log_ok("cobble initialized");

	is_running = true;

	while (is_running)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	shutdown();
}

void cobble::context::shutdown()
{
	Java::Kill();
	logger.shutdown();
}
