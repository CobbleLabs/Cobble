#pragma once

#include <jni.h>
#include <atomic>
#include <chrono>
#include <random>
#include <mutex>
#include "../../jni/java.h"
#include "../module.h"

// Simple Timer class since util/timer.h is missing
class Timer {
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;

public:
    Timer() { Start(); }
    void Start() { m_start = std::chrono::high_resolution_clock::now(); }

    template <typename DurationT = std::chrono::milliseconds>
    float Elapsed() const {
        return (float)std::chrono::duration_cast<DurationT>(std::chrono::high_resolution_clock::now() - m_start)
            .count();
    }
};

class AimAssist : public Module {
public:
    AimAssist();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onWorldChange() override;
    void onRenderMenu() override;

    // Called from the outgoing packet gate. When packetRotation is enabled, the
    // aim jitter is kept OUT of the render rotation (smooth first-person camera)
    // and applied here to the C03 movement packet instead, so the rotation the
    // server / third-person observers see is randomized.
    void onOutgoingPacket(JNIEnv* env, jobject packet);

    // True while this module is steering the client's own rotation fields.
    //
    // Anything that writes the rotation has to declare it, because the other writers arbitrate
    // against each other rather than fighting over the same two floats: a placement, a thrown
    // potion and an aim controller all writing the same tick produce a rotation none of them
    // asked for, and the one that reads the field back afterwards treats the result as the
    // player's own mouse movement. BlockHit and Backtrack also read this claim as their "there is
    // someone in reach" signal, since this module only steers while it holds a target.
    //
    // The claim has a tail because this controller runs at ~500 Hz while the checks happen
    // on the 20 Hz client tick. An instantaneous flag would read false on most of the ticks
    // that matter, which is worse than no flag at all.
    bool isRewritingRotation() const;

    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum class TargetMode { HEALTH, DISTANCE, FOV, SWITCH };
    enum class SortBy { DISTANCE, HEALTH, FOV, HURT_TIME };

    // How the two speed settings are turned into a response rate.
    //   SMOOTH   - the speeds are used exactly as configured.
    //   ADAPTIVE - scales the rate with the current angular error: quick to close a
    //              large gap, gentle once it is already on the target.
    //   BLATANT  - ignores the low end of the sliders and snaps onto the target.
    enum class AimMode { SMOOTH = 0, ADAPTIVE = 1, BLATANT = 2 };

    // Armor tier filter: only lock onto players wearing this armor tier (on any slot)
    enum class ArmorFilter {
        ANY = 0,   // No restriction (default)
        NONE = 1,  // Target players with NO armor
        LEATHER = 2,
        CHAINMAIL = 3,
        IRON = 4,
        GOLD = 5,
        DIAMOND = 6,
    };

    // Multi-select armor filter: a bitmask of tiers to target. A player matches
    // if ANY of their armor pieces is one of the checked tiers (ARMOR_NO_ARMOR
    // matches players wearing nothing). 0 = no restriction (target everyone).
    enum ArmorTierBit {
        ARMOR_NO_ARMOR = 1 << 0,  // players wearing NO armor
        ARMOR_LEATHER = 1 << 1,
        ARMOR_CHAIN = 1 << 2,
        ARMOR_IRON = 1 << 3,
        ARMOR_GOLD = 1 << 4,
        ARMOR_DIAMOND = 1 << 5,
    };

    struct Settings {
        bool invisibles = false;
        bool alwaysAim = false;
        bool breakBlocks = false;
        bool aimAtClosestPoint = false;
        bool weaponOnly = false;
        bool stopOnHitbox = false;  // Stops aim assist when crosshair is inside hitbox
        // Bot filtering moved out to the AntiBot module (src/module/combat/antibot.h) so one
        // switch governs every targeting module instead of each keeping its own idea of a bot.

        AimMode aimMode = AimMode::SMOOTH;
        TargetMode targetMode = TargetMode::DISTANCE;
        SortBy switchSortBy = SortBy::DISTANCE;  // Only used when targetMode is SWITCH
        bool hurtTimeLock = false;               // Instant aim when switching targets in Hurt Time mode
        int fov = 180;
        float distance = 5.0f;

        // Hard lock. Once a target is acquired it is held across button releases, wins
        // over every target-selection rule including Switch, keeps extra reach, and
        // ignores Stop On Hitbox. Releases only when the target dies, leaves the world
        // or goes genuinely out of range.
        bool lockIn = false;

        // How hard the aim pulls on each axis, 1..100, mapped linearly onto the
        // controller's response rate.
        float horizontalSpeed = 50.0f;
        float verticalSpeed = 50.0f;

        // Yaw only. Hides Vertical Speed rather than leaving a slider that does nothing.
        bool horizontalOnly = false;

