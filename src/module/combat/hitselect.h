#pragma once

#include <chrono>
#include <random>
#include "../module.h"

class HitSelect : public Module {
public:
    HitSelect();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;

    struct Settings {
        // Minimum hits in combo before filtering kicks in
        int minComboHits = 0;

        // Max time between hits to count as same combo (ms)
        int comboDetectionTime = 100;

        // Wait for the first successful hit before filtering
        bool waitForFirstHit = true;

        // How long to filter after detecting a combo (ms)
        int waitDuration = 500;

        // Conditions
        bool weaponOnly = true;
    } settings;

    // Called by AutoClicker before executing a click
    // Returns true if click should proceed, false if blocked
    bool shouldAllowClick();

private:
    std::chrono::steady_clock::time_point lastHitTime;
    std::chrono::steady_clock::time_point comboStartTime;
    std::chrono::steady_clock::time_point lastAllowedClick;

    int comboHitCount = 0;
    bool inCombo = false;
    bool firstHitLanded = false;

    // Cached state
    int lastTargetHurtTime = 0;
    bool hasTarget = false;

    // Helpers
    bool isHoldingWeapon();
    int getHeldItemId();
    void detectHit();
};

extern HitSelect* hitSelect;
