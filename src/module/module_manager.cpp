#include "module_manager.h"
#include "../jni/packet/bytecode_patcher.hpp"
#include "../jni/java.h"
#include "../sdk/core/minecraft.h"
#include "../sdk/player/render_data.h"
#include "../ui/menu/menu.h"
#include "../util/diagnostics.hpp"
#include "../util/inject_trace.hpp"
#include "../util/state_check.hpp"
#include "../security/vmprotect.h"
#include "../spectate/spectate.hpp"
#include "utility/autorefill.h"


#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Psapi.h>
#include <Windows.h>

#ifdef COBBLE_DEV_BUILD
#include <atomic>
#include <chrono>

namespace ModuleDiag {

// Per-module failure tracking (compact, no heap alloc per tick)
struct ModuleHealth {
    const char* name = nullptr;
    uint32_t consecutiveSkips = 0;   // How many ticks shouldUpdate() returned false
    uint32_t exceptionCount = 0;     // How many times onUpdate() threw
    bool wasOperational = true;      // Last known shouldUpdate() result
    bool reportedDegraded = false;   // Avoid log spam: only report transition once
    bool reportedException = false;  // Same for first exception
};

inline ModuleHealth g_health[64];  // Max 64 modules tracked
inline int g_healthCount = 0;
inline std::atomic<uint64_t> g_loopIterations{0};
inline std::atomic<uint64_t> g_lastPulseLog{0};

// Pulse interval: log a "still alive" line every 60 seconds
constexpr uint64_t PULSE_INTERVAL_MS = 60000;

inline ModuleHealth* getHealth(const char* name) {
    for (int i = 0; i < g_healthCount; i++) {
        if (g_health[i].name == name) return &g_health[i];
    }
    if (g_healthCount < 64) {
        g_health[g_healthCount].name = name;
        return &g_health[g_healthCount++];
    }
    return nullptr;
}

inline void logPulse() {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    uint64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    uint64_t last = g_lastPulseLog.load();

    if (last == 0) {
        g_lastPulseLog.store(nowMs);
        return;
    }

    if (nowMs - last >= PULSE_INTERVAL_MS) {
        g_lastPulseLog.store(nowMs);
        uint64_t iters = g_loopIterations.load();

        // Count active/degraded modules
        int active = 0, degraded = 0, errored = 0;
        for (int i = 0; i < g_healthCount; i++) {
            if (!g_health[i].wasOperational) degraded++;
            if (g_health[i].exceptionCount > 0) errored++;
            // "active" = currently enabled is tracked by caller
        }

        char buf[256];
        wsprintfA(buf, "[PULSE] loops=%I64u degraded=%d errored=%d",
                  (unsigned long long)iters, degraded, errored);
        cobble_trace::write_step(buf);
    }
}

}  // namespace ModuleDiag
#endif  // COBBLE_DEV_BUILD

ModuleManager* moduleManager = nullptr;

// Check if Minecraft/Lunar window is the foreground window
bool ModuleManager::isMinecraftFocused() {
    HWND foreground = GetForegroundWindow();
    if (!foreground)
        return false;

    // Get the process ID of the foreground window
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(foreground, &foregroundPid);

    // Compare with our process
    DWORD ourPid = GetCurrentProcessId();

    return foregroundPid == ourPid;
}

void ModuleManager::registerModule(Module* mod) {
    modules.push_back(mod);
}

