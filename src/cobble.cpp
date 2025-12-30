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
	// Initialize logging console immediately
	c_logger::initialize(); // Fixed: static call
	log_raw("========================================");
	log_ok("Cobble injected! Build: %s", __DATE__);
	log_debug("Module Handle: 0x%p", hModule);
	
	(void)COBBLE_SIGNATURE[0];
	moduleHandle = hModule;
	
	log_raw("Initializing Java...");
	Java::Init();
	if (!Java::Initialized) {
		log_err("Java initialization failed!");
		FreeLibraryAndExitThread(moduleHandle, 0);
		return;
	}
	log_ok("Java initialized");

	log_raw("Initializing SDK...");
	SDK_Init();
	log_ok("SDK initialized");

	log_raw("Allocating modules...");
	// Dynamically allocate modules to avoid static initialization order issues
	moduleManager = new ModuleManager();
	autoClicker = new AutoClicker();
	aimAssist = new AimAssist();
	autoRefill = new AutoRefill();
	velocity = new Velocity();
	reach = new Reach();
	noJumpDelay = new NoJumpDelay();
	nameTags = new NameTags();
	log_ok("Modules allocated");

	log_raw("Registering modules...");
	moduleManager->registerModule(autoClicker);
	moduleManager->registerModule(aimAssist);
	moduleManager->registerModule(autoRefill);
	moduleManager->registerModule(velocity);
	moduleManager->registerModule(reach);
	moduleManager->registerModule(noJumpDelay);
	moduleManager->registerModule(nameTags);
	log_ok("Modules registered");

	int tickCount = 0;
	const int OVERLAY_INIT_DELAY = 1000;
	const int INTEGRITY_CHECK_INTERVAL = 30000;
	bool overlayInitialized = false;
	bool notificationShown = false;
	
	// Initialize self-integrity check (captures .text hash)
	log_raw("Initializing Integrity Check...");
	Integrity::Init(moduleHandle);
	log_ok("Integrity Check initialized. Initial Hash: 0x%llX", Integrity::g_InitialTextHash);

	is_running = true;
	log_ok("Main loop starting...");

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
		moduleManager->update();
		
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
	
	if (autoClicker) autoClicker->setEnabled(false);
	if (aimAssist) aimAssist->setEnabled(false);
	if (autoRefill) autoRefill->setEnabled(false);
	if (velocity) velocity->setEnabled(false);
	if (reach) reach->setEnabled(false);
	if (noJumpDelay) noJumpDelay->setEnabled(false);
	if (nameTags) nameTags->setEnabled(false);
	
	Overlay::Shutdown();
	Java::Kill();

	// Clean up modules
	if (autoClicker) { delete autoClicker; autoClicker = nullptr; }
	if (aimAssist) { delete aimAssist; aimAssist = nullptr; }
	if (autoRefill) { delete autoRefill; autoRefill = nullptr; }
	if (velocity) { delete velocity; velocity = nullptr; }
	if (reach) { delete reach; reach = nullptr; }
	if (noJumpDelay) { delete noJumpDelay; noJumpDelay = nullptr; }
	if (nameTags) { delete nameTags; nameTags = nullptr; }
	
	if (moduleManager) { delete moduleManager; moduleManager = nullptr; }
	
	c_logger::shutdown();
}
