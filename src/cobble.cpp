#include "cobble.hpp"
#include "module/module_manager.h"
#include "module/combat/autoclicker.h"
#include "module/combat/aimassist.h"
#include "module/combat/autorefill.h"
#include "module/combat/hitselect.h"
#include "module/visual/nametags.h"
#include "overlay/overlay.h"
#include "overlay/notifications.h"

// Signature for loader to detect if we're already injected
// This must be writable so we can clear it during shutdown to allow re-injection
// (Manual-mapped memory isn't freed, so signature persists unless we overwrite it)
volatile char COBBLE_SIGNATURE[] = "COBBLE_LOADED_V1";

// Clear the signature so loader can re-inject after eject
static void invalidateSignature()
{
	// Overwrite with zeros so memory scan won't find the signature
	for (int i = 0; i < 16; i++) {
		COBBLE_SIGNATURE[i] = '\0';
	}
}

void cobble::context::initialize(HMODULE hModule)
{
	// Touch the signature so compiler doesn't optimize it away
	(void)COBBLE_SIGNATURE[0];
	
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
	moduleManager.registerModule(&hitSelect);
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
	
	// Unload DLL from process
	// If manually mapped, FreeLibrary will crash because module is not in LDR.
	// Safe bet is to just exit the thread. The memory (DLL) stays allocated but inactive.
	// To properly free, we'd need a separate thread that frees memory after this thread exits, which is complex.
	// For now: Just ExitThread.
	
	// Check if moduleHandle is a valid HMODULE (in LDR)
    // Simple heuristic: if we are here and valid, try FreeLibrary ONLY if we think it's safe.
    // Actually, ExitThread is always safe enough for a cheat appearing to "unload".
    // Re-injecting might duplicate memory, but that's acceptable for this stage.
    
    // Using FreeLibraryAndExitThread ONLY if we are fairly sure it's a standard load (moduleHandle has high bits set typically)
    // But since we are manually mapping, let's just use ExitThread for safety across both.
    
	ExitThread(0);
	// FreeLibraryAndExitThread(moduleHandle, 0);
}

void cobble::context::shutdown()
{
	// Clear the signature FIRST so loader can re-inject after eject
	// (Memory persists after manual-map, so we must invalidate it manually)
	invalidateSignature();
	
	// Disable all modules first
	autoClicker.setEnabled(false);
	aimAssist.setEnabled(false);
	autoRefill.setEnabled(false);
	hitSelect.setEnabled(false);
	nameTags.setEnabled(false);
	
	// Signal overlay to stop rendering but don't cleanup GL resources
	// (cleanup during shutdown causes race conditions with render thread)
	Overlay::Shutdown();
	
	// Detach from JVM
	Java::Kill();
}
