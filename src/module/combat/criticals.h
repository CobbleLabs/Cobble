#pragma once

#include <chrono>
#include "../module.h"

// Criticals module - Packet mode
// Delays movement packets at the right time to increase the chance of crits
class Criticals : public Module {
public:
    Criticals();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;

    struct Settings {
        int maxDelay = 100;       // Maximum delay in ms (50-500)
        bool weaponOnly = false;  // Only activate when holding weapon
    } settings;

private:
    // State tracking
    bool isDelaying = false;
    std::chrono::steady_clock::time_point delayStartTime;
    bool wasAttacking = false;
    int lastHurtTime = 0;

    // Helper methods
    bool isHoldingWeapon();
    bool shouldStartDelay();
    void startPacketDelay();
    void stopPacketDelay();
};

extern Criticals* criticals;
