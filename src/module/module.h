#pragma once

#include <string>
#include "../ui/hud/notifications.h"
#include "../util/aes_string.hpp"
#include "../util/state.hpp"

struct Module {
    const char* name;
    bool enabled = false;
    int keybind = 0;               // Virtual key code, 0 = no keybind
    bool locked = false;           // If true, module is hidden/grayed in UI until unlocked
    bool renders = false;          // If true, module draws in onRender
    bool usesPlayerCache = false;  // If true, module uses shared visual cache

    // Global unlock for experimental modules (Velocity, Reach)
    static inline bool experimentalUnlocked = false;

    // Unlock code is computed at runtime via OBFUSCATE_STRING so the literal
    // never lives in .rdata. Compare with checkUnlockCode() — never expose it.
    static std::string unlockCodeMaterialized() {
        return OBFUSCATE_STRING("CityboyKirkificationsahurnigger");
    }
    static bool checkUnlockCode(const char* candidate) {
        if (!candidate)
            return false;
        std::string expected = unlockCodeMaterialized();
        bool ok = (expected.size() == strlen(candidate)) && (memcmp(expected.data(), candidate, expected.size()) == 0);
        SecureZeroMemory(&expected[0], expected.size());
        return ok;
    }

    Module(const char* name) : name(name) {}
    virtual ~Module() = default;

    // Check if module is accessible in UI (not locked OR experiments unlocked)
    bool isAccessible() const { return !locked || experimentalUnlocked; }
    virtual bool showsDeveloperOnly() const { return locked && !experimentalUnlocked; }

    // Called when module is enabled
    virtual void onEnable() {}

    // Called when module is disabled
    virtual void onDisable() {}

    // Called every tick while enabled
    virtual void onUpdate() {}

    // Called every frame for rendering (visual modules)
    virtual void onRender() {}

    // Called to render this module's settings in the menu
    // Each module implements its own UI using ui:: widgets
    virtual void onRenderMenu() {}

    // Returns extra info to display in ArrayList (e.g., "Normal 17.0 cps")
    // Override in modules that want to show status info
    virtual std::string getDisplayInfo() { return ""; }

    // Check if module can be toggled (override to block toggle in certain states)
    virtual bool canToggle() { return true; }

    // Check if SDK state allows operation (called before onUpdate)
    // Returns false if state is degraded - module should skip update
    bool shouldUpdate() const { return sdk::internal::isOperational(); }

    void toggle() {
        // Don't allow toggling locked modules unless unlocked
        if (locked && !experimentalUnlocked)
            return;

        // Check if module allows toggle in current state
        if (!canToggle())
            return;

        enabled = !enabled;
        if (enabled)
            onEnable();
        else
            onDisable();

        // Send notification
        NotificationManager::add(name, enabled);
    }

    void setEnabled(bool value) {
        if (enabled == value)
            return;
        toggle();
    }
};
