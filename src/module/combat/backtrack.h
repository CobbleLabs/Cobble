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
        // Distance gate, measured exactly like vanilla getDistanceToEntity(). The
        // defaults span the whole reachable range, so gating is opt-in.
        float minRange = 0.0f;
        float maxRange = 6.0f;
        // Hold only while an attack source is actually running (mouse held,
        // AutoClicker on, or KillAura holding a target).
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
    // AutoClicker, or KillAura while it holds a target. Used by onlyWhileAttacking.
    bool attackSourceActive() const;
    // Vanilla feet-to-feet distance against the local player, tested against the
    // configured window. Returns true when the range fields cannot be read, so a
    // missing mapping degrades to "no gate" instead of disabling the module.
    bool targetWithinRange(JNIEnv* env, jobject target) const;
    void publishIntent(const HoldIntent& intent);
    void closeAdmission();
    bool flushPending(JNIEnv* env);

    mutable std::recursive_mutex controlMutex_;
    std::atomic<std::uint64_t> intentSequence_{0};
    std::atomic<int> intentTarget_{-1};
    std::atomic<std::uint64_t> intentEpoch_{0};
    std::atomic<bool> intentActive_{false};
    std::atomic<bool> drainRequested_{false};
    std::atomic<bool> discardRequested_{false};
    std::atomic<std::uint64_t> visualEpoch_{1};
    BoxAnimation boxAnimation_;
};

extern Backtrack* backtrack;
