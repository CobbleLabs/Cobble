#pragma once

#include <jni.h>

#include "../module.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <vector>

// Placement engine shared by two modules:
//   Scaffold - bridges blocks under the spot just ahead of where you look. LOCKED:
//              a bridge is a sustained placement stream and this engine is built
//              around the occasional save, so it stays behind the developer gate.
//   Clutch   - places a block under you when your predicted path is about to drop
//              you off the map (an emergency save).
//
// HOW THIS AVOIDS FLAGGING
// ------------------------
// The previous engine did silent aim from the packet thread: it wrote the player's
// rotation, raytraced, right-clicked and restored the view all inside the send of an
// outgoing flying packet, forced `rotating = true`, zeroed rightClickDelayTimer, and
// added a +-1-GCD pitch flip on every single packet. Each of those is individually
// detectable, and the pitch flip in particular is a period-2 oscillation of exactly
// one sensitivity grid step -- no mouse produces that, ever.
//
// This engine does the opposite: it moves the REAL rotation, on the client tick, and
// lets vanilla do everything else. There is no discrepancy between what the client
// renders, what the player sees and what the server receives, because they are the
// same rotation. The only thing left to detect is the rotation itself, so the rotation
// is made human:
//   * a randomized reaction delay before the head starts to move at all;
//   * the flick is spread over several ticks on a smoothstep curve, with per-tick
//     noise, so no two deltas repeat and none of them is a teleport;
//   * an optional slight overshoot followed by a correction, which is what a real
//     flick to a target looks like;
//   * every rotation quantized onto the client's own mouse GCD grid, so each delta
//     is one a real mouse could physically have produced;
//   * vanilla's rightClickDelayTimer is respected, never zeroed, so the placement
//     rate is capped where a held right-click caps it;
//   * the hotbar switch is a real vanilla slot change (C09) taken while the camera is
//     still moving, and the switch back happens after a delay, not instantly.
//
// The trade is that the camera visibly moves. For a clutch that is correct: the user
// is saving themselves and expects the view to snap down, exactly as it would if they
// did it by hand. Snap Back then flicks the view to where it came from, which is the
// second half of what a human does.
enum class PlaceMode { Scaffold = 0, Clutch = 1 };

class Clutch : public Module {
public:
    struct Settings {
        float scaffoldLead = 0.9f;  // Scaffold: blocks ahead (along your look) to place

        int predictionMs = 400;     // Clutch: how far ahead to simulate the fall
        int searchRadius = 3;       // anchor search radius, in rings
        int safeDepth = 4;          // no floor within this many blocks below == danger
        bool allowForward = false;  // also save while on ground walking off a ledge

        float reach = 4.2f;

        // ---- Humanization ----
        int reactMinMs = 110;    // reaction delay before the head starts moving
        int reactMaxMs = 240;
        int aimTicks = 3;        // ticks the flick is spread over
        float aimNoise = 0.6f;   // per-tick rotation noise, in degrees
        bool overshoot = true;   // overshoot slightly then correct, like a real flick
        bool snapBack = true;    // flick the view back where it came from afterwards
        bool restoreSlot = true;
        int missChance = 0;      // % of opportunities to ignore outright

        bool cameraWeight = true;  // prefer anchors close to the current view

        // ---- HUD ----
        bool showBps = true;
        bool showPath = true;
        bool showPlacement = true;
    } settings;

    explicit Clutch(PlaceMode mode);
    ~Clutch() override;

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Client thread, pre-motion, once per tick (from the SlowDownGate hook). Danger
    // detection, anchor search, the aim curve and the placement all live here, so
    // every timing decision is in whole ticks and every JNI call is on the game
    // thread at 20 Hz instead of on the module loop at ~500 Hz.
    void onClientTick(JNIEnv* env);
    // Stays true while a sequence is unwinding so a disable can always settle it.
    bool needsClientTick() const {
        return enabled || owningRotation_.load(std::memory_order_acquire);
    }

