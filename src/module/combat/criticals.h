#pragma once

#include "../module.h"

class Criticals : public Module {
public:
    Criticals();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum Mode {
        MODE_ATTACK = 0,  // Trigger crit on the click edge (per swing)
        MODE_MS = 1,      // Trigger crit on a fixed ms interval while conditions hold
    };

    struct Settings {
        int mode = MODE_ATTACK;         // How crits are triggered (see Mode)
        int maxDelayMs = 100;           // Delay/interval between criticals (50-500ms)
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
    void doCritical();
};

extern Criticals* criticals;
