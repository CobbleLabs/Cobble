#pragma once

#include <jni.h>
#include <atomic>
#include <chrono>
#include <random>
#include <mutex>
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

// ============================================================
// AimBoost - Speed multiplier randomization
// ============================================================
class AimBoost {
public:
    AimBoost(float speed_mult_min, float speed_mult_max, float freq_min_ms, float freq_max_ms, bool continuous);
    AimBoost() = default;

    float speed_mult_min = 0.5f;
    float speed_mult_max = 1.8f;
    uint32_t frequency_min_ms = 400;
    uint32_t frequency_max_ms = 700;
    uint32_t frequency_ms = 500;
    bool continuous = true;

    float Delay();

private:
    float prev_speed_mult = 1.f;
    float speed_mult = 1.f;
    Timer timer;
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
    void setEntityLock(bool enabled);
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum class TargetMode { HEALTH, DISTANCE, FOV, SWITCH };
    enum class SortBy { DISTANCE, HEALTH, FOV, HURT_TIME };

    // Aim application style:
    //   SMOOTH   - humanized, speed-limited tracking (the original behaviour).
    //   ADAPTIVE - auto-tunes the speed each tick from the angle error, target
    //              distance and target velocity (fast to catch up, slow for
    //              precision when on target).
    //   BLATANT  - instant hard lock: sets the exact rotation onto the target
    //              every tick (obvious aimbot look, moves the real camera).
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

    // Aim bone: named point inside the hitbox expressed as (x,y,z) ratios of
    // the AABB (0 = min side, 1 = max side). Aim always resolves to a bone;
    // adding a new one is a single entry in kBones[].
    struct Bone {
        const char* name;
        float xRatio;
        float yRatio;
        float zRatio;
    };
    static constexpr Bone kBones[] = {
        {"Center", 0.5f, 0.5f, 0.5f},
    };
    static constexpr int kBoneCount = static_cast<int>(sizeof(kBones) / sizeof(kBones[0]));

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
        bool horizontalOnly = false;  // false = aim yaw AND pitch (vertical works)
        bool invisibles = false;
        bool alwaysAim = false;
        bool breakBlocks = false;
        bool aimAtClosestPoint = false;
        bool entityLock = false;  // Keep the acquired entity until it becomes invalid
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
        float speed = 50.0f;
        float jitterPercent = 0.0f;
        float prediction = 0.0f;      // 0-100%: lead the aim by the target's velocity
        bool packetRotation = false;  // Smooth camera, jitter only on the sent packet
        int reactionTime = 50;        // ms

        int aimBone = 0;  // Index into kBones[]; 0 = Center

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
    float slerp(float start, float end, float t);

    // Target finding - returns true if target found, fills position
    bool findTarget(double& targetX, double& targetY, double& targetZ);

    // Single time-based controller used by every aim mode. Unlike the legacy
    // stack of reaction ramps/accumulators, this consumes one error vector once
    // per update and is independent of the 2 ms module-loop cadence.
    void applyAimController(float yawDiff, float pitchDiff);

    // Get aim point (handles closest point vs center)
    bool getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ);

    // The AABB getAimPoint actually resolved this update, after its detached-box repair.
    // Everything that offsets the aim afterwards -- the prediction lead, the packet jitter --
    // is bounded against this box, because a rotation that leaves it is a rotation the
    // server raytraces into empty air.
    struct AimBox {
        double minX = 0.0, minY = 0.0, minZ = 0.0;
        double maxX = 0.0, maxY = 0.0, maxZ = 0.0;
        bool valid = false;
    } resolvedBox_;

    // Boost systems
    AimBoost yawBoost{0.9f, 1.1f, 450, 600, true};
    AimBoost yawBoost2{1.0f, 1.3f, 800, 1000, false};
    AimBoost pitchBoost{0.8f, 1.2f, 450, 600, true};
    AimBoost pitchBoost2{0.7f, 1.3f, 600, 900, false};

    // Mouse + smoothing state
    // Preserve sub-GCD movement so fast tracking never quantizes to zero for
    // several ticks and feels stuck.
    double yawRotationResidual = 0.0;
    double pitchRotationResidual = 0.0;
    double cursorAccumY = 0.0;
    float cachedMouseSensitivity = 0.5f;

    float pitchdiffSpeed = 0.f;
    float yawdiffSpeed = 0.f;
    float targetYawSpeed = 0.f;
    float targetPitchSpeed = 0.f;
    bool reactionPrimed = false;
    bool targetHasHitbox = false;
    float targetHitboxMinX = 0.0f;
    float targetHitboxMinY = 0.0f;
    float targetHitboxMinZ = 0.0f;
    float targetHitboxMaxX = 0.0f;
    float targetHitboxMaxY = 0.0f;
    float targetHitboxMaxZ = 0.0f;
    bool armorFilterKeyPrev_ = false;  // Edge-detection state for armor filter keybind
    bool prevFrameHadTarget = false;
    bool targetChangedThisFrame = false;
    bool clickStartedThisFrame = false;
    bool lastMouseDown = false;
    Timer targetRampTimer;
    float targetRampDurationMs = 200.0f;
    Timer clickRampTimer;
    float clickRampDurationMs = 140.0f;
    Timer hitboxRefreshTimer;
    float hitboxRefreshMs = 140.0f;
    float targetSpeedVariance = 1.0f;
    float aimHumanizer = 1.0f;
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

    Timer reactionTimeTimer;
    Timer controllerTimer;

    // Cached Target for hybrid tracking
    jobject cachedTarget = nullptr;
    std::atomic<bool> entityLockEnabled_{false};
    std::atomic<bool> targetResetRequested_{false};

    // Locked target position
    bool hasLockedTarget = false;
    double lockedTargetX = 0.0;
    double lockedTargetY = 0.0;
    double lockedTargetZ = 0.0;

    // Target velocity estimate (blocks/sec, smoothed) for prediction + adaptive speed.
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
