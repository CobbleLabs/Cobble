#pragma once

#include <jni.h>
#include <atomic>
#include <chrono>
#include <random>
#include "../module.h"

struct PlayerInfo;

// AutoRod performs one bounded rod -> hook -> weapon -> hit sequence per attack
// signal. It never loops while the mouse is held, never changes slots after the player
// manually selected another item, and restores the original slot on teardown.
//
// Every game-state mutation happens on the CLIENT thread, one phase per tick, from
// onClientTick(). The ~500 Hz module loop only latches the attack intent.
//
// Tick alignment is not cosmetic here. A hotbar change reaches the server only through
// PlayerControllerMP.syncCurrentPlayItem(), which runs once per tick near the top of
// Minecraft.runTick(). A sequence that selects the rod and switches back inside one
// tick therefore emits NO C09PacketHeldItemChange for the rod at all: the server
// receives a use-item for an item that was never in the player's hand, which is an
// unambiguous flag rather than a heuristic one. Spacing each phase by a real tick
// boundary is what guarantees the matching C09 is on the wire before the action that
// depends on it.
//
// WHY THE ROD HAS TO STAY IN HAND
// -------------------------------
// The cast is not the play; the hook arriving is. EntityFishHook.onUpdate opens with
//
//     if (this.angler.getHeldItem() == null ||
//         this.angler.getHeldItem().getItem() != Items.fishing_rod || ...)
//     { this.setDead(); this.angler.fishEntity = null; }
//
// so the server destroys the bobber on the first tick the angler is not holding a rod.
// The hook leaves at 1.5 blocks/tick and loses 8% of its speed every tick, which is
// four ticks to cover five blocks. Any sequence that swaps back to the sword before
// then has thrown the hook away in flight: no knockback, no sprint reset, nothing on
// the other player at all — just a C09/C08/C09 burst and a melee swing spent on
// holding the wrong item. That is what this module used to do on every trigger.
//
// So the rod is held for an estimated flight time derived from the target's distance,
// and the swap back happens after the hook has had time to land. Holding the rod costs
// melee time, which is exactly why the trigger below refuses to fire inside sword range.
//
// WHEN A ROD IS THE RIGHT PLAY
// ----------------------------
// Out of reach, and not much else. Inside melee range a swing is worth more than a
// knockback, and the module previously required objectMouseOver to be an entity — a
// raytrace bounded by the block-reach distance — so it could ONLY fire in the one
// situation where rodding is a mistake. The trigger is now a real distance band, and
// because this module does not rotate (KillAura owns the view), it also refuses to cast
// unless the crosshair is already close enough to the target for the hook to arrive.
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
        // The distance band a rod is worth casting in. Below the minimum a sword hit is
        // strictly better and the rod would be costing you the swing; above the maximum
        // the hook's own gravity drops it under the hitbox before it arrives.
        float minDistance = 3.6f;
        float maxDistance = 9.0f;
        int cooldownMs = 1600;   // minimum spacing between sequences
        int jitterPercent = 35;  // +-% spread applied to every delay
    } settings;

private:
    // Select rod -> cast -> hold while the hook flies -> swap back -> hit.
    enum class Phase { Idle, RodSelected, HookFlying, WeaponSelected };

    // ---- Client-thread state. ----
    Phase phase_ = Phase::Idle;
    int waitTicks_ = 0;
    int rodSlot_ = -1;
    int weaponSlot_ = -1;
    int targetId_ = 0;  // who the cast was aimed at, so the flight can be cut short
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

    // Ticks for the hook to cover `distance`, from its own 1.5 b/t launch and 0.92
    // per-tick drag, plus slack for the spawn tick and the trip back.
    static int hookFlightTicks(double distance);
    // Eye-to-target distance and the angle between the current view and the aim point,
    // for one candidate. Returns false when the candidate cannot be measured.
    bool measure(const PlayerInfo& target, double& distanceOut, double& angleOut) const;
    // The best rod target in the band right now, or entityId 0 when there is none.
    int pickTarget(double& distanceOut) const;
    // Distance to a live player by entity id; negative when they are gone.
    double distanceToId(int entityId) const;
};

extern AutoRod* autoRod;
