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
    //   Regular    - simple % motion reduction (non-prediction servers only).
    //   Legit      - single jump-reset on the hit tick, via the real jump keybind
    //                (vanilla physics performs the jump -> position stays self-
    //                consistent, so even strict physics checks like Intave don't
    //                flag "moved incorrectly"). Subtle and safe.
    //   LegitAbuse - same keybound jump-reset but on every grounded tick of the
    //                window (re-grounds repeatedly to bleed knockback faster).
    //                No sprint / no forced forward: injecting that speed is what a
    //                strict AC can't predict. Never writes motion directly.
    //   Ignore     - hard cancel motion to 0 (non-prediction servers only).
    enum class Mode {
        Regular = 0,
        Legit = 1,
        LegitAbuse = 2,
        Ignore = 3,
        Ping = 4  // Delays the knockback reduction by a set ping value so it reads
                  // like natural latency (packet-timing based, no direct motion desync).
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

        // Legit / LegitAbuse
        float jumpChance = 100.0f;  // Chance to trigger the jump-reset
        bool forwardTap = true;     // LegitAbuse: press forward into the knockback
        int legitWindow = 10;       // Ticks the jump-reset window stays active

        // Ping mode
        int pingDelay = 100;        // ms the knockback reduction is held back (your "ping")

        // Ignore mode
        VerticalMode verticalMode = VerticalMode::Never;

        // Anti-detection (Regular only)
        float percentageVariance = 3.0f;  // ±variance% randomization (0-10)
        int tickSpread = 1;               // Spread reduction across N ticks (0-3)
        int microDelayMs = 15;            // Random timing variance 0-N ms (0-50)

        // Conditions
        bool onlyLookingAtPlayer = false;
        bool onlyMousePressed = false;
        bool onlyMovingForward = false;
        bool onlyHoldingWeapon = false;
        bool onlySprinting = false;  // Legit / LegitAbuse only
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

    // Gradual application state (Regular)
    struct PendingReduction {
        bool active = false;
        double targetMotionX = 0.0;
        double targetMotionY = 0.0;
        double targetMotionZ = 0.0;
        int ticksRemaining = 0;
        int totalTicks = 0;
        int lastHurtTime = -1;
    } pending;

    // Legit / LegitAbuse per-tick state
    int legitTicks = 0;       // Ticks left in the active hurt window
    bool inputsHeld = false;  // Whether we are currently overriding forward/sprint

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
    void handleLegit(int hurtTime, bool justGotHit);
    void releaseInputs();
};

extern Velocity* velocity;
