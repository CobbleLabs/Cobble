#include "web/config_server.h"

#include "cobble.hpp"
#include "module/module_manager.h"
#include "module/autoclicker.h"
#include "module/aimassist.h"
#include "module/nametags.h"
#include "web/config.h"
#include "overlay/overlay.h"

void cobble::context::initialize(HMODULE hModule)
{
	moduleHandle = hModule;
	
	Java::Init();

	if (!Java::Initialized) {
		// Failed to attach - unload DLL
		FreeLibraryAndExitThread(moduleHandle, 0);
		return;
	}

	SDK_Init();

	// Register modules
	moduleManager.registerModule(&autoClicker);
	moduleManager.registerModule(&aimAssist);
	moduleManager.registerModule(&nameTags);

	// Start WebSocket server for web config
	configServer.onMessage([](const std::string& msg) {
		Config::apply(msg);
	});
	Web_Init();

	// Counter for delayed overlay init
	int tickCount = 0;
	const int OVERLAY_INIT_DELAY = 3000; // 3 seconds worth of ticks
	bool overlayInitialized = false;

	is_running = true;

	while (is_running)
	{
		// Delayed overlay init - runs on main thread to avoid detached thread issues
		if (!overlayInitialized && tickCount >= OVERLAY_INIT_DELAY) {
			Overlay::Init();
			overlayInitialized = true;
		}
		tickCount++;
		
		// Refresh player state each tick
		player.refresh();
		
		// Update all modules
		moduleManager.update();

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	shutdown();
	
	// Wait for hooks to fully unwind before unloading
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	
	// Unload DLL from process - allows re-injection
	FreeLibraryAndExitThread(moduleHandle, 0);
}

void cobble::context::shutdown()
{
	// Disable all modules first
	autoClicker.setEnabled(false);
	aimAssist.setEnabled(false);
	nameTags.setEnabled(false);
	
	Overlay::Shutdown();
	Web_Shutdown();
	Java::Kill();
}
