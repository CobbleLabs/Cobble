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

    enum class Mode {
        Pause = 0,  // One attack per pauseDuration (slow methodical hits)
        Burst = 1   // Only attack when target hurtTime == 0 (perfect timing)
    };

    struct Settings {
        Mode mode = Mode::Burst;

        // Pause mode: time between attacks in ms
        int pauseDurationMs = 500;

        // Burst mode: chance to cancel a bad hit (0-100%)
        float cancelRate = 100.0f;

        // Cancel rate for swings at air (0-100%)
        float missedSwingsRate = 50.0f;

        // Show swing animation even when click is blocked
        bool fakeSwing = true;

        // Conditions
        bool weaponOnly = false;
        bool targetingPlayerOnly = false;
    } settings;

    // Called by AutoClicker before executing a click
    // Returns true if click should proceed, false if blocked
    bool shouldAllowClick();

    // Check if currently targeting a valid entity
    bool isTargetingEntity();

private:
    std::chrono::steady_clock::time_point lastAllowedAttack;
    std::chrono::steady_clock::time_point lastBlockedClick;

    // Cached target state
    int cachedTargetHurtTime = 0;
    bool hasTarget = false;

    // RNG for cancel rate
    std::mt19937 rng;

    // Helper methods
    bool isHoldingWeapon();
    int getHeldItemId();
    bool checkCancelRate(float rate);
    void doFakeSwing();
};

extern HitSelect* hitSelect;