    // True while we are driving the player's rotation, so the aim modules defer
    // instead of fighting the flick.
    bool isRewritingRotation() const { return owningRotation_.load(std::memory_order_acquire); }

private:
    struct Placement {
        int targetX = 0;
        int targetY = 0;
        int targetZ = 0;
        int supportX = 0;
        int supportY = 0;
        int supportZ = 0;
        double hitX = 0.0;
        double hitY = 0.0;
        double hitZ = 0.0;
        double score = 0.0;
        bool valid = false;
    };

    struct PathPoint {
        double relX = 0.0;
        double relY = 0.0;
        double relZ = 0.0;
        bool hitBlock = false;
    };

    // Idle -> React (reaction delay) -> Aim (the flick) -> Place (respect vanilla
    // cadence, fire) -> Settle (flick back, restore slot) -> Idle. A continuous run
    // loops Place -> Aim without paying the reaction delay again; it is only charged
    // on entry from Idle, because a human reacts to the situation once, not to every
    // block in it.
    enum class Phase { Idle, React, Aim, Place, Settle };

    static std::uint64_t nowMs();
    static bool isPlaceableBlock(int id);
    static float wrapDegrees(float angle);
    static float smoothstep(float t);

    int findBlockSlot() const;
    bool isDangerous(double predictedX, double predictedY, double predictedZ) const;
    bool isPlacementUsable(const Placement& placement) const;
    Placement findPlacement(double predictedX, double predictedY, double predictedZ) const;
    // Where this variant wants a block, and whether it wants one at all.
    bool wantsPlacement(Placement& out) const;

    void aimAt(const Placement& p, bool freshOpportunity);
    void beginSettle();
    void abort(JNIEnv* env);
    void restoreSlot();
    void publishPreview(const Placement& p);

    // Rotation the server should see this tick: quantized onto the mouse GCD grid
    // relative to where we are now, so the delta is mouse-producible.
    void applyRotation(float yaw, float pitch);

    int rollReactionMs();
    bool rollMiss();

    const PlaceMode placeMode_;
    bool isClutch() const { return placeMode_ == PlaceMode::Clutch; }
    bool isScaffold() const { return placeMode_ == PlaceMode::Scaffold; }

    // ---- Client-thread state (touched only inside onClientTick). ----
    Phase phase_ = Phase::Idle;
    Placement target_;
    int slot_ = -1;
    int originalSlot_ = -1;
    std::uint64_t reactDeadlineMs_ = 0;
    std::uint64_t restoreSlotAtMs_ = 0;
    std::uint64_t nextPlaceMs_ = 0;
    int aimTick_ = 0;
    int aimTotalTicks_ = 0;
    float aimFromYaw_ = 0.0f;
    float aimFromPitch_ = 0.0f;
    float aimToYaw_ = 0.0f;
    float aimToPitch_ = 0.0f;
    float returnYaw_ = 0.0f;
    float returnPitch_ = 0.0f;
    bool haveReturnView_ = false;
    int placesThisRun_ = 0;
    std::mt19937 rng_{std::random_device{}()};

    // True while the phase machine owns the player's rotation. Published for
    // isRewritingRotation()/needsClientTick(), which run on other threads.
    std::atomic<bool> owningRotation_{false};
    std::atomic<const char*> status_{nullptr};

    // ---- Render-thread caches (module thread writes, render thread reads). ----
    mutable std::mutex pathMutex_;
    std::vector<PathPoint> cachedPath_;
    Placement cachedPlacement_;
    std::uint64_t nextPathMs_ = 0;

    double cachedLocalX_ = 0.0;
    double cachedLocalY_ = 0.0;
    double cachedLocalZ_ = 0.0;
    double cachedLocalLastX_ = 0.0;
    double cachedLocalLastY_ = 0.0;
    double cachedLocalLastZ_ = 0.0;
    double cachedRenderX_ = 0.0;
    double cachedRenderY_ = 0.0;
    double cachedRenderZ_ = 0.0;
    double cachedMotionX_ = 0.0;
    double cachedMotionY_ = 0.0;
    double cachedMotionZ_ = 0.0;
    bool cachedPlayerValid_ = false;
    float cachedPartialTicks_ = 0.0f;
    long long lastUpdateTick_ = 0;
};

// Two instances: the Scaffold module and its Clutch fork.
extern Clutch* scaffold;
extern Clutch* clutch;
