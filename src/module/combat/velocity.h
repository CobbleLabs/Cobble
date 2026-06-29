#pragma once

#include "../module.h"

class Velocity : public Module {
public:
    Velocity();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Mode enum
    enum class Mode {
        Regular = 0,  // Simple reduction
        Jump = 1,     // Jump after knockback
        Reduce = 2,   // Attack slowdown abuse
        Ignore = 3    // Cancel velocity completely
    };

    // Vertical mode for Ignore
    enum class VerticalMode {
        Never = 0,      // Always apply vertical velocity
        OnlyInAir = 1,  // Cancel only if hit mid-air
        Always = 2      // Always cancel vertical velocity
    };

    struct Settings {
        // General
        Mode mode = Mode::Regular;
        float horizontal = 100.0f;  // 0-100% (reduceTo)
        float vertical = 100.0f;    // 0-100%
        float chance = 100.0f;      // 0-100% chance to apply
        float fov = 360.0f;         // FOV filter (360 = all angles)
        int reduceDelay = 0;        // Ticks before reduction

        // Jump mode
        float jumpChance = 100.0f;  // Chance to jump

        // Reduce mode
        bool allowDoubleClicks = false;

        // Ignore mode
        VerticalMode verticalMode = VerticalMode::Never;

        // Anti-detection
        float percentageVariance = 3.0f;  // ±variance% randomization (0-10)
        int tickSpread = 1;               // Spread reduction across N ticks (0-3)
        int microDelayMs = 15;            // Random timing variance 0-N ms (0-50)

        // Conditions
        bool onlyLookingAtPlayer = false;
        bool onlyMousePressed = false;
        bool onlyMovingForward = false;
        bool onlyHoldingWeapon = false;
        bool onlySprinting = false;  // Jump mode only
    } settings;

private:
    int lastHurtTime = 0;

    struct PendingHit {
        bool active = false;
        Mode mode = Mode::Regular;
        int applyAtHurtTime = 0;
        unsigned long applyAtMs = 0;
        float horizontal = 100.0f;
        float vertical = 100.0f;
    } queuedHit;

    // Gradual application state
    struct PendingReduction {
        bool active = false;
        double targetMotionX = 0.0;
        double targetMotionY = 0.0;
        double targetMotionZ = 0.0;
        int ticksRemaining = 0;
        int totalTicks = 0;
        int lastHurtTime = -1;
    } pending;

    // Helper methods
    bool checkChance(float chance);
    bool checkFOV();
    bool checkConditions();
    bool isLookingAtPlayer();
    bool isHoldingWeapon();
    float getAttackerAngle();
    void queueHit(int hurtTime);
    void applyQueuedHit(int hurtTime);
    void continuePendingReduction(int hurtTime);
};

extern Velocity* velocity;