void ModuleManager::update() {
    const bool focused = isMinecraftFocused();
    if (!focused) {
        checkKeybinds(false);
        return;
    }

    // Distributed state check #1: gateAB combines stateFlags + checksum
    // invariance. If a cracker stubs Module::shouldUpdate() / isOperational(),
    // this still guards the entire update loop. Picked AB rather than ABC
    // because gateC (timing) needs the heartbeat thread alive.
    if (!sdk::internal::gateAB()) {
        checkKeybinds(false);
        return;
    }

    COBBLE_DLL_ULTRA_START("Cobble.ModuleManager.Update");
    checkKeybinds(true);

#ifdef COBBLE_DEV_BUILD
    ModuleDiag::g_loopIterations++;
    ModuleDiag::logPulse();
#endif

    for (auto* mod : modules) {
        if (!mod->enabled)
            continue;

        const bool shouldUpdate = mod->shouldUpdate();
        if (shouldUpdate) {
            Diagnostics::g_currentOperation.store(mod->name);

#ifdef COBBLE_DEV_BUILD
            auto* health = ModuleDiag::getHealth(mod->name);
            if (health && !health->wasOperational) {
                health->wasOperational = true;
                health->consecutiveSkips = 0;
                health->reportedDegraded = false;
                log_ok("[MOD] %s: recovered (operational again)", mod->name);
            }

            __try {
                mod->onUpdate();
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                if (health) {
                    health->exceptionCount++;
                    if (!health->reportedException) {
                        health->reportedException = true;
                        log_err("[MOD] %s: SEH exception in onUpdate() (code=0x%08X)",
                                mod->name, GetExceptionCode());
                    }
                }
                mod->enabled = false;
                log_err("[MOD] %s: disabled after exception to protect main loop", mod->name);
            }
#else
            mod->onUpdate();
#endif
        }
#ifdef COBBLE_DEV_BUILD
        else {
            auto* health = ModuleDiag::getHealth(mod->name);
            if (health) {
                health->consecutiveSkips++;
                if (health->wasOperational) {
                    health->wasOperational = false;
                    log_warn("[MOD] %s: shouldUpdate()=false (state degraded)", mod->name);
                } else if (!health->reportedDegraded && health->consecutiveSkips >= 500) {
                    health->reportedDegraded = true;
                    log_err("[MOD] %s: persistently degraded for %u ticks",
                            mod->name, health->consecutiveSkips);
                }
            }
        }
#endif
    }
    Diagnostics::g_currentOperation.store("moduleManager.update.done");

    // Apply any pending vanilla-nametag hide/restore on the main JVM thread
    // (NameTags toggles only flip a flag; the retransform must not run on the
    // render thread). Cheap no-op when nothing changed.
    BytecodePatcher::ApplyHideVanillaNames(Java::Env());

    // AutoRefill passive modes and active disabled runs update outside the
    // regular enabled-module loop.
    if (autoRefill && !autoRefill->enabled &&
        (autoRefill->settings.onInventoryOpen || autoRefill->settings.automatic || autoRefill->isRunning())) {
        if (autoRefill->shouldUpdate())
            autoRefill->onUpdate();
    }

    // Cobble Spectate: advertise presence + capture/upload host frames (when
    // watched) + pull spectator frames. Rate-limited and no-ops unless the user
    // has enabled spectate or is actively spectating. Runs on this JVM thread.
    SpectateManager::get().tick();

    COBBLE_DLL_ULTRA_END();
}

void ModuleManager::onWorldChange() {
#ifdef COBBLE_DEV_BUILD
    COBBLE_TRACE("ModuleManager::onWorldChange");
#endif
    for (auto* mod : modules) {
        mod->onWorldChange();
    }
#ifdef COBBLE_DEV_BUILD
    COBBLE_TRACE("ModuleManager::onWorldChange:done");
#endif
}

void ModuleManager::onRender() {
    // Distributed state check #2: gateD uses an indirect function pointer
    // wired at boot. The render loop has its own enforcement so a patch to
    // ::update() doesn't leak rendering artifacts.
    if (!sdk::internal::gateD())
        return;
    COBBLE_DLL_ULTRA_START("Cobble.ModuleManager.Render");
    // World-space visuals (ESP, tracers, nametags, ...) stand down while a screen
    // covers the world. The ImGui overlay is the last thing drawn each frame, so
    // otherwise they land on top of the open inventory instead of behind it --
    // boxes over the item slots, tracers sweeping across the panel. Screen-space
    // HUD modules keep drawing. The flag is sampled on the JNI thread; chat does
    // not count as covering (see RenderData::guiCoversWorld).
    const bool hideWorldVisuals = RenderData::guiCoversWorld;
    for (auto* mod : modules) {
        if (!mod->enabled)
            continue;
        if (hideWorldVisuals && mod->worldVisual)
            continue;
        mod->onRender();
    }
    COBBLE_DLL_ULTRA_END();
}

bool ModuleManager::hasRenderWork() const {
    for (const auto* mod : modules) {
        if (mod->enabled && mod->renders)
            return true;
    }
    return false;
}

bool ModuleManager::needsPlayerCache() const {
    for (const auto* mod : modules) {
        if (mod->enabled && mod->usesPlayerCache)
            return true;
    }
    return false;
}

void ModuleManager::checkKeybinds(bool allowActivation) {
    const bool inputAllowed = allowActivation && !mc.isInGui() && !Menu::isOpen;

    bool currentStates[256] = {};
    bool queried[256] = {};
    int activeKeys[256];
    int activeKeyCount = 0;

    for (auto* mod : modules) {
        const int key = mod->keybind;
        if (key <= 0 || key >= 256 || queried[key])
            continue;

        queried[key] = true;
        currentStates[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        activeKeys[activeKeyCount++] = key;
    }

    for (auto* mod : modules) {
        const int key = mod->keybind;
        const bool validKey = key > 0 && key < 256;

        if (mod->usesHoldKeybind()) {
            const bool shouldEnable = inputAllowed && validKey && currentStates[key];
            if (shouldEnable != mod->enabled)
                mod->setEnabled(shouldEnable);
            continue;
        }

        if (inputAllowed && validKey && currentStates[key] && !keyStates[key])
            mod->toggle();
    }

    for (int i = 0; i < activeKeyCount; ++i) {
        const int key = activeKeys[i];
        keyStates[key] = currentStates[key];
    }
}
