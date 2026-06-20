#pragma once

#include "../module.h"

class Criticals : public Module {
public:
    Criticals();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;

    struct Settings {
        int maxDelayMs = 100;           // Delay between criticals (50-500ms)
        int chance = 80;                // Chance to do critical (0-100%)
        bool onlyWhileAttacking = true; // Only apply when LMB pressed
        bool lookingAtPlayer = true;    // Only apply when looking at a player
        bool onlyWithSword = false;     // Only apply when holding sword
    } settings;

private:
    unsigned long lastCriticalTime = 0;
    bool wasAttacking = false;

    bool isHoldingSword();
    bool isLookingAtPlayer();
    bool canDoCritical();
    bool checkChance();
};

extern Criticals* criticals;