        float jitterPercent = 0.0f;
        float prediction = 0.0f;      // 0-100%: lead the aim by the target's velocity
        bool packetRotation = false;  // Smooth camera, jitter only on the sent packet

        ArmorFilter armorFilter = ArmorFilter::ANY;  // Legacy single-select (kept for config migration)
        int armorTierMask = 0;                       // Multi-select bitmask of ArmorTierBit (0 = no restriction)
        bool armorFilterEnabled = true;              // Master toggle for the armor filter (key-toggleable)
        int armorFilterKeybind = 0;                  // Virtual key to toggle armorFilterEnabled (0 = none)
    } settings;

private:
    void releaseCachedTarget(JNIEnv* env);
    void resetTargetState(JNIEnv* env);
    void updateSimulatedJitter();
    // Every early-out (menu open, mouse released, no target, crosshair already
    // inside the hitbox) must leave the controller timer fresh. applyAimController
    // scales its step by the elapsed time, so a dt inflated by an idle stretch
    // turns the next update into one large catch-up snap instead of a step.
    void markAimIdle() { controllerTimer.Start(); }

    bool aimsPitch() const { return !settings.horizontalOnly && settings.verticalSpeed > 0.0f; }

    // How far past the configured reach an already-acquired target is still held.
    double retentionSlack() const;

    // Stamped every time a rotation write actually lands on the player. Read from the client
    // tick and from the outgoing packet gate, hence the atomic.
    void markRotationWrite();
    std::atomic<unsigned long long> lastRotWriteMs_{0};

    // Weapon check
    bool isHoldingWeapon();

    // Math utilities
    float wrapTo180(float value);
    std::pair<float, float> getAngles(double x1, double y1, double z1, double x2, double y2, double z2);
    float randFloat(float min, float max);
    int randInt(int min, int max);

    // Target finding - returns true if target found, fills position
    bool findTarget(double& targetX, double& targetY, double& targetZ);

    // Reads position and hitbox straight off the cached entity and applies the
    // retention rules. The player-list scan only ever *picks* a target; this is
    // what *keeps* one, so a scan that misses for a frame cannot break the lock.
    bool readCachedTarget(double& targetX, double& targetY, double& targetZ);

    // Single time-based controller used by both axes. Unlike the legacy stack of
    // reaction ramps/accumulators, this consumes one error vector once per update
    // and is independent of the 2 ms module-loop cadence.
    void applyAimController(float yawDiff, float pitchDiff);

