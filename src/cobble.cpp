#include "cobble.hpp"
#include "module/module_manager.h"
#include "module/combat/autoclicker.h"
#include "module/combat/aimassist.h"
#include "module/combat/autorefill.h"
#include "module/combat/velocity.h"
#include "module/combat/reach.h"
#include "module/utility/nojumpdelay.h"
#include "module/visual/nametags.h"
#include "overlay/overlay.h"
#include "overlay/notifications.h"
#include "util/integrity.h"

// Signature for loader detection (cleared on shutdown for re-injection support)
volatile char COBBLE_SIGNATURE[] = "COBBLE_LOADED_V1";

static void invalidateSignature()
{
	for (int i = 0; i < 16; i++) {
		COBBLE_SIGNATURE[i] = '\0';
	}
}

void cobble::context::initialize(HMODULE hModule)
{
	(void)COBBLE_SIGNATURE[0];
	moduleHandle = hModule;
	
	Java::Init();
	if (!Java::Initialized) {
		FreeLibraryAndExitThread(moduleHandle, 0);
		return;
	}

	SDK_Init();

	moduleManager.registerModule(&autoClicker);
	moduleManager.registerModule(&aimAssist);
	moduleManager.registerModule(&autoRefill);
	moduleManager.registerModule(&velocity);
	moduleManager.registerModule(&reach);
	moduleManager.registerModule(&noJumpDelay);
	moduleManager.registerModule(&nameTags);

	int tickCount = 0;
	const int OVERLAY_INIT_DELAY = 1000;
	const int INTEGRITY_CHECK_INTERVAL = 30000;
	bool overlayInitialized = false;
	bool notificationShown = false;
	
	// Initialize self-integrity check (captures .text hash)
	Integrity::Init(moduleHandle);

	is_running = true;

	while (is_running)
	{
		if (!overlayInitialized && tickCount >= OVERLAY_INIT_DELAY) {
			Overlay::Init();
			overlayInitialized = true;
		}
		
		if (overlayInitialized && !notificationShown && tickCount >= OVERLAY_INIT_DELAY + 100) {
			NotificationManager::addSystem("Cobble loaded");
			notificationShown = true;
		}
		
		tickCount++;
		player.refresh();
		moduleManager.update();
		
		// Periodic integrity check (silent exit if tampered)
		if (tickCount % INTEGRITY_CHECK_INTERVAL == 0) {
			if (!Integrity::Verify(moduleHandle)) {
				is_running = false;  // Soft exit
			}
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	shutdown();
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	ExitThread(0);
}

void cobble::context::shutdown()
{
	invalidateSignature();
	
	autoClicker.setEnabled(false);
	aimAssist.setEnabled(false);
	autoRefill.setEnabled(false);
	velocity.setEnabled(false);
	reach.setEnabled(false);
	noJumpDelay.setEnabled(false);
	nameTags.setEnabled(false);
	
	Overlay::Shutdown();
	Java::Kill();
}
