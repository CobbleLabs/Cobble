#include "web/config_server.h"

#include "cobble.hpp"
#include "module/module_manager.h"
#include "module/autoclicker.h"
#include "web/config.h"

void cobble::context::initialize()
{
	logger.initialize();
	
	log_debug("starting cobble...");
	std::cout.flush();
	
	Java::Init();

	if (!Java::Initialized) {
		log_err("java init failed, staying alive for debug");
		while (true) { std::this_thread::sleep_for(std::chrono::seconds(1)); }
	}

	SDK_Init();

	if (!mc.isValid()) {
		log_err("sdk init failed");
	}

	// Register modules
	moduleManager.registerModule(&autoClicker);

	// Start WebSocket server for web config
	configServer.onMessage([](const std::string& msg) {
		Config::apply(msg);
	});
	Web_Init();

	log_ok("cobble initialized");
	log_prompt("press R to toggle autoclicker");
	log_prompt("web config: ws://localhost:8765");

	is_running = true;

	while (is_running)
	{
		// Refresh player state each tick
		player.refresh();
		
		// Update all modules
		moduleManager.update();

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	shutdown();
}

void cobble::context::shutdown()
{
	Web_Shutdown();
	Java::Kill();
	logger.shutdown();
}