    // Resolves the aim point inside the target hitbox and publishes resolvedBox_.
    void getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ);
    void resolveAimHorizontal(double minX, double maxX, double minZ, double maxZ, double localX, double localZ,
                              double& aimX, double& aimZ);

    // Whether this version needs the head-tracking and jump-offset machinery. Only 1.7.10
    // does: its remote bounding box arrives a yOffset too low, so the plain box centre lands
    // on the target's legs. Every other version aims at the centre like it always did, which
    // is the behaviour that keeps the camera from riding the target up and down.
    bool usesJumpAimLogic() const { return Java::Version == MinecraftVersion::MC_1_7_10; }

    // Eases aimHeightBlend_ toward the chest while the target is airborne and back to
    // the head once it lands. Called once per update, before the aim point is resolved.
    void updateAimHeightBlend(JNIEnv* env, double targetFeetY);

    // Whether the target is off the ground.
    //
    // 1.7.10's entity move packets carry no ground flag -- that arrived in 1.8 -- so
    // Entity.onGround is never synced for a remote player there and every target reads
    // as airborne forever. On that version the answer is derived from the hitbox
    // instead: a standing or walking player holds one feet height, a jumping or falling
    // one does not. Every other version keeps reading the field, which is accurate.
    bool resolveTargetAirborne(JNIEnv* env, double targetFeetY);
    double lastFeetY_ = 0.0;
    bool hasLastFeetY_ = false;
    Timer feetMovingTimer_;

    // Resolves how far Entity.posY sits above an entity's feet. Zero on every version
    // except 1.7.10, which is the only one this correction touches at all.
    //
    // 1.7.10 still carries Entity.yOffset and EntityPlayer sets it to 1.62, so posY is
    // the EYE height there while the bounding box bottom is the feet. 1.8 dropped
    // yOffset and posY became the feet, so the two agree. Every vertical in this module
    // is feet-space, and leaving the difference in made the eye origin and the hitbox
    // sanity check each carry their own 1.62 error -- they cancelled on 1.8 and did not
    // on 1.7.10, which is what put the aim a body-length low there.
    //
    // The 1.62 is confirmed against the local player's own box rather than assumed, so a
    // 1.7.10 client that already normalises posY is left alone too.
    void measurePosYFeetDelta(JNIEnv* env);
    double posYFeetDelta_ = 0.0;
    bool posYFeetDeltaKnown_ = false;

    // Throttles the vertical-solve trace so the console stays readable: this module runs
    // at ~500 Hz and an unthrottled line per update is unreadable and slow.
    Timer verticalTraceTimer_;
    int targetOnGroundRaw_ = -1;  // -1 = Entity.onGround unavailable

    // The AABB getAimPoint actually resolved this update, after its detached-box repair.
    // Everything that offsets the aim afterwards -- the prediction lead, the packet jitter --
    // is bounded against this box, because a rotation that leaves it is a rotation the
    // server raytraces into empty air.
    struct AimBox {
        double minX = 0.0, minY = 0.0, minZ = 0.0;
        double maxX = 0.0, maxY = 0.0, maxZ = 0.0;
    } resolvedBox_;

    // Mouse + smoothing state
    // Preserve sub-GCD movement so fast tracking never quantizes to zero for
    // several ticks and feels stuck.
    double yawRotationResidual = 0.0;
    double pitchRotationResidual = 0.0;
    float cachedMouseSensitivity = 0.5f;

    bool targetHasHitbox = false;
    float targetHitboxMinX = 0.0f;
    float targetHitboxMinY = 0.0f;
    float targetHitboxMinZ = 0.0f;
    float targetHitboxMaxX = 0.0f;
    float targetHitboxMaxY = 0.0f;
    float targetHitboxMaxZ = 0.0f;
    bool armorFilterKeyPrev_ = false;  // Edge-detection state for armor filter keybind
    bool targetChangedThisFrame = false;

    // 0 = aim on the head, 1 = aim on the airborne torso point. Eased rather than
    // switched, because the two points are a fair fraction of a block apart and
    // stepping between them would be a jump the player sees and an anti-cheat measures.
    float aimHeightBlend_ = 0.0f;
    Timer aimHeightTimer_;
    bool targetAirborne_ = false;

    // Multiplier on the vertical speed setting, 1.0 on the ground. Only updateAimHeightBlend
    // writes it and only 1.7.10 calls that, so every other version keeps a flat 1.0.
    float verticalDamping_ = 1.0f;

    // Smoothed local airborne state, kept apart from aimHeightBlend_ because a local jump
    // should soften pitch fully while moving the aim point only slightly.
    float localAirBlend_ = 0.0f;

    // Feet height of the target the last time it was on the ground. Mid-air this lags
    // the rising hitbox by the displacement of the jump, and the aim point is measured
    // from a partial step back toward it, so the crosshair follows a jump without
    // tracking all of it.
    double groundFeetY_ = 0.0;
    bool hasGroundAnchor_ = false;
    Timer simulatedJitterTimer;
    float simulatedJitterYaw = 0.0f;
    float simulatedJitterPitch = 0.0f;
    float simulatedJitterTargetYaw = 0.0f;
    float simulatedJitterTargetPitch = 0.0f;
    int simulatedJitterIntervalMs = 30;
    // Jitter is an OFFSET from the tracked rotation, not a rotation speed.
    // applyAimController consumes per-iteration steps, so adding the raw offset
    // on every loop (~2 ms / ~500 Hz) integrated it into a runaway drift of up to
    // ~200 deg/s, which the tracking error then fought back - the random up/down
    // and sideways flicking. These hold the offset already folded into the
    // rotation so only its change is ever injected as a step.
    float appliedJitterYaw_ = 0.0f;
    float appliedJitterPitch_ = 0.0f;

    Timer controllerTimer;

    // Cached Target for hybrid tracking
    jobject cachedTarget = nullptr;
    int cachedTargetMissingScans_ = 0;
    std::atomic<bool> targetResetRequested_{false};

    // Target velocity estimate (blocks/sec, smoothed) for prediction.
    double predPrevX = 0.0, predPrevY = 0.0, predPrevZ = 0.0;
    double predVelX = 0.0, predVelY = 0.0, predVelZ = 0.0;
    Timer predTimer;

    std::mt19937 rng;
    mutable std::recursive_mutex targetMutex_;

    // Packet-rotation (smooth camera) state. Written on the module thread,
    // read on the network thread from onOutgoingPacket.
    std::atomic<bool> hasServerRotation_{false};
    std::atomic<float> serverYawOffset_{0.0f};
    std::atomic<float> serverPitchOffset_{0.0f};

    // How far, in degrees, the sent rotation may sit off the aim point before the
    // server's raytrace leaves the hitbox. Derived from the target's angular size at
    // its current distance, so the packet jitter shrinks as the target gets smaller
    // on screen instead of being a fixed amplitude that is fine at 2 blocks and a
    // miss at 6. Zero means "no target measured" and suppresses the offset entirely.
    std::atomic<float> packetYawBudget_{0.0f};
    std::atomic<float> packetPitchBudget_{0.0f};
};

extern AimAssist* aimAssist;
