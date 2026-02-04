#include "cobble.hpp"
#include "config/config.hpp"
#include "module/combat/aimassist.h"
#include "module/combat/autoclicker.h"
#include "module/combat/backtrack.h"
#include "module/combat/hitbox.h"
#include "module/combat/hitselect.h"
#include "module/combat/nohitdelay.h"
#include "module/combat/criticals.h"

#include "module/combat/reach.h"
#include "module/combat/velocity.h"
#include "module/module_manager.h"
#include "module/movement/nojumpdelay.h"
#include "module/movement/blink.h"
#include "module/movement/timer.h"
#include "module/movement/sprint.h"
#include "module/utility/armorswitch.h"
#include "module/utility/autorefill.h"
#include "module/utility/autotool.h"
#include "module/utility/bridgeassist.h"
#include "module/utility/fastplace.h"
#include "module/utility/noitemrelease.h"
#include "module/utility/inventorymanager.h"
#include "module/visual/blockesp.h"
#include "module/visual/esp.h"
#include "module/visual/item_esp.h"
#include "module/visual/nametags.h"
#include "module/visual/tracers.h"
#include "module/visual/tracers.h"
#include "module/visual/trajectories.h"
#include "module/visual/dropped_info.h"

#include "hooks/winsock_hooks.h"
#include "jni/packet/packet_interceptor.hpp"
#include "sdk/network/lunar_packet_listener.hpp"
#include "sdk/network/lunar_sender.hpp"
#include "sdk/network/network_probe.hpp"
#include "sdk/player/render_data.h"
#include "sdk/player/visual_cache.h"
#include "security/dll_integrity.hpp"
#include "security/honeypot.hpp"
#include "security/loader_validator.hpp"  // Phase 4: Multiple validators
#include "security/loader_verify.hpp"
#include "security/session_validator.hpp"
#include "ui/core/overlay.h"
#include "ui/hud/notifications.h"
#include "util/diagnostics.hpp"
#include "util/state.hpp"
#include "util/team/friends_manager.h"
#include "jni/apollo/apollo_handshake.hpp"

#include <algorithm>

// Signature for loader detection (cleared on shutdown for re-injection support)
volatile char COBBLE_SIGNATURE[] = "COBBLE_LOADED_V1";

// Honeypot global variables (for async reporting from other threads)
std::string g_Honeypot_HWID;
std::string g_Honeypot_Username;

static void invalidateSignature() {
    for (int i = 0; i < 16; i++) {
        COBBLE_SIGNATURE[i] = '\0';
    }
}

