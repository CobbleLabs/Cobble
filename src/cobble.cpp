#include "cobble.hpp"
#include "config/config.hpp"
#include "module/module_manager.h"
#include "module/combat/autoclicker.h"
#include "module/combat/aimassist.h"
#include "module/combat/autorefill.h"
#include "module/combat/velocity.h"
#include "module/combat/reach.h"
#include "module/combat/blink.h"
#include "module/combat/criticals.h"
#include "module/utility/nojumpdelay.h"
#include "module/utility/noitemrelease.h"
#include "module/utility/bridgeassist.h"
#include "module/utility/sprint.h"
#include "module/utility/fastplace.h"
#include "module/utility/timer.h"
#include "module/utility/autotool.h"
#include "module/visual/nametags.h"
#include "module/visual/esp.h"
#include "module/visual/tracers.h"
#include "module/visual/blockesp.h"
#include "module/visual/trajectories.h"

#include "hooks/winsock_hooks.h"
#include "ui/core/overlay.h"
#include "ui/hud/notifications.h"
#include "util/team/friends_manager.h"
#include "sdk/player/render_data.h"
#include "sdk/network/network_probe.hpp"
#include "jni/packet_interceptor.hpp"
#include "sdk/network/lunar_packet_listener.hpp"
#include "security/loader_verify.hpp"
#include "security/session_validator.hpp"
#include "security/dll_integrity.hpp"

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
    // CRITICAL: Verify we were loaded by legitimate loader, not via LoadLibrary
    LoaderVerify::verifyOrDie(hModule);
    
	// CRITICAL: Validate injection secret from loader's shared memory
	// Without this, the DLL refuses to run - this is the primary security gate
	if (!SessionValidator::ValidateInjection()) {
		// Corrupt memory and exit to frustrate reverse engineering
		DWORD oldProtect;
		if (VirtualProtect(hModule, 0x1000, PAGE_READWRITE, &oldProtect)) {
			memset(hModule, 0xCC, 0x1000);
		}
		ExitProcess(0);
		return;
	}
	
	// Initialize DLL integrity check (hash .text section for anti-patch detection)
	// Must be done BEFORE any JIT/hooks that would modify code
	DllIntegrity::Initialize(hModule);
	
	// Start heartbeat thread for runtime validation
	// Session token was cached from shared memory during ValidateInjection()
	SessionValidator::StartHeartbeatThread("");

    // Initialize logging console immediately
    c_logger::initialize();
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
    criticals = new Criticals();
    noJumpDelay = new NoJumpDelay();
    noItemRelease = new NoItemRelease();
    bridgeAssist = new BridgeAssist();
    sprint = new Sprint();
    fastPlace = new FastPlace();
    timerModule = new TimerMod();
    autoTool = new AutoTool();
    nameTags = new NameTags();
    esp = new ESP();
    tracers = new Tracers();
    blockESP = new BlockESP();
    trajectories = new Trajectories();
    friendsManager = new FriendsManager();
    log_ok("Modules allocated");

    log_raw("Registering modules...");
    moduleManager->registerModule(autoClicker);
    moduleManager->registerModule(aimAssist);
    moduleManager->registerModule(autoRefill);
    moduleManager->registerModule(velocity);
    moduleManager->registerModule(reach);
    moduleManager->registerModule(blink);
    moduleManager->registerModule(criticals);
    moduleManager->registerModule(noJumpDelay);
    moduleManager->registerModule(noItemRelease);
    moduleManager->registerModule(bridgeAssist);
    moduleManager->registerModule(sprint);
    moduleManager->registerModule(fastPlace);
    moduleManager->registerModule(timerModule);
    moduleManager->registerModule(autoTool);
    moduleManager->registerModule(nameTags);
    moduleManager->registerModule(esp);
    moduleManager->registerModule(tracers);
    moduleManager->registerModule(blockESP);
    moduleManager->registerModule(trajectories);
    log_ok("Modules registered");
    
    // Initialize config (loads local, applies to modules, async fetch from server)
    log_raw("Initializing Config...");
    ConfigManager::get().init();
    
    log_raw("Initializing WSA hooks...");
    if (WinsockHooks::Init()) {
        log_ok("WSA hooks initialized");
    } else {
        log_warn("WSA hooks failed - using PacketInterceptor only");
    }

    // Initialize PacketInterceptor for Java-level packet filtering (primary)
    log_raw("Initializing PacketInterceptor...");
    if (PacketInterceptor::Init()) {
        log_ok("PacketInterceptor initialized");
    } else {
        log_warn("PacketInterceptor failed");
    }

    // Initialize LunarPacketListener for Lunar Client API packet interception
    log_raw("Initializing LunarPacketListener...");
    if (LunarPacketListener::Init()) {
        log_ok("LunarPacketListener initialized");
    } else {
        log_warn("LunarPacketListener failed");
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
    bool overlayInitialized = false;
    bool notificationShown = false;
    
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
        
        // Update render data matrices (for ESP) - must be on main thread with JNI access
        RenderData::Update();
        
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
    if (criticals) criticals->setEnabled(false);
    if (noJumpDelay) noJumpDelay->setEnabled(false);
    if (noItemRelease) noItemRelease->setEnabled(false);
    if (bridgeAssist) bridgeAssist->setEnabled(false);
    if (sprint) sprint->setEnabled(false);
    if (fastPlace) fastPlace->setEnabled(false);
    if (timerModule) timerModule->setEnabled(false);
    if (autoTool) autoTool->setEnabled(false);
    if (nameTags) nameTags->setEnabled(false);
    if (esp) esp->setEnabled(false);
    if (tracers) tracers->setEnabled(false);
    if (blockESP) blockESP->setEnabled(false);
    if (trajectories) trajectories->setEnabled(false);
    
    LunarPacketListener::Shutdown();
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
    if (criticals) { delete criticals; criticals = nullptr; }
    if (noJumpDelay) { delete noJumpDelay; noJumpDelay = nullptr; }
    if (noItemRelease) { delete noItemRelease; noItemRelease = nullptr; }
    if (bridgeAssist) { delete bridgeAssist; bridgeAssist = nullptr; }
    if (sprint) { delete sprint; sprint = nullptr; }
    if (fastPlace) { delete fastPlace; fastPlace = nullptr; }
    if (timerModule) { delete timerModule; timerModule = nullptr; }
    if (autoTool) { delete autoTool; autoTool = nullptr; }
    if (nameTags) { delete nameTags; nameTags = nullptr; }
    if (esp) { delete esp; esp = nullptr; }
    if (tracers) { delete tracers; tracers = nullptr; }
    if (blockESP) { delete blockESP; blockESP = nullptr; }
    if (trajectories) { delete trajectories; trajectories = nullptr; }
    if (friendsManager) { delete friendsManager; friendsManager = nullptr; }
    
    if (moduleManager) { delete moduleManager; moduleManager = nullptr; }
    
    c_logger::shutdown();
}
