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

    // Critical technique.
    //   Packet   - spoof the airborne state with no real movement (1 tick).
    //   MiniJump - small real hop, smooth and consistent.
    //   Jump     - full vanilla jump height (classic manual critical).
    //   NoGround - hold the onGround spoof for several ticks (basic/legacy AC).
    //   Hop      - very low real hop, minimal displacement.
    //   Smooth   - hop up then cancel next tick, near-zero net movement.
    //   NCP      - full jump gated strictly to on-ground (legit jump-crit).
    enum class Mode : int {
        Packet = 0,
        MiniJump = 1,
        Jump = 2,
        NoGround = 3,
        Hop = 4,
        Smooth = 5,
        NCP = 6,
    };

    struct Settings {
        Mode mode = Mode::Packet;       // Critical technique
        int maxDelayMs = 100;           // Delay between criticals (50-500ms)
        int chance = 80;                // Chance to do critical (0-100%)
        bool onlyWhileAttacking = true; // Only apply when LMB pressed
        bool lookingAtPlayer = true;    // Only apply when looking at a player
        bool onlyWithSword = false;     // Only apply when holding sword
    } settings;

private:
    unsigned long lastCriticalTime = 0;
    bool wasAttacking = false;

    // Multi-tick sequence state (advanced by onUpdate each tick).
    int groundSpoofTicks = 0; // NoGround: ticks left holding onGround = false
    bool smoothCancel = false; // Smooth: cancel the upward hop next tick

    void doPacketCritical();
    void doJumpCritical(double height);
    void startCritical();
    void advanceSequence();

    bool isHoldingSword();
    bool isLookingAtPlayer();
    bool canDoCritical();
    bool checkChance();
};

extern Criticals* criticals;