void cobble::context::initialize(HMODULE hModule) {
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

    // Populate honeypot globals for async reporting
    extern std::string g_Honeypot_HWID;
    extern std::string g_Honeypot_Username;
    g_Honeypot_HWID = SessionValidator::g_HWID;
    g_Honeypot_Username = SessionValidator::g_Username;

    // Phase 4: Run multiple loader validators
    // This validates shared memory, parent process, timing, modules
    // Failures are reported to Discord and server
    if (!LoaderValidator::RunStartupValidation()) {
        // Validation failed - trigger JNI poison
        sdk::internal::markCorrupted();
    }

    // Initialize DLL integrity check (hash .text section for anti-patch detection)
    // Must be done BEFORE any JIT/hooks that would modify code
    DllIntegrity::Initialize(hModule);

    // Start heartbeat thread for runtime validation
    // Session token was cached from shared memory during ValidateInjection()
    SessionValidator::StartHeartbeatThread("");

    // Initialize logging console immediately
    c_logger::initialize();

    // Install crash handler (watchdog starts after main loop begins)
    Diagnostics::installCrashHandler();

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
    sdk::internal::onCacheReady();  // Capture SDK state baseline
    log_ok("SDK initialized");

    // Run NetworkManager discovery probe (debug only)
    log_raw("Running NetworkManager probe...");
    NetworkProbe::run();

    log_raw("Allocating modules...");
    // Dynamically allocate modules to avoid static initialization order issues
    moduleManager = new ModuleManager();
    autoClicker = new AutoClicker();
    aimAssist = new AimAssist();
    armorSwitch = new ArmorSwitch();
    autoRefill = new AutoRefill();
    velocity = new Velocity();
    reach = new Reach();

    backtrack = new Backtrack();
    hitSelect = new HitSelect();
    hitbox = new Hitbox();
    noHitDelay = new NoHitDelay();
    criticals = new Criticals();
    noJumpDelay = new NoJumpDelay();
    noItemRelease = new NoItemRelease();
    bridgeAssist = new BridgeAssist();
    blink = new Blink();
    fastPlace = new FastPlace();
    timerModule = new TimerMod();
    sprint = new Sprint();
    autoTool = new AutoTool();
    inventoryManager = new InventoryManager();
    nameTags = new NameTags();
    esp = new ESP();
    itemEsp = new ItemESP();
    tracers = new Tracers();
    blockESP = new BlockESP();
    trajectories = new Trajectories();
    droppedInfo = new DroppedInfo();
    friendsManager = new FriendsManager();
    log_ok("Modules allocated");

    log_raw("Registering modules...");
    moduleManager->registerModule(autoClicker);
    moduleManager->registerModule(aimAssist);
    moduleManager->registerModule(armorSwitch);
    moduleManager->registerModule(autoRefill);
    moduleManager->registerModule(velocity);
    moduleManager->registerModule(reach);

    moduleManager->registerModule(backtrack);
    moduleManager->registerModule(hitSelect);
    moduleManager->registerModule(hitbox);
    moduleManager->registerModule(noHitDelay);
    moduleManager->registerModule(criticals);
    moduleManager->registerModule(noJumpDelay);
    moduleManager->registerModule(noItemRelease);
    moduleManager->registerModule(bridgeAssist);
    moduleManager->registerModule(blink);
    moduleManager->registerModule(fastPlace);
    moduleManager->registerModule(timerModule);
    moduleManager->registerModule(sprint);
    moduleManager->registerModule(autoTool);
    moduleManager->registerModule(inventoryManager);
    moduleManager->registerModule(nameTags);
    moduleManager->registerModule(esp);
    moduleManager->registerModule(itemEsp);
    moduleManager->registerModule(tracers);
    moduleManager->registerModule(blockESP);
    moduleManager->registerModule(trajectories);
    moduleManager->registerModule(droppedInfo);
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
    // Initialize Network Listeners
    NetworkProbe::run();
    log_ok("NetworkProbe ran");
    
    // Lunar Integration
    LunarPacketListener::Init();
    LunarSender::Init();



    // Create Named Mutex for Injection Detection
    DWORD pid = GetCurrentProcessId();
    std::wstring mutexName = L"Global\\Cobble_Instance_" + std::to_wstring(pid);
    hInstanceMutex = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        log_warn("Cobble instance mutex already exists! (Double Injection?)");
    } else {
        log_ok("Created instance mutex: %ls", mutexName.c_str());
    }

    const auto startTime = std::chrono::steady_clock::now();
    auto lastSessionRefresh = startTime;
    auto lastPlayerRefresh = startTime;
    auto lastModuleUpdate = startTime;
    auto lastRenderUpdate = startTime;
    auto lastVisualCacheUpdate = startTime;

    constexpr auto OVERLAY_INIT_DELAY = std::chrono::milliseconds(1000);
    constexpr auto NOTIFICATION_DELAY = std::chrono::milliseconds(1100);
    constexpr auto SESSION_REFRESH_INTERVAL = std::chrono::milliseconds(1000);
    constexpr auto PLAYER_REFRESH_INTERVAL = std::chrono::milliseconds(2);
    constexpr auto MODULE_UPDATE_INTERVAL = std::chrono::milliseconds(2);
    constexpr auto RENDERDATA_UPDATE_INTERVAL = std::chrono::milliseconds(4);
    constexpr auto VISUAL_CACHE_INTERVAL = std::chrono::milliseconds(4);
    constexpr auto MIN_SLEEP = std::chrono::milliseconds(1);
    bool overlayInitialized = false;
    bool notificationShown = false;

    is_running = true;
    // Seed watchdog timestamp and start after init to avoid false freezes
    Diagnostics::tick();
    Diagnostics::startWatchdog();
    log_ok("Main loop starting...");

    while (is_running) {
        // Update watchdog tick timestamp
        Diagnostics::tick();

        auto now = std::chrono::steady_clock::now();

        if (!overlayInitialized && now - startTime >= OVERLAY_INIT_DELAY) {
            DIAG_SCOPE("Overlay::Init");
            Overlay::Init();
            overlayInitialized = true;
        }

        if (overlayInitialized && !notificationShown && now - startTime >= NOTIFICATION_DELAY) {
            NotificationManager::addSystem("Cobble loaded");
            notificationShown = true;
        }

        // Refresh SDK state periodically (keeps timing in sync)
        if (now - lastSessionRefresh >= SESSION_REFRESH_INTERVAL) {
            DIAG_SCOPE("SessionValidator::refresh");
            SessionValidator::refreshState();

            // Feed the dead man switch (prevents 5-min crash)
            sdk::internal::syncTick();

            // Check for world changes - clears teammate data on server switch
            if (friendsManager) {
                friendsManager->checkWorldChange();
            }

            // Process pending Apollo handshakes
            ApolloHandshake::Tick();

            // Check if SDK state needs recovery
            if (SessionValidator::isStateCorrupted() || SessionValidator::isStateStale()) {
                // Attempt recovery - may cause instability
                sdk::internal::attemptRecovery();
            }

            // Check devOverride honeypot
            internal::checkDevOverride();
            lastSessionRefresh = now;
        }

        if (now - lastPlayerRefresh >= PLAYER_REFRESH_INTERVAL) {
            DIAG_SCOPE("player.refresh");
            player.refresh();
            lastPlayerRefresh = now;
        }

        if (moduleManager && moduleManager->needsPlayerCache() &&
            now - lastVisualCacheUpdate >= VISUAL_CACHE_INTERVAL) {
            DIAG_SCOPE("VisualCache::Update");
            VisualCache::Update();
            lastVisualCacheUpdate = now;
        }

        if (now - lastModuleUpdate >= MODULE_UPDATE_INTERVAL) {
            DIAG_SCOPE("moduleManager.update");
            moduleManager->update();
            lastModuleUpdate = now;
        }

        // Update render data matrices (for ESP) - must be on main thread with JNI access
        if (now - lastRenderUpdate >= RENDERDATA_UPDATE_INTERVAL) {
            DIAG_SCOPE("RenderData::Update");
            RenderData::Update();
            lastRenderUpdate = now;
        }

        auto nextWake =
            std::min({lastPlayerRefresh + PLAYER_REFRESH_INTERVAL, lastModuleUpdate + MODULE_UPDATE_INTERVAL,
                      lastRenderUpdate + RENDERDATA_UPDATE_INTERVAL, lastVisualCacheUpdate + VISUAL_CACHE_INTERVAL,
                      lastSessionRefresh + SESSION_REFRESH_INTERVAL});
        auto sleepFor = nextWake - now;
        if (sleepFor > MIN_SLEEP) {
            std::this_thread::sleep_for(sleepFor);
        } else {
            std::this_thread::sleep_for(MIN_SLEEP);
        }
    }

    shutdown();
    ExitThread(0);
}

