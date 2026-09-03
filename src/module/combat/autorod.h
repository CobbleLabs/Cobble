#pragma once

#include <jni.h>
#include <atomic>
#include <chrono>
#include <random>
#include "../module.h"

// AutoRod performs one bounded rod -> weapon -> hit sequence per attack signal.
// It never loops while the mouse is held, never changes slots after the player
// manually selected another item, and restores the original slot on teardown.
//
// Every game-state mutation happens on the CLIENT thread, one phase per tick,
// from onClientTick(). The ~500 Hz module loop only latches the attack intent.
//
// Tick alignment is not cosmetic here. A hotbar change reaches the server only
// through PlayerControllerMP.syncCurrentPlayItem(), which runs once per tick near
// the top of Minecraft.runTick(). A sequence that selects the rod and switches
// back inside one tick therefore emits NO C09PacketHeldItemChange for the rod at
// all: the server receives a use-item for an item that was never in the player's
// hand, which is an unambiguous flag rather than a heuristic one. Spacing each
// phase by a real tick boundary is what guarantees the matching C09 is on the
// wire before the action that depends on it.
class AutoRod : public Module {
public:
    AutoRod();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    bool isSequencing() const { return phase_ != Phase::Idle; }
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Client thread, pre-motion, once per tick (from the SlowDownGate hook).
    void onClientTick(JNIEnv* env);
    bool needsClientTick() const { return enabled; }

    struct Settings {
        bool onlyWhileAttacking = true;
        bool requireWeapon = true;
        bool autoHit = true;
        int rodHoldTicks = 3;    // ticks the rod stays in hand after the cast
        int hitDelayTicks = 2;   // ticks after the swap back before the follow-up hit
        int cooldownMs = 1600;   // minimum spacing between sequences
        int jitterPercent = 35;  // +-% spread applied to every delay above
    } settings;

private:
    // Select rod -> cast -> swap back -> hit, one transition per onClientTick().
    enum class Phase { Idle, RodSelected, RodCast, WeaponSelected };

    // ---- Client-thread state. ----
    Phase phase_ = Phase::Idle;
    int waitTicks_ = 0;
    int rodSlot_ = -1;
    int weaponSlot_ = -1;
    std::chrono::steady_clock::time_point lastSequence_{};
    int cooldownMsThisRound_ = 0;
    std::mt19937 rng_;

    // ---- Module-thread state (attack-intent latch only). ----
    bool prevAttackDown_ = false;
    std::chrono::steady_clock::time_point observedAutoClick_{};

    // Hand-off: set on the module thread, consumed on the client thread.
    std::atomic<bool> requestPending_{false};

    static bool isWeapon(int itemId);
    int findRodSlot() const;
    int jitterTicks(int baseTicks);
    int rollCooldownMs();
    void abortSequence(bool restoreSlot);
};

extern AutoRod* autoRod;
