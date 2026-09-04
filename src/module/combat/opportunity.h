#pragma once

#include <jni.h>

#include "../module.h"

#include <atomic>
#include <cstdint>
#include <random>
#include <string>

// Opportunity — watches the nearest enemy and, when one of the plays below is actually
// on, takes it: a cobweb in their feet, a lava bucket under them, flint and steel on the
// block they are standing on, or a water bucket to wash them off it.
//
// Every play reduces to the same mechanic -- "put item X in the block cell the enemy is
// standing in" -- so there is one execution engine and four independent triggers, each
// with its own range window, health gate, chance and cooldown.
//
// WHY IT DOES NOT FLAG
// --------------------
// Nothing here is synthetic. The module moves the REAL rotation on the client tick,
// switches the hotbar with a real vanilla slot change, and then calls the same
// rightClickMouse() a held mouse button would, with vanilla's rightClickDelayTimer left
// alone. The server sees a rotation that the client is genuinely using, a C09, and a C08
// whose hit vector matches the raytrace -- in that order, because the placement is never
// fired on the same tick the flick finishes (see the dwell rule in onClientTick).
//
// WHAT MAKES IT HUMAN
// -------------------
// A bot that webs the enemy 40 ms after they enter range, every single time, from any
// angle, is obvious to a spectator long before it is obvious to an anticheat. So:
//   * a randomized reaction delay per opportunity, drawn from a triangular
//     distribution -- never the same, never on the bounds;
//   * the flick is spread over several ticks on a smoothstep curve with per-tick noise
//     and a slight overshoot, then a correction;
//   * a per-play chance to simply not take it, and a per-play cooldown so a taken
//     opportunity is not immediately retaken;
//   * a global cooldown so two different plays cannot fire back to back;
//   * line of sight is required -- it will not place through a wall it cannot see past;
//   * self-preservation: lava and fire are refused when the cell is close enough to
//     burn us too, which is also exactly why a human would not take that shot.
class Opportunity : public Module {
public:
    // Order is load-bearing: it indexes settings.opp[] and kOppItems.
    enum class Play : int { Cobweb = 0, Lava = 1, Fire = 2, Water = 3, Count = 4 };
    static constexpr int kPlayCount = static_cast<int>(Play::Count);

    struct PlaySettings {
        bool enabled = true;
        float minDist = 1.0f;
        float maxDist = 4.0f;
        float maxTargetHealth = 20.0f;  // take it only while they are at or below this
        int chance = 100;               // % of opportunities actually taken
        int cooldownMs = 1500;          // per-play cooldown after a successful placement
    };

    struct Settings {
        PlaySettings play[kPlayCount];

        float reach = 4.2f;          // max eye-to-hit-point distance we will use
        float selfSafeDist = 2.5f;   // refuse lava/fire nearer to us than this
        bool requireLineOfSight = true;
        bool ignoreTeam = true;      // never target our own team
        bool ignoreInvisible = true;

        int globalCooldownMs = 700;  // floor between any two plays

        // ---- Humanization ----
        int reactMinMs = 140;
        int reactMaxMs = 320;
        int aimTicks = 3;
        float aimNoise = 0.7f;
        bool overshoot = true;
        bool snapBack = true;
        bool restoreSlot = true;
    } settings;

    Opportunity();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Client thread, pre-motion, once per tick (from the SlowDownGate hook). Trigger
    // evaluation, the aim curve and the placement all live here.
    void onClientTick(JNIEnv* env);
    bool needsClientTick() const {
        return enabled || owningRotation_.load(std::memory_order_acquire);
    }

    // True while we are driving the player's rotation, so the aim modules defer.
    bool isRewritingRotation() const { return owningRotation_.load(std::memory_order_acquire); }

    static const char* playName(Play play);

private:
    struct Shot {
        Play play = Play::Cobweb;
        int entityId = 0;  // who this play is for, so it can be re-pointed as they move
        int cellX = 0;  // the block cell the item goes into
        int cellY = 0;
        int cellZ = 0;
        int supportX = 0;  // the face we right-click to get it there
        int supportY = 0;
        int supportZ = 0;
        double hitX = 0.0;
        double hitY = 0.0;
        double hitZ = 0.0;
        int slot = -1;
        bool valid = false;
    };

    enum class Phase { Idle, React, Aim, Place, Settle };

    static std::uint64_t nowMs();
    static float wrapDegrees(float angle);
    static float smoothstep(float t);

    // Find a clickable face that puts a block into `cell`, within reach and in sight.
    bool anchorFor(int cellX, int cellY, int cellZ, Shot& out) const;
    int findItemSlot(Play play) const;
    // Is `play` on right now, and if so where. Fills `out` and returns true.
    bool evaluate(Shot& out);
    // Re-point an existing shot at where its target is NOW. Rolls nothing and spends no
    // cooldown: a reaction delay is not an excuse to web the spot they just left.
    bool refreshShot(Shot& shot) const;
    bool stillValid(const Shot& shot) const;
    bool hasLineOfSight(double toX, double toY, double toZ) const;

    void aimAt(const Shot& shot, bool freshOpportunity);
    void beginSettle();
    void abort(JNIEnv* env);
    void restoreSlot();
    void applyRotation(float yaw, float pitch);
    int rollReactionMs();
    bool rollChance(int percent);

    // ---- Client-thread state (touched only inside onClientTick). ----
    Phase phase_ = Phase::Idle;
    Shot shot_;
    int originalSlot_ = -1;
    // The hotbar as it was at the top of this client tick, so a pass that considers all
    // four plays costs nine JNI reads instead of eighty.
    int hotbar_[9] = {};
    int heldSlot_ = -1;
    std::uint64_t reactDeadlineMs_ = 0;
    std::uint64_t restoreSlotAtMs_ = 0;
    std::uint64_t nextAnyPlayMs_ = 0;
    std::uint64_t nextPlayMs_[kPlayCount] = {};
    std::uint64_t declinedUntilMs_[kPlayCount] = {};
    int aimTick_ = 0;
    int aimTotalTicks_ = 0;
    float aimFromYaw_ = 0.0f;
    float aimFromPitch_ = 0.0f;
    float aimToYaw_ = 0.0f;
    float aimToPitch_ = 0.0f;
    float returnYaw_ = 0.0f;
    float returnPitch_ = 0.0f;
    bool haveReturnView_ = false;
    std::mt19937 rng_{std::random_device{}()};

    std::atomic<bool> owningRotation_{false};
    std::atomic<const char*> status_{nullptr};

    // Which play the settings page is currently editing. UI state only.
    int editPlay_ = 0;
};

extern Opportunity* opportunity;