void cobble::context::shutdown() {
    SessionValidator::StopHeartbeatThread();

    invalidateSignature();

    if (hInstanceMutex) {
        ReleaseMutex(hInstanceMutex);
        CloseHandle(hInstanceMutex);
        hInstanceMutex = nullptr;
        log_raw("Released instance mutex");
    }

    // Save config before shutdown
    ConfigManager::get().shutdown();

    if (autoClicker)
        autoClicker->setEnabled(false);
    if (aimAssist)
        aimAssist->setEnabled(false);
    if (armorSwitch)
        armorSwitch->setEnabled(false);
    if (autoRefill)
        autoRefill->setEnabled(false);
    if (velocity)
        velocity->setEnabled(false);
    if (reach)
        reach->setEnabled(false);

    if (backtrack)
        backtrack->setEnabled(false);
    if (hitSelect)
        hitSelect->setEnabled(false);
    if (hitbox)
        hitbox->setEnabled(false);
    if (noHitDelay)
        noHitDelay->setEnabled(false);
    if (criticals)
        criticals->setEnabled(false);
    if (noJumpDelay)
        noJumpDelay->setEnabled(false);
    if (noItemRelease)
        noItemRelease->setEnabled(false);
    if (bridgeAssist)
        bridgeAssist->setEnabled(false);
    if (blink)
        blink->setEnabled(false);
    if (fastPlace)
        fastPlace->setEnabled(false);
    if (timerModule)
        timerModule->setEnabled(false);
    if (sprint)
        sprint->setEnabled(false);
    if (autoTool)
        autoTool->setEnabled(false);
    if (inventoryManager)
        inventoryManager->setEnabled(false);
    if (nameTags)
        nameTags->setEnabled(false);
    if (esp)
        esp->setEnabled(false);
    if (itemEsp)
        itemEsp->setEnabled(false);
    if (tracers)
        tracers->setEnabled(false);
    if (blockESP)
        blockESP->setEnabled(false);
    if (trajectories)
        trajectories->setEnabled(false);

    LunarPacketListener::Shutdown();
    PacketInterceptor::Shutdown();
    WinsockHooks::Shutdown();

    Overlay::Shutdown();
    Java::Kill();

    // Clean up modules
    if (autoClicker) {
        delete autoClicker;
        autoClicker = nullptr;
    }
    if (aimAssist) {
        delete aimAssist;
        aimAssist = nullptr;
    }
    if (armorSwitch) {
        delete armorSwitch;
        armorSwitch = nullptr;
    }
    if (autoRefill) {
        delete autoRefill;
        autoRefill = nullptr;
    }
    if (velocity) {
        delete velocity;
        velocity = nullptr;
    }
    if (reach) {
        delete reach;
        reach = nullptr;
    }

    if (backtrack) {
        delete backtrack;
        backtrack = nullptr;
    }
    if (hitSelect) {
        delete hitSelect;
        hitSelect = nullptr;
    }
    if (hitbox) {
        delete hitbox;
        hitbox = nullptr;
    }
    if (noHitDelay) {
        delete noHitDelay;
        noHitDelay = nullptr;
    }
    if (criticals) {
        delete criticals;
        criticals = nullptr;
    }
    if (noJumpDelay) {
        delete noJumpDelay;
        noJumpDelay = nullptr;
    }
    if (noItemRelease) {
        delete noItemRelease;
        noItemRelease = nullptr;
    }
    if (bridgeAssist) {
        delete bridgeAssist;
        bridgeAssist = nullptr;
    }
    if (blink) {
        delete blink;
        blink = nullptr;
    }
    if (fastPlace) {
        delete fastPlace;
        fastPlace = nullptr;
    }
    if (timerModule) {
        delete timerModule;
        timerModule = nullptr;
    }
    if (sprint) {
        delete sprint;
        sprint = nullptr;
    }
    if (autoTool) {
        delete autoTool;
        autoTool = nullptr;
    }
    if (inventoryManager) {
        delete inventoryManager;
        inventoryManager = nullptr;
    }
    if (nameTags) {
        delete nameTags;
        nameTags = nullptr;
    }
    if (esp) {
        delete esp;
        esp = nullptr;
    }
    if (itemEsp) {
        delete itemEsp;
        itemEsp = nullptr;
    }
    if (tracers) {
        delete tracers;
        tracers = nullptr;
    }
    if (blockESP) {
        delete blockESP;
        blockESP = nullptr;
    }
    if (trajectories) {
        delete trajectories;
        trajectories = nullptr;
    }
    if (droppedInfo) {
        delete droppedInfo;
        droppedInfo = nullptr;
    }
    if (friendsManager) {
        delete friendsManager;
        friendsManager = nullptr;
    }

    if (moduleManager) {
        delete moduleManager;
        moduleManager = nullptr;
    }

    c_logger::shutdown();
}
