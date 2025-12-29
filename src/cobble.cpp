#include "cobble.hpp"
#include "module/module_manager.h"
#include "module/combat/autoclicker.h"
#include "module/combat/aimassist.h"
#include "module/combat/autorefill.h"
#include "module/visual/nametags.h"
#include "overlay/overlay.h"
#include "overlay/notifications.h"

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
	moduleManager.registerModule(&autoRefill);
	moduleManager.registerModule(&nameTags);

	// Counter for delayed overlay init
	int tickCount = 0;
	const int OVERLAY_INIT_DELAY = 1000; // 1 second delay for OpenGL hooks to settle
	bool overlayInitialized = false;
	bool notificationShown = false;

	is_running = true;

	while (is_running)
	{
		// Delayed overlay init - runs on main thread to avoid detached thread issues
		if (!overlayInitialized && tickCount >= OVERLAY_INIT_DELAY) {
			Overlay::Init();
			overlayInitialized = true;
		}
		
		// Show loaded notification shortly after overlay is ready
		if (overlayInitialized && !notificationShown && tickCount >= OVERLAY_INIT_DELAY + 100) {
			NotificationManager::addSystem("Cobble loaded - Press INSERT for Menu");
			notificationShown = true;
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
	autoRefill.setEnabled(false);
	nameTags.setEnabled(false);
	
	// Signal overlay to stop rendering but don't cleanup GL resources
	// (cleanup during shutdown causes race conditions with render thread)
	Overlay::Shutdown();
	
	// Detach from JVM
	Java::Kill();
}
