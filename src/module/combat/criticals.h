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

    bool isRecentlyCritical(unsigned long windowMs = 400) const;

    // Critical technique.
    //   MiniJump - small real hop, smooth and consistent.
    //   Jump     - full vanilla jump height (classic manual critical).
    enum class Mode : int {
        MiniJump = 0,
        Jump = 1,
    };

    // What decides WHEN a critical fires. Kept separate from Mode so the
    // technique stays a free choice - Hits works with MiniJump or Jump.
    //   Chance - roll the Chance slider on every hit (original behaviour).
    //   Hits   - fire on a fixed cadence, every Nth hit.
    enum class Trigger : int {
        Chance = 0,
        Hits = 1,
    };

    struct Settings {
        Mode mode = Mode::MiniJump;         // Critical technique
        Trigger trigger = Trigger::Chance;  // What decides when to fire
        int hitInterval = 2;                // Hits: fire every Nth hit
        int maxDelayMs = 100;               // Delay between criticals (50-500ms)
        int chance = 80;                    // Chance to do critical (0-100%)
        bool onlyWhileAttacking = true;     // Only apply when LMB pressed
        bool lookingAtPlayer = true;        // Only apply when looking at a player
        bool onlyWithSword = false;         // Only apply when holding sword
    } settings;

private:
    unsigned long lastCriticalTime = 0;
    bool wasAttacking = false;

    // Hits trigger: length of the CURRENT combo, i.e. how many hits have landed
    // back to back. A swing that misses, or a long enough gap between hits, ends
    // the streak and starts counting again. Only reset on a fired critical or a
    // broken streak, so a hit that lands mid-air or inside the delay does not
    // silently burn a slot in the cadence.
    int hitCount = 0;
    unsigned long lastHitTime = 0;

    void doJumpCritical(double height);
    void startCritical();

    bool isHoldingSword();
    bool isLookingAtPlayer();
    // Target/gear conditions: is this click a real attack worth counting?
    bool isValidAttack();
    // Hits trigger: advance the combo streak and fire when it reaches the target.
    void handleComboHit();
    // Physical readiness: on the ground, not sneaking, delay elapsed.
    bool canDoCritical();
    bool checkChance();
};

extern Criticals* criticals;
