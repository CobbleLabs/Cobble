#pragma once

#include <Windows.h>
#include <vector>
#include "module.h"

// Handles keybinds and update loop for all registered modules
class ModuleManager {
public:
    void registerModule(Module* mod);
    void update();
    void onRender();  // Called from overlay for visual modules
    bool hasRenderWork() const;
    bool needsPlayerCache() const;
    void checkKeybinds();

    void onWorldChange();

    const std::vector<Module*>& getModules() const { return modules; }

private:
    std::vector<Module*> modules;

    // Keybind state tracking to detect key press (not hold)
    bool keyStates[256] = {false};

    // Check if Minecraft window is focused
    bool isMinecraftFocused();
};

extern ModuleManager* moduleManager;
