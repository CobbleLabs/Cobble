#include "module_manager.h"
#include "../sdk/core/minecraft.h"
#include "../ui/menu/menu.h"
#include "../util/diagnostics.hpp"
#include "../util/state_check.hpp"
#include "../security/vmprotect.h"
#include "utility/autorefill.h"

#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Psapi.h>
#include <Windows.h>

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
    // Only process keybinds and modules when Minecraft is focused
    if (!isMinecraftFocused())
        return;

    // Distributed state check #1: gateAB combines stateFlags + checksum
    // invariance. If a cracker stubs Module::shouldUpdate() / isOperational(),
    // this still guards the entire update loop. Picked AB rather than ABC
    // because gateC (timing) needs the heartbeat thread alive.
    if (!sdk::internal::gateAB())
        return;

    COBBLE_DLL_ULTRA_START("Cobble.ModuleManager.Update");
    checkKeybinds();

    for (auto* mod : modules) {
        if (mod->enabled && mod->shouldUpdate()) {
            // Set current module name for diagnostics
            Diagnostics::g_currentOperation.store(mod->name);
            mod->onUpdate();
        }
    }
    Diagnostics::g_currentOperation.store("moduleManager.update.done");

    // AutoRefill passive mode: always update when onInventoryOpen is enabled
    if (autoRefill && !autoRefill->enabled && autoRefill->settings.onInventoryOpen) {
        if (autoRefill->shouldUpdate())
            autoRefill->onUpdate();
    }
    COBBLE_DLL_ULTRA_END();
}

void ModuleManager::onRender() {
    // Distributed state check #2: gateD uses an indirect function pointer
    // wired at boot. The render loop has its own enforcement so a patch to
    // ::update() doesn't leak rendering artifacts.
    if (!sdk::internal::gateD())
        return;
    COBBLE_DLL_ULTRA_START("Cobble.ModuleManager.Render");
    for (auto* mod : modules) {
        if (mod->enabled)
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

void ModuleManager::checkKeybinds() {
    if (mc.isInGui())
        return;
    if (Menu::isOpen)
        return;

    // Collect all unique keybinds and their current state
    std::unordered_map<int, bool> currentStates;
    for (auto* mod : modules) {
        if (mod->keybind != 0 && currentStates.find(mod->keybind) == currentStates.end()) {
            currentStates[mod->keybind] = (GetAsyncKeyState(mod->keybind) & 0x8000) != 0;
        }
    }

    // Toggle all modules with matching keybind on key press
    for (auto* mod : modules) {
        if (mod->keybind == 0)
            continue;

        bool pressed = currentStates[mod->keybind];
        bool wasPressed = keyStates[mod->keybind];

        if (pressed && !wasPressed) {
            mod->toggle();
        }
    }

    // Update key states after processing all modules
    for (auto& [key, pressed] : currentStates) {
        keyStates[key] = pressed;
    }
}
