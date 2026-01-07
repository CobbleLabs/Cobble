#include "cobble.hpp"
#include "config/config.hpp"
#include "module/module_manager.h"
#include "module/combat/autoclicker.h"
#include "module/combat/aimassist.h"
#include "module/combat/autorefill.h"
#include "module/combat/velocity.h"
#include "module/combat/reach.h"
#include "module/combat/blink.h"
#include "module/utility/nojumpdelay.h"
#include "module/utility/noitemrelease.h"
#include "module/utility/bridgeassist.h"
#include "module/visual/nametags.h"
#include "module/visual/esp.h"
#include "module/visual/tracers.h"
#include "module/visual/blockesp.h"
#include "hooks/winsock_hooks.h"
#include "ui/core/overlay.h"
#include "ui/hud/notifications.h"
#include "util/integrity.h"
#include "util/friends_manager.h"
#include "util/visual_team_resolver.h"
#include "sdk/player/render_data.h"
#include "sdk/network/network_probe.hpp"
#include "jni/packet_interceptor.hpp"


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

	// Run NetworkManager discovery probe (debug only)
	log_raw("Running NetworkManager probe...");
	NetworkProbe::run();


	log_raw("Allocating modules...");
	// Dynamically allocate modules to avoid static initialization order issues
	moduleManager = new ModuleManager();
	autoClicker = new AutoClicker();
	aimAssist = new AimAssist();
	autoRefill = new AutoRefill();
	velocity = new Velocity();
	reach = new Reach();
	blink = new Blink();
	noJumpDelay = new NoJumpDelay();
	noItemRelease = new NoItemRelease();
	bridgeAssist = new BridgeAssist();
	nameTags = new NameTags();
	esp = new ESP();
	tracers = new Tracers();
	blockESP = new BlockESP();
	friendsManager = new FriendsManager();
	visualTeamResolver = new VisualTeamResolver();
	log_ok("Modules allocated");

	log_raw("Registering modules...");
	moduleManager->registerModule(autoClicker);
	moduleManager->registerModule(aimAssist);
	moduleManager->registerModule(autoRefill);
	moduleManager->registerModule(velocity);
	moduleManager->registerModule(reach);
	moduleManager->registerModule(blink);
	moduleManager->registerModule(noJumpDelay);
	moduleManager->registerModule(noItemRelease);
	moduleManager->registerModule(bridgeAssist);
	moduleManager->registerModule(nameTags);
	moduleManager->registerModule(esp);
	moduleManager->registerModule(tracers);
	moduleManager->registerModule(blockESP);
	log_ok("Modules registered");
	
	// Initialize config (loads local, applies to modules, async fetch from server)
	log_raw("Initializing Config...");
	ConfigManager::get().init();
	
	// Initialize WSA hooks for Blink packet blocking (fallback)
	log_raw("Initializing WSA hooks...");
	if (WinsockHooks::Init()) {
		log_ok("WSA hooks initialized");
	} else {
		log_warn("WSA hooks failed - using PacketInterceptor only");
	}

	// Initialize PacketInterceptor for Java-level packet filtering (primary)
	log_raw("Initializing PacketInterceptor...");
	if (PacketInterceptor::Init()) {
		log_ok("PacketInterceptor initialized - Blink will use Java-level interception");
	} else {
		log_warn("PacketInterceptor failed - Blink will use network-level blocking (may timeout)");
	}

	// Create Named Mutex for Injection Detection
	DWORD pid = GetCurrentProcessId();
	std::wstring mutexName = L"Global\\Cobble_Instance_" + std::to_wstring(pid);
	hInstanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		log_warn("Cobble instance mutex already exists! (Double Injection?)");
	} else {
		log_ok("Created instance mutex: %ls", mutexName.c_str());
	}
	
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
		
		// Update render data matrices (for ESP/VisualTeamResolver) - must be on main thread with JNI access
		RenderData::Update();
		
		// Background visual team scanning - pre-cache detection before AimAssist needs it
		// Run every 20 ticks (~20ms) to minimize JNI overhead
		if (visualTeamResolver && visualTeamResolver->enabled && tickCount % 20 == 0) {
			visualTeamResolver->scanAllPlayers();
		}
		
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
	
	if (hInstanceMutex) {
		ReleaseMutex(hInstanceMutex);
		CloseHandle(hInstanceMutex);
		hInstanceMutex = nullptr;
		log_raw("Released instance mutex");
	}
	
	// Save config before shutdown
	ConfigManager::get().shutdown();
	
	if (autoClicker) autoClicker->setEnabled(false);
	if (aimAssist) aimAssist->setEnabled(false);
	if (autoRefill) autoRefill->setEnabled(false);
	if (velocity) velocity->setEnabled(false);
	if (reach) reach->setEnabled(false);
	if (blink) blink->setEnabled(false);
	if (noJumpDelay) noJumpDelay->setEnabled(false);
	if (noItemRelease) noItemRelease->setEnabled(false);
	if (bridgeAssist) bridgeAssist->setEnabled(false);
	if (nameTags) nameTags->setEnabled(false);
	if (esp) esp->setEnabled(false);
	
	PacketInterceptor::Shutdown();
	WinsockHooks::Shutdown();
	
	Overlay::Shutdown();
	Java::Kill();

	// Clean up modules
	if (autoClicker) { delete autoClicker; autoClicker = nullptr; }
	if (aimAssist) { delete aimAssist; aimAssist = nullptr; }
	if (autoRefill) { delete autoRefill; autoRefill = nullptr; }
	if (velocity) { delete velocity; velocity = nullptr; }
	if (reach) { delete reach; reach = nullptr; }
	if (blink) { delete blink; blink = nullptr; }
	if (noJumpDelay) { delete noJumpDelay; noJumpDelay = nullptr; }
	if (noItemRelease) { delete noItemRelease; noItemRelease = nullptr; }
	if (bridgeAssist) { delete bridgeAssist; bridgeAssist = nullptr; }
	if (nameTags) { delete nameTags; nameTags = nullptr; }
	if (esp) { delete esp; esp = nullptr; }
	if (friendsManager) { delete friendsManager; friendsManager = nullptr; }
	if (visualTeamResolver) { delete visualTeamResolver; visualTeamResolver = nullptr; }
	
	if (moduleManager) { delete moduleManager; moduleManager = nullptr; }
	
	c_logger::shutdown();
}
