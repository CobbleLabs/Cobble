#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "../../sdk/core/minecraft.h"
#include "../module.h"

class Backtrack : public Module {
public:
    struct HoldIntent {
        int targetEntityId = -1;
        std::uint64_t engagementEpoch = 0;
        bool active = false;
        // Whether the retained stream may stay open at all. Separate from `active`
        // because whole-stream retention holds everyone's movement for as long as the
        // module is on, while `active` says which entity the rewind is aimed at and
        // goes false the moment you look away.
        bool streamHold = false;
    };

    struct HeldTargetSnapshot {
        bool valid = false;
        bool active = false;
        int entityId = -1;
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        std::uint64_t engagementEpoch = 0;
        std::uint64_t connectionEpoch = 0;
        std::uint64_t sequence = 0;
        std::uint32_t pendingPackets = 0;
    };

    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onRender() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;
    std::string getDisplayInfo() override;

    void onClientTick(JNIEnv* env);
    bool needsClientTick() const;

    HoldIntent holdIntent() const;
    HeldTargetSnapshot heldTargetSnapshot() const;

    struct Settings {
        // ---- Retention ----
        // How long a movement packet may sit in the queue before it is replayed.
        // This IS the backtrack window: the ghost box shows where the server still
        // thinks the target is, and everything you do against it lands there.
        int holdMs = 180;
        // Soft ceiling on retained packets. Movement arrives ~20x/second per entity,
        // so 180 ms is about four packets; anything past ~10 behaves as "no cap".
        int maxPackets = 32;

        // ---- Engagement ----
        // Hold every entity's movement instead of only the target's. A real connection
        // delays whatever happens to be in flight, so retention that switches on and off
        // with the crosshair reads as one player selectively rubber-banding rather than
        // as lag. Off restores the old behaviour: only the engaged target is withheld and
        // everybody else renders live.
        bool wholeStream = true;
        // Distance gate, measured exactly like vanilla getDistanceToEntity(). The
        // defaults span the whole reachable range, so gating is opt-in.
        float minRange = 0.0f;
        float maxRange = 6.0f;
        // Hold only while an attack source is actually running (mouse held,
        // AutoClicker on, or AimAssist steering at a target). With whole-stream on this
        // gates the rewind, not the stream: the delay stays put either way.
        bool onlyWhileAttacking = false;

        // ---- Ghost box ----
        // Deliberately just the colour. Every other knob the ghost used to expose
        // (fill, glow, corners, follow speed, line width) only ever had one correct
        // answer — the box has to read instantly at combat distance and it has to
        // trail the server position closely enough to aim at — so those are fixed in
        // backtrack.cpp and the page stays a single swatch.
        float color[4] = {0.42f, 0.92f, 1.0f, 1.0f};
    } settings;

private:
    struct BoxAnimation {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        float alpha = 0.0f;
        int entityId = -1;
        std::uint64_t engagementEpoch = 0;
        std::uint64_t connectionEpoch = 0;
        std::uint64_t visualEpoch = 0;
        bool initialized = false;
    };

    jobject targetedPlayer(JNIEnv* env) const;
    // True when something is actually swinging: the physical attack button, the
    // AutoClicker, or AimAssist while it steers at a target. Used by onlyWhileAttacking.
    bool attackSourceActive() const;
    // Vanilla feet-to-feet distance against the local player, tested against the
    // configured window. Returns true when the range fields cannot be read, so a
    // missing mapping degrades to "no gate" instead of disabling the module.
    bool targetWithinRange(JNIEnv* env, jobject target) const;
    void publishIntent(const HoldIntent& intent);
    void closeAdmission();
    // Drops the whole-stream hold as well, so the queue is allowed to run dry. Used on
    // the paths where retention must genuinely stop rather than merely lose its target.
    void closeStream();
    // Client-tick body for whole-stream retention: the hold is kept up unconditionally
    // and only the rewind target is engaged or released.
    void tickWholeStream(JNIEnv* env);
    bool flushPending(JNIEnv* env);

    mutable std::recursive_mutex controlMutex_;
    std::atomic<std::uint64_t> intentSequence_{0};
    std::atomic<int> intentTarget_{-1};
    std::atomic<std::uint64_t> intentEpoch_{0};
    std::atomic<bool> intentActive_{false};
    std::atomic<bool> intentStreamHold_{false};
    std::atomic<bool> drainRequested_{false};
    std::atomic<bool> discardRequested_{false};
    std::atomic<std::uint64_t> visualEpoch_{1};
    BoxAnimation boxAnimation_;
};

extern Backtrack* backtrack;
