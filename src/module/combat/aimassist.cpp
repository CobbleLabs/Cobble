#include "aimassist.h"
#include "antibot.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"
#include "../../util/inject_trace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AimAssist* aimAssist = nullptr;

// How long a rotation write keeps the claim alive. Five client ticks: long enough that the
// 20 Hz consumers never sample a gap between two 500 Hz writes, short enough that letting go
// of the mouse hands the head back within a fifth of a second.
static constexpr unsigned long long kRotClaimTailMs = 250ull;

void AimAssist::markRotationWrite() {
    lastRotWriteMs_.store(GetTickCount64(), std::memory_order_release);
}

bool AimAssist::isRewritingRotation() const {
    if (!enabled)
        return false;
    const unsigned long long last = lastRotWriteMs_.load(std::memory_order_acquire);
    if (last == 0ull)
        return false;
    const unsigned long long now = GetTickCount64();
    return now >= last && (now - last) <= kRotClaimTailMs;
}


// ── C03PacketPlayer resolution for packet-rotation (mirrors clutch) ──────────
// Resolved lazily on the network thread inside onOutgoingPacket, so no cross-
// thread races on these statics.
static jclass g_aimC03 = nullptr;
static jfieldID g_aim_fid_yaw = nullptr;
static jfieldID g_aim_fid_pitch = nullptr;
static jfieldID g_aim_fid_rotating = nullptr;
static bool g_aimResolvedC03 = false;

static void resolveAimC03(JNIEnv* env) {
    if (g_aimResolvedC03)
        return;
    g_aimResolvedC03 = true;

    jclass localClass = nullptr;
    if (Java::AssignClass("net.minecraft.network.play.client.C03PacketPlayer", localClass)) {
        g_aimC03 = (jclass)env->NewGlobalRef(localClass);

        g_aim_fid_yaw = env->GetFieldID(g_aimC03, "yaw", "F");
        if (!g_aim_fid_yaw)
            g_aim_fid_yaw = env->GetFieldID(g_aimC03, "field_149476_e", "F");

        g_aim_fid_pitch = env->GetFieldID(g_aimC03, "pitch", "F");
        if (!g_aim_fid_pitch)
            g_aim_fid_pitch = env->GetFieldID(g_aimC03, "field_149473_f", "F");

        g_aim_fid_rotating = env->GetFieldID(g_aimC03, "rotating", "Z");
        if (!g_aim_fid_rotating)
            g_aim_fid_rotating = env->GetFieldID(g_aimC03, "field_149481_i", "Z");

        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
}

// Weapon item IDs
namespace ItemIds {
// Swords
constexpr int WOOD_SWORD = 268;
constexpr int STONE_SWORD = 272;
constexpr int IRON_SWORD = 267;
constexpr int DIAMOND_SWORD = 276;
constexpr int GOLD_SWORD = 283;

// Axes
constexpr int WOOD_AXE = 271;
constexpr int STONE_AXE = 275;
constexpr int IRON_AXE = 258;
constexpr int DIAMOND_AXE = 279;
constexpr int GOLD_AXE = 286;
}  // namespace ItemIds

// Neuromotor state for human-like aiming
struct NeuromotorState {
    float driftYaw = 0.0f;  // Accumulated Brownian drift
    float driftPitch = 0.0f;
    float reactionJitter = 0.0f;   // Variable reaction delay
    float overshootFactor = 1.0f;  // Tendency to over/undershoot
    int ticksUntilCorrection = 0;  // When to apply micro-correction
    Timer stateRefreshTimer;

    void update(std::mt19937& rng, float speedSetting = 50.0f) {
        auto randF = [&](float min, float max) {
            std::uniform_real_distribution<float> d(min, max);
            return d(rng);
        };
        auto randI = [&](int min, int max) {
            std::uniform_int_distribution<int> d(min, max);
            return d(rng);
        };

        // Scale drift based on speed setting
        // Low speed needs minimal drift (clean aim), high speed also needs clean aim
        // Medium-low speeds (40-60) can have slightly more drift for human-like feel
        // Formula creates bell curve peaking around speed 50
        float speedNorm = speedSetting / 100.0f;
        float bellCurve = std::exp(-std::pow((speedNorm - 0.5f) * 2.5f, 2.0f));
        float speedScale = 0.15f + bellCurve * 0.45f;  // Range: 0.15 to 0.60

        // Brownian drift: small random walk that accumulates
        float yawDriftStep = randF(-0.012f, 0.012f) * speedScale;
        float pitchDriftStep = randF(-0.008f, 0.008f) * speedScale;

        driftYaw += yawDriftStep;
        driftPitch += pitchDriftStep;

        // Decay drift slowly (mean-reverting) - faster decay at low speeds
        float decayRate = std::lerp(0.85f, 0.94f, speedScale);
        driftYaw *= decayRate;
        driftPitch *= decayRate;

        // Clamp drift to realistic ranges (scaled)
        float maxDrift = 0.06f * speedScale;
        driftYaw = std::clamp(driftYaw, -maxDrift, maxDrift);
        driftPitch = std::clamp(driftPitch, -maxDrift * 0.8f, maxDrift * 0.8f);

        // Refresh overshoot tendency periodically (humans are inconsistent)
        if (stateRefreshTimer.Elapsed<>() > randF(800.f, 2000.f)) {
            // Overshoot closer to 1.0 at low speeds (more precise)
            float overshootRange = std::lerp(0.02f, 0.08f, speedScale);
            overshootFactor = 1.0f + randF(-overshootRange, overshootRange);
            reactionJitter = randF(-15.f, 15.f) * speedScale;
            ticksUntilCorrection = randI(3, 12);
            stateRefreshTimer.Start();
        }
    }
};

static NeuromotorState g_neuroState;

// GCD calculation based on Minecraft's mouse sensitivity formula
static float calculateGCD(float sensitivity) {
    float f = sensitivity * 0.6f + 0.2f;
    return f * f * f * 1.2f;
}

// Strict GCD application - real mice ARE quantized, AC detects when we skip GCD
static float applyGCDStrict(float delta, float gcd) {
    if (gcd < 0.0001f)
        return delta;
    return std::round(delta / gcd) * gcd;
}

// Keep an absolute pitch inside the vertical limit without leaving the mouse grid.
// Clamping to exactly +-90 truncates the delta between consecutive rotations into
// something no mouse could report, and parks the pitch on a value no human holds --
// both of them vertical-aim tells. Stepping back toward zero by whole GCD steps stays
// on the grid by construction, whatever the incoming value was.
static float limitPitchOnGrid(float pitch, float gcd) {
    constexpr float kPitchLimit = 89.9f;
    if (std::abs(pitch) <= kPitchLimit)
        return pitch;
    if (gcd < 0.0001f)
        return pitch > 0.0f ? kPitchLimit : -kPitchLimit;
    const float steps = std::ceil((std::abs(pitch) - kPitchLimit) / gcd);
    return pitch > 0.0f ? pitch - steps * gcd : pitch + steps * gcd;
}

// Vary the integer step counts of the two axes.
//
// A real mouse reports whole counts, so a real rotation delta is always an exact
// multiple of the GCD and its floor divisor is ~0. Being "perfectly aligned" is
// therefore normal, not suspicious, and nudging a delta by a FRACTION of a step to
// disguise it is precisely what a gcd check detects — on both axes at once, and
// hardest on pitch, whose tracking deltas are small enough that a fraction of a
// step is a large relative error.
//
// What can still read as mechanical is the two axes' step counts moving in
// lockstep. So vary the counts themselves and stay on the grid: borrow or lend a
// whole step, occasionally, on one axis. The caller settles the rotation residual
// after this runs, so a step taken here is paid back on the next frame rather than
// accumulating into aim error.
static void varyStepCounts(float& yawDelta, float& pitchDelta, float gcd, std::mt19937& rng) {
    if (gcd < 0.0001f)
        return;

    const long yawSteps = std::lround(yawDelta / gcd);
    const long pitchSteps = std::lround(pitchDelta / gcd);
    if (yawSteps == 0 && pitchSteps == 0)
        return;

    std::uniform_int_distribution<int> roll(0, 99);
    std::uniform_int_distribution<int> pick(0, 1);

    // A locked step ratio across frames is the tell worth breaking. Only touch the
    // axis that is actually carrying several steps, so a 1-step correction is never
    // doubled or cancelled outright.
    const bool yawRoomy = std::labs(yawSteps) >= 2;
    const bool pitchRoomy = std::labs(pitchSteps) >= 2;
    if (!yawRoomy && !pitchRoomy)
        return;
    if (roll(rng) >= 14)
        return;

    const bool touchYaw = yawRoomy && (!pitchRoomy || pick(rng) == 0);
    float& target = touchYaw ? yawDelta : pitchDelta;
    const long steps = touchYaw ? yawSteps : pitchSteps;
    const long adjusted = steps + (pick(rng) == 0 ? 1 : -1);
    target = static_cast<float>(adjusted) * gcd;
}

static float g_cachedGCD = 0.0f;
static Timer g_gcdRefreshTimer;

// AimBoost Implementation
AimBoost::AimBoost(float speed_mult_min, float speed_mult_max, float freq_min_ms, float freq_max_ms, bool continuous)
    : speed_mult_min(speed_mult_min),
      speed_mult_max(speed_mult_max),
      frequency_min_ms((uint32_t)freq_min_ms),
      frequency_max_ms((uint32_t)freq_max_ms),
      continuous(continuous) {
    speed_mult = (speed_mult_min + speed_mult_max) / 2;
    prev_speed_mult = speed_mult;
}

float AimBoost::Delay() {
    static std::random_device rd;
    static std::mt19937 gen(rd());

    auto randFloat = [&](float min, float max) -> float {
        std::uniform_real_distribution<float> dis(min, max);
        return dis(gen);
    };

    auto randInt = [&](int min, int max) -> int {
        std::uniform_int_distribution<int> dis(min, max);
        return dis(gen);
    };

    auto slerp = [](float start, float end, float t) -> float {
        t = std::clamp(t, 0.0f, 1.0f);
        t = t * t * (3.0f - 2.0f * t);
        return start + (end - start) * t;
    };

    if (timer.Elapsed<>() >= frequency_ms) {
        if (continuous) {
            prev_speed_mult = randFloat(speed_mult_min, speed_mult_max);
        } else {
            prev_speed_mult = speed_mult;
            speed_mult = randFloat(speed_mult_min, speed_mult_max);
        }

        frequency_ms = randInt(frequency_min_ms, frequency_max_ms);
        timer.Start();
    }

    return slerp(prev_speed_mult, continuous ? speed_mult : 1.f, timer.Elapsed<>() / frequency_ms);
}

// AimAssist Implementation
AimAssist::AimAssist() : Module("AimAssist") {
    keybind = 0;
    std::random_device rd;
    rng.seed(rd());
    entityLockEnabled_.store(settings.entityLock, std::memory_order_relaxed);
}

void AimAssist::releaseCachedTarget(JNIEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    if (!cachedTarget || !env)
        return;

    if (env->ExceptionCheck())
        env->ExceptionClear();
    env->DeleteGlobalRef(cachedTarget);
    if (env->ExceptionCheck())
        env->ExceptionClear();
    cachedTarget = nullptr;
}

void AimAssist::resetTargetState(JNIEnv* env) {
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    releaseCachedTarget(env);
    hasLockedTarget = false;
    targetHasHitbox = false;
    prevFrameHadTarget = false;
    targetChangedThisFrame = false;
    reactionPrimed = false;
    controllerTimer.Start();
    yawdiffSpeed = 0.0f;
    pitchdiffSpeed = 0.0f;
    simulatedJitterYaw = simulatedJitterPitch = 0.0f;
    simulatedJitterTargetYaw = simulatedJitterTargetPitch = 0.0f;
    appliedJitterYaw_ = appliedJitterPitch_ = 0.0f;
    yawRotationResidual = pitchRotationResidual = 0.0;
    cursorAccumY = 0.0;
    // Drop the velocity estimate with the target it was measured from.
    predVelX = predVelY = predVelZ = 0.0;
    predTimer.Start();
}

void AimAssist::setEntityLock(bool enabled) {
    settings.entityLock = enabled;
    entityLockEnabled_.store(enabled, std::memory_order_release);
    targetResetRequested_.store(true, std::memory_order_release);
}

void AimAssist::onWorldChange() {
    resetTargetState(Java::Env());
    targetResetRequested_.store(true, std::memory_order_release);
}

void AimAssist::onEnable() {
    log_ok("AimAssist enabled");

    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    JNIEnv* env = Java::Env();
    releaseCachedTarget(env);
    entityLockEnabled_.store(settings.entityLock, std::memory_order_release);
    targetResetRequested_.store(false, std::memory_order_release);
    hasLockedTarget = false;
    pitchdiffSpeed = 0.f;
    yawdiffSpeed = 0.f;
    cursorAccumY = 0.0;
    yawRotationResidual = pitchRotationResidual = 0.0;
    targetYawSpeed = 0.f;
    targetPitchSpeed = 0.f;
    reactionPrimed = false;
    controllerTimer.Start();
    targetHasHitbox = false;
    targetHitboxMinX = targetHitboxMinY = targetHitboxMinZ = 0.0f;
    targetHitboxMaxX = targetHitboxMaxY = targetHitboxMaxZ = 0.0f;
    prevFrameHadTarget = false;
    targetRampTimer.Start();
    clickRampTimer.Start();
    hitboxRefreshTimer.Start();
    lastMouseDown = false;
    clickStartedThisFrame = false;
    targetSpeedVariance = 1.0f;
    aimHumanizer = 1.0f;
    simulatedJitterYaw = simulatedJitterPitch = 0.0f;
    simulatedJitterTargetYaw = simulatedJitterTargetPitch = 0.0f;
    appliedJitterYaw_ = appliedJitterPitch_ = 0.0f;
    simulatedJitterTimer.Start();
}

void AimAssist::onDisable() {
    log_ok("AimAssist disabled");
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    hasLockedTarget = false;

    releaseCachedTarget(Java::Env());
    targetResetRequested_.store(false, std::memory_order_release);
    cursorAccumY = 0.0;
    yawRotationResidual = pitchRotationResidual = 0.0;
    pitchdiffSpeed = yawdiffSpeed = 0.f;
    targetYawSpeed = 0.f;
    targetPitchSpeed = 0.f;
    reactionPrimed = false;
    targetHasHitbox = false;
    targetHitboxMinX = targetHitboxMinY = targetHitboxMinZ = 0.0f;
    targetHitboxMaxX = targetHitboxMaxY = targetHitboxMaxZ = 0.0f;
    reactionTimeTimer.Start();
    lastMouseDown = false;
    clickStartedThisFrame = false;
    simulatedJitterYaw = simulatedJitterPitch = 0.0f;
    simulatedJitterTargetYaw = simulatedJitterTargetPitch = 0.0f;
    appliedJitterYaw_ = appliedJitterPitch_ = 0.0f;
}

float AimAssist::wrapTo180(float value) {
    float res = std::fmod(value + 180.f, 360.f);
    if (res < 0)
        res += 360.f;
    return res - 180.f;
}

std::pair<float, float> AimAssist::getAngles(double x1, double y1, double z1, double x2, double y2, double z2) {
    float d_x = (float)(x2 - x1);
    float d_y = (float)(y2 - y1);
    float d_z = (float)(z2 - z1);

    constexpr float PI = 3.14159265358979323846f;
    float hypothenuse = std::sqrt(d_x * d_x + d_z * d_z);
    float yaw = std::atan2(d_z, d_x) * 180.f / PI - 90.f;
    float pitch = -std::atan2(d_y, hypothenuse) * 180.f / PI;

    return std::make_pair(yaw, pitch);
}

float AimAssist::randFloat(float min, float max) {
    std::uniform_real_distribution<float> dis(min, max);
    return dis(rng);
}

int AimAssist::randInt(int min, int max) {
    std::uniform_int_distribution<int> dis(min, max);
    return dis(rng);
}

float AimAssist::slerp(float start, float end, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    return start + (end - start) * t;
}

void AimAssist::onUpdate() {
    std::lock_guard<std::recursive_mutex> lock(targetMutex_);
    clickStartedThisFrame = false;
    // Default off each tick; applyAimController re-arms it only while actively
    // aiming with packetRotation, so idle/no-target ticks never rewrite packets.
    hasServerRotation_.store(false, std::memory_order_relaxed);
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    if (targetResetRequested_.exchange(false, std::memory_order_acq_rel))
        resetTargetState(env);

    const bool entityLock = entityLockEnabled_.load(std::memory_order_acquire);
    if (!player.isValid()) {
        markAimIdle();
        return;
    }
    if (Menu::isOpen) {
        markAimIdle();
        return;
    }
    if (mc.isInGui()) {
        markAimIdle();
        return;
    }

    // Toggle the armor filter on/off with its dedicated keybind (edge-triggered).
    if (settings.armorFilterKeybind != 0) {
        bool keyDown = (GetAsyncKeyState(settings.armorFilterKeybind) & 0x8000) != 0;
        if (keyDown && !armorFilterKeyPrev_) {
            settings.armorFilterEnabled = !settings.armorFilterEnabled;
            NotificationManager::add("Armor Filter", settings.armorFilterEnabled);
        }
        armorFilterKeyPrev_ = keyDown;
    }

    bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (!settings.alwaysAim && !mouseDown) {
        // Entity Lock keeps the target between clicks. Without it, the next
        // click starts with a fresh target selection.
        if (!entityLock)
            resetTargetState(env);
        lastMouseDown = false;
        markAimIdle();
        return;
    }

    if (mouseDown && !lastMouseDown) {
        clickStartedThisFrame = true;
        clickRampTimer.Start();
        targetRampTimer.Start();
        yawdiffSpeed = 0.f;
        pitchdiffSpeed = 0.f;
        reactionPrimed = false;
    }

    if (settings.breakBlocks && mouseDown) {
        if (mc.isTargetingBlock()) {
            markAimIdle();
            return;
        }
    }

    // Weapon-only check
    if (settings.weaponOnly && !isHoldingWeapon()) {
        markAimIdle();
        return;
    }

    float localYaw = player.getYaw();
    float localPitch = player.getPitch();
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();
    // Angles must originate from the eyes, not the feet, or the pitch aims ~1.62
    // blocks too high (the "vertical aims above the player" bug).
    double eyeY = localY + 1.62;
    targetChangedThisFrame = false;

    double targetX = 0.0;
    double targetY = 0.0;
    double targetZ = 0.0;

    hasLockedTarget = findTarget(targetX, targetY, targetZ);

    if (!hasLockedTarget) {
        prevFrameHadTarget = false;
        // Do not leave the last correction velocity/residual alive when a
        // target scan misses for a frame. Otherwise the next click can inherit
        // an old direction and pull the camera toward the floor or a nearby
        // block even though the button is still held.
        yawdiffSpeed = pitchdiffSpeed = 0.0f;
        targetYawSpeed = targetPitchSpeed = 0.0f;
        yawRotationResidual = pitchRotationResidual = 0.0;
        cursorAccumY = 0.0;
        reactionPrimed = false;
        // The velocity estimate belongs to a target we no longer have; a stale
        // lead is what makes the next acquisition start with a flick.
        predVelX = predVelY = predVelZ = 0.0;
        predTimer.Start();
        controllerTimer.Start();
        lastMouseDown = mouseDown;
        return;
    }

    lockedTargetX = targetX;
    lockedTargetY = targetY;
    lockedTargetZ = targetZ;

    // Estimate the target's velocity (blocks/sec, smoothed).
    //
    // Remote entity positions only advance on server ticks (~20 Hz) while this
    // module runs every ~2 ms, so differentiating on every iteration divided a
    // whole tick of movement by one loop period: the estimate spiked to 20-50x
    // the real speed on the single iteration where the step landed and decayed to
    // zero in between. Prediction turned that sawtooth into a random flick.
    // Sample on actual movement instead, over the time since the last movement.
    {
        // A target switch is a position discontinuity, not motion: re-baseline so
        // the gap between two players never reads as velocity.
        if (targetChangedThisFrame) {
            predVelX = predVelY = predVelZ = 0.0;
            predPrevX = targetX;
            predPrevY = targetY;
            predPrevZ = targetZ;
            predTimer.Start();
        }

        const double jdx = targetX - predPrevX;
        const double jdy = targetY - predPrevY;
        const double jdz = targetZ - predPrevZ;
        const double moved2 = jdx * jdx + jdy * jdy + jdz * jdz;
        const float dtp = predTimer.Elapsed<>() / 1000.0f;

        if (moved2 > 1e-8 && dtp >= 0.008f) {
            if (dtp > 0.5f || moved2 > 9.0) {
                // Hitch or teleport - not motion worth leading.
                predVelX = predVelY = predVelZ = 0.0;
            } else {
                const double sm = 0.35;
                predVelX += ((jdx / dtp) - predVelX) * sm;
                predVelY += ((jdy / dtp) - predVelY) * sm;
                predVelZ += ((jdz / dtp) - predVelZ) * sm;
            }
            predPrevX = targetX;
            predPrevY = targetY;
            predPrevZ = targetZ;
            predTimer.Start();
        } else if (dtp > 0.3f) {
            // Standing still long enough that the last estimate is stale.
            predVelX = predVelY = predVelZ = 0.0;
            predPrevX = targetX;
            predPrevY = targetY;
            predPrevZ = targetZ;
            predTimer.Start();
        }

        // A sprint-jumping player tops out near 8 blocks/sec; anything past this
        // is a sampling artifact, not something to lead.
        predVelX = std::clamp(predVelX, -12.0, 12.0);
        predVelY = std::clamp(predVelY, -12.0, 12.0);
        predVelZ = std::clamp(predVelZ, -12.0, 12.0);
    }

    // Adaptive + Entity Lock = hard-lock chest mode. Any of the two alone keeps
    // the classic aim-assist behavior. Gate all the "sticky chest" changes on it.
    const bool adaptiveLock = (settings.aimMode == AimMode::ADAPTIVE) && entityLock;

    // Resolve the aim point from the target's REAL position. Feeding a predicted
    // position in here tripped getAimPoint's "detached hitbox" guards as soon as
    // the lead passed 1.5 blocks: the box got rebuilt around the predicted point
    // and the aim teleported by the whole lead - a hard flick, and the only way
    // prediction affected the result at all, since the aim point is otherwise
    // derived from the (unpredicted) AABB. Prediction is a bounded offset on the
    // resolved point instead.
    double aimX, aimY, aimZ;
    if (!getAimPoint(targetX, targetY, targetZ, aimX, aimY, aimZ)) {
        markAimIdle();
        return;
    }

    // Prediction: lead the aim point by the target's velocity so tracking sits
    // ahead of a strafing target instead of trailing it. Hard-lock skips the
    // vertical lead so a jumping/falling target does not pull the aim off chest.
    //
    // The lead is bounded by the hitbox itself, not by a constant. A player box is
    // 0.6 blocks wide, so the +-0.75 block clamp this used to carry allowed an aim
    // point nearly half a block OUTSIDE the box: the rotation pointed at empty air,
    // the client's own raytrace stopped returning the entity, and any attack that
    // still went out was one the server raytraced into nothing -- which is exactly
    // what Intave reports as AttackRaytrace "attacked a player out of sight".
    // Leading is only worth anything while the led point is still on the target.
    if (settings.prediction > 0.0f && resolvedBox_.valid) {
        const double leadSec = (settings.prediction / 100.0) * 0.16;
        // Keep the ray off the faces: a point exactly on the plane is a coin flip
        // between hit and miss once the server's copy of the position differs from
        // ours by a rounding step.
        constexpr double kFaceInset = 0.09;
        // Room from the aim point to each face, so a point already pushed to the near
        // side by aimAtClosestPoint gets the asymmetric budget it actually has.
        const auto lead = [&](double vel, double point, double lo, double hi) {
            const double back = std::max(0.0, (point - lo) - kFaceInset);
            const double fwd = std::max(0.0, (hi - point) - kFaceInset);
            return std::clamp(vel * leadSec, -back, fwd);
        };
        aimX += lead(predVelX, aimX, resolvedBox_.minX, resolvedBox_.maxX);
        aimZ += lead(predVelZ, aimZ, resolvedBox_.minZ, resolvedBox_.maxZ);
        if (!settings.horizontalOnly && !adaptiveLock)
            aimY += lead(predVelY, aimY, resolvedBox_.minY, resolvedBox_.maxY);
    }

    // How far the rotation may be nudged off the aim point before the ray leaves the
    // box, in degrees, measured at the target's current distance. packetRotation adds
    // its jitter to the C03 only -- the server raytraces a rotation the camera never
    // had -- so that offset has to be budgeted against the target's angular size or it
    // is a miss the player cannot see coming. Wide box and close range means a large
    // budget; at max reach the budget collapses and so does the jitter.
    {
        float yawBudget = 0.0f, pitchBudget = 0.0f;
        if (resolvedBox_.valid) {
            const double dx = aimX - localX;
            const double dz = aimZ - localZ;
            const double flat = std::sqrt(dx * dx + dz * dz);
            const double dy = aimY - eyeY;
            const double dist = std::sqrt(flat * flat + dy * dy);
            const double halfW =
                std::min(resolvedBox_.maxX - resolvedBox_.minX, resolvedBox_.maxZ - resolvedBox_.minZ) * 0.5;
            const double halfH = (resolvedBox_.maxY - resolvedBox_.minY) * 0.5;
            if (flat > 0.05 && halfW > 0.0)
                yawBudget = (float)(std::atan2(halfW - 0.09, flat) * 57.29577951308232);
            if (dist > 0.05 && halfH > 0.0)
                pitchBudget = (float)(std::atan2(halfH - 0.09, dist) * 57.29577951308232);
            // Only part of the budget is spent, because the rendered rotation is itself
            // still converging on the aim point and owns the rest of it.
            yawBudget = std::clamp(yawBudget * 0.5f, 0.0f, 3.0f);
            pitchBudget = std::clamp(pitchBudget * 0.5f, 0.0f, 3.0f);
        }
        packetYawBudget_.store(yawBudget, std::memory_order_release);
        packetPitchBudget_.store(pitchBudget, std::memory_order_release);
    }

    auto [yawTarget, pitchTarget] = getAngles(localX, eyeY, localZ, aimX, aimY, aimZ);

    float yawDiff = wrapTo180(-(localYaw - yawTarget));
    float pitchDiff = wrapTo180(-(localPitch - pitchTarget));

    // Stop On Hitbox: If crosshair is already inside the hitbox, let user aim freely.
    // Adaptive+EntityLock is a hard-lock mode by design — never release, even
    // mid-hitbox, so the aim stays pinned on the chest when the enemy is close.
    if (settings.stopOnHitbox && targetHasHitbox && !adaptiveLock) {
        float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
        float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;

        if (hitboxWidth > 0.01f && hitboxHeight > 0.01f) {
            // Calculate angles to hitbox edges
            float yawToMinX = getAngles(localX, eyeY, localZ, targetHitboxMinX, aimY, targetHitboxMinZ).first;
            float yawToMaxX = getAngles(localX, eyeY, localZ, targetHitboxMaxX, aimY, targetHitboxMaxZ).first;
            float pitchToMinY = getAngles(localX, eyeY, localZ, aimX, targetHitboxMinY, aimZ).second;
            float pitchToMaxY = getAngles(localX, eyeY, localZ, aimX, targetHitboxMaxY, aimZ).second;

            float yawDiffToMinX = wrapTo180(-(localYaw - yawToMinX));
            float yawDiffToMaxX = wrapTo180(-(localYaw - yawToMaxX));
            float pitchDiffToMinY = wrapTo180(-(localPitch - pitchToMinY));
            float pitchDiffToMaxY = wrapTo180(-(localPitch - pitchToMaxY));

            // Inside hitbox: signs of diffs to opposite edges are different
            bool insideYaw = (yawDiffToMinX * yawDiffToMaxX) <= 0.0f;
            bool insidePitch = (pitchDiffToMinY * pitchDiffToMaxY) <= 0.0f;

            bool insideHitbox = settings.horizontalOnly ? insideYaw : (insideYaw && insidePitch);

            if (insideHitbox) {
                prevFrameHadTarget = hasLockedTarget;
                lastMouseDown = mouseDown;
                markAimIdle();
                return;
            }
        }
    }

    // Hurt Time Lock: Instant aim when switching targets
    if (settings.targetMode == TargetMode::SWITCH && settings.switchSortBy == SortBy::HURT_TIME &&
        settings.hurtTimeLock && targetChangedThisFrame) {
        // Snapping is the point here, but the snap still has to land somewhere a mouse
        // could put it: a whole number of GCD steps away from the current rotation.
        // Writing the exact target instead leaves a delta that is not a multiple of the
        // step, which is the one thing about a flick that a gcd check can see. What is
        // given up is at most half a step, well below the resolution of a hitbox.
        const float gcdSnap = g_cachedGCD > 0.0001f ? g_cachedGCD
                                                    : calculateGCD(mc.getMouseSensitivity());
        g_cachedGCD = gcdSnap;
        const float yawNow = player.getYaw();
        const float snappedYaw =
            yawNow + applyGCDStrict(wrapTo180(yawTarget - yawNow), gcdSnap);
        player.setYaw(snappedYaw);
        player.setPrevYaw(snappedYaw);
        if (!settings.horizontalOnly) {
            const float pitchNow = player.getPitch();
            const float stepped = pitchNow + applyGCDStrict(pitchTarget - pitchNow, gcdSnap);
            const float snappedPitch = limitPitchOnGrid(stepped, gcdSnap);
            player.setPitch(snappedPitch);
            player.setPrevPitch(snappedPitch);
        }
        markRotationWrite();
        prevFrameHadTarget = hasLockedTarget;
        lastMouseDown = mouseDown;
        // The rotation was written absolutely, so no jitter offset is folded in
        // any more - forget it instead of unwinding it on the next update.
        appliedJitterYaw_ = appliedJitterPitch_ = 0.0f;
        markAimIdle();
        return;
    }

    cachedMouseSensitivity = mc.getMouseSensitivity();

    // All aim modes now share one time-based controller. The mode only changes
    // response rate; it no longer swaps in a separate pitch/yaw implementation.
    applyAimController(yawDiff, pitchDiff);
    prevFrameHadTarget = hasLockedTarget;
    lastMouseDown = mouseDown;
}

bool AimAssist::findTarget(double& targetX, double& targetY, double& targetZ) {
    // HYBRID TRACKING SYSTEM
    // 1. Fast Path (Every Frame): Update position of locked target directly (JNI)
    // 2. Slow Path (Scan Interval): Scan full player list for new/better targets
    static Timer scanTimer;
    JNIEnv* env = Java::Env();
    if (!env)
        return false;
    const bool entityLock = entityLockEnabled_.load(std::memory_order_acquire);

    // CRITICAL: Clear any pending JNI exceptions that could cause silent failures
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
        if (cachedTarget) {
            env->DeleteGlobalRef(cachedTarget);
            cachedTarget = nullptr;
        }
        targetHasHitbox = false;
        log_warn("[AimAssist] Cleared JNI exception - cache reset");
    }

    bool wantHitboxRefresh = entityLock && cachedTarget && hitboxRefreshTimer.Elapsed<>() >= hitboxRefreshMs;
    bool forceScan = false;

    // FAST PATH: If we have a cached target, update its position & validate
    if (cachedTarget && scanTimer.Elapsed<>() < 50 && !wantHitboxRefresh)  // Rescan every 50ms
    {
        // Check validity (basic)
        if (Java::Env()->IsSameObject(cachedTarget, NULL)) {
            Java::Env()->DeleteGlobalRef(cachedTarget);  // Clean up invalid ref
            cachedTarget = nullptr;                      // Invalid
            targetHasHitbox = false;
            prevFrameHadTarget = false;
            forceScan = true;
        } else {
            if (!JniCache::fid_posX || !JniCache::fid_posY || !JniCache::fid_posZ) {
                Java::Env()->ExceptionClear();
                Java::Env()->DeleteGlobalRef(cachedTarget);
                cachedTarget = nullptr;
                targetHasHitbox = false;
                forceScan = true;
            } else {
                // Update position
                double cX = Java::Env()->GetDoubleField(cachedTarget, JniCache::fid_posX);
                double cY = Java::Env()->GetDoubleField(cachedTarget, JniCache::fid_posY);
                double cZ = Java::Env()->GetDoubleField(cachedTarget, JniCache::fid_posZ);

                if (Java::Env()->ExceptionCheck()) {
                    Java::Env()->ExceptionClear();
                    Java::Env()->DeleteGlobalRef(cachedTarget);
                    cachedTarget = nullptr;
                    targetHasHitbox = false;
                    prevFrameHadTarget = false;
                    forceScan = true;
                } else {
                    // The cached entity position is refreshed every frame, so
                    // refresh its absolute AABB with it. Keeping the first scan's
                    // box while the player strafes makes the aim point drift to
                    // an old/nearby coordinate (the apparent random block flick).
                    if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
                        jobject aabb = Java::Env()->GetObjectField(cachedTarget, JniCache::fid_boundingBox);
                        if (aabb) {
                            if (JniCache::fid_aabb_minX) targetHitboxMinX = (float)Java::Env()->GetDoubleField(aabb, JniCache::fid_aabb_minX);
                            if (JniCache::fid_aabb_minY) targetHitboxMinY = (float)Java::Env()->GetDoubleField(aabb, JniCache::fid_aabb_minY);
                            if (JniCache::fid_aabb_minZ) targetHitboxMinZ = (float)Java::Env()->GetDoubleField(aabb, JniCache::fid_aabb_minZ);
                            if (JniCache::fid_aabb_maxX) targetHitboxMaxX = (float)Java::Env()->GetDoubleField(aabb, JniCache::fid_aabb_maxX);
                            if (JniCache::fid_aabb_maxY) targetHitboxMaxY = (float)Java::Env()->GetDoubleField(aabb, JniCache::fid_aabb_maxY);
                            if (JniCache::fid_aabb_maxZ) targetHitboxMaxZ = (float)Java::Env()->GetDoubleField(aabb, JniCache::fid_aabb_maxZ);
                            Java::Env()->DeleteLocalRef(aabb);
                            const float w = targetHitboxMaxX - targetHitboxMinX;
                            const float h = targetHitboxMaxY - targetHitboxMinY;
                            targetHasHitbox = w > 0.01f && h > 0.01f;
                        }
                        if (Java::Env()->ExceptionCheck())
                            Java::Env()->ExceptionClear();
                    }
                    // Basic distance check to see if we should drop it immediately
                    double localX = player.getPosX();
                    double localY = player.getPosY();
                    double localZ = player.getPosZ();
                    double dx = cX - localX;
                    double dy = cY - localY;
                    double dz = cZ - localZ;
                    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    // Adaptive + Entity Lock is a hard-lock — a locked target stays
                    // locked even past the configured FOV/distance until it's really
                    // out of reach. Adaptive alone follows the normal drop rules.
                    const bool adaptiveHardLock = (settings.aimMode == AimMode::ADAPTIVE) && entityLock;
                    double maxDist = entityLock ? (settings.distance + 1.5f) : settings.distance;
                    if (adaptiveHardLock)
                        maxDist = std::max<double>(maxDist, settings.distance + 3.0);

                    // If angle error is large, force slow scan to re-acquire smoothly
                    float localYaw = player.getYaw();
                    // Position fields are feet-space; target selection must use
                    // the same eye/chest space as the actual aim point.
                    float yawToTarget = getAngles(localX, localY + 1.62, localZ,
                                                  cX, cY + 0.9, cZ).first;
                    float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));
                    float limit = entityLock ? std::max(90.0f, settings.fov * 0.5f) : (settings.fov * 0.5f);
                    if (adaptiveHardLock)
                        limit = std::max(limit, 180.0f);
                    // Point-blank targets: don't drop them through tight strafes even
                    // when the configured FOV is narrow. Fixes "close targets sometimes
                    // don't work" — up close a small position change swings the yaw a lot.
                    if (dist < 3.0f)
                        limit = std::max(limit, 150.0f);

                    if (dist > maxDist || dist < 0.08f || yawDiff > limit) {
                        log_debug("[AimAssist] target dropped: dist=%.1f yaw=%.1f", dist, yawDiff);
                        Java::Env()->DeleteGlobalRef(cachedTarget);
                        cachedTarget = nullptr;
                        targetHasHitbox = false;
                        prevFrameHadTarget = false;
                        forceScan = true;  // force slow scan to drop/find new target
                    } else {
                        targetX = cX;
                        targetY = cY;
                        targetZ = cZ;
                        return true;
                    }
                }
            }
        }
    }

    // SLOW PATH GUARD: If we didn't take the fast path, checks if we are allowed to scan
    if (!forceScan && scanTimer.Elapsed<>() < 50 && !wantHitboxRefresh) {
        return false;  // Skip scan (rate limited)
    }

    // SLOW PATH: Full Scan
    scanTimer.Start();

    auto players = mc.getPlayerList();

    // Clear any exceptions that occurred during player list fetch
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (friendsManager) {
        friendsManager->resolvePending(players);
    }

    if (players.empty()) {
        if (cachedTarget) {
            Java::Env()->DeleteGlobalRef(cachedTarget);
            cachedTarget = nullptr;
        }
        targetHasHitbox = false;
        return false;
    }

    float localYaw = player.getYaw();
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();

    float lowestHealth = FLT_MAX;
    float minimalAngleDiff = (float)settings.fov / 2.f;
    float closestDistance = FLT_MAX;
    float bestSwitchScore = FLT_MAX;  // Combined score for Switch mode
    bool found = false;

    jobject bestPlayerObjLocal = nullptr;
    PlayerInfo closestPlayer;
    bool refreshedHitbox = false;
    bool lockedEntityFound = false;
    auto releasePlayerRef = [env](PlayerInfo& candidate) {
        if (candidate.instance && env) {
            env->DeleteGlobalRef(candidate.instance);
            candidate.instance = nullptr;
        }
    };

    for (auto& p : players) {
        bool sameAsLocked = false;
        if (cachedTarget && env) {
            if (env->IsSameObject(cachedTarget, NULL)) {
                Java::Env()->DeleteGlobalRef(cachedTarget);
                cachedTarget = nullptr;
            } else {
                sameAsLocked = env->IsSameObject(cachedTarget, p.instance);
            }
        }

        // Refresh stored hitbox for Entity Lock to avoid stale dimensions.
        if (sameAsLocked && entityLock) {
            targetHitboxMinX = p.hitboxMinX;
            targetHitboxMinY = p.hitboxMinY;
            targetHitboxMinZ = p.hitboxMinZ;
            targetHitboxMaxX = p.hitboxMaxX;
            targetHitboxMaxY = p.hitboxMaxY;
            targetHitboxMaxZ = p.hitboxMaxZ;
            float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
            float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;
            targetHasHitbox = (hitboxWidth > 0.01f) && (hitboxHeight > 0.01f);
            refreshedHitbox = true;
        }

        double dx = p.posX - localX;
        double dy = p.posY - localY;
        double dz = p.posZ - localZ;
        float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

        float maxDist = (entityLock && sameAsLocked) ? (settings.distance + 1.5f) : settings.distance;
        if (dist > maxDist || dist < 0.08f) {
            releasePlayerRef(p);
            continue;
        }
        if (p.isInvisible && !settings.invisibles) {
            releasePlayerRef(p);
            continue;
        }
        if (AntiBot::Filtered(p)) {
            releasePlayerRef(p);
            continue;
        }
        if (p.health <= 0.0f) {
            releasePlayerRef(p);
            continue;
        }

        if (friendsManager) {
            if (p.isOnLocalTeam && friendsManager->honorsVanillaTeams()) {
                releasePlayerRef(p);
                continue;
            }

            bool isApolloTeam = !p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid);
            if (isApolloTeam) {
                releasePlayerRef(p);
                continue;
            }

            bool isApolloNametag = !p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid);
            if (isApolloNametag) {
                releasePlayerRef(p);
                continue;
            }

            // Priority 3: Manual Friends
            bool isFriendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool isFriendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (isFriendByUUID || isFriendByName) {
                releasePlayerRef(p);
                continue;
            }

            // Priority 4: Scoreboard Teams (color matching)
            // Get local player's team color for comparison (cached per scan)
            static int localTeamColor = -1;
            static Timer colorRefreshTimer;
            if (colorRefreshTimer.Elapsed<>() > 1000) {  // Refresh every second
                localTeamColor = mc.getLocalPlayerTeamColor();
                colorRefreshTimer.Start();
            }

            ChatColor playerColor = static_cast<ChatColor>(p.teamColor);
            ChatColor localColor = static_cast<ChatColor>(localTeamColor);
            static std::string localPlayerName;
            static Timer nameRefreshTimer;
            if (localPlayerName.empty() || nameRefreshTimer.Elapsed<>() > 1000) {
                localPlayerName = mc.getLocalPlayerName();
                nameRefreshTimer.Start();
            }
            bool isScoreboardTeammate =
                friendsManager->isScoreboardTeammate(localPlayerName,
                                                     p.profileName.empty() ? p.name : p.profileName) ||
                friendsManager->isTeammate(p.uuid, playerColor, localColor);
            if (isScoreboardTeammate) {
                releasePlayerRef(p);
                continue;
            }
        }

        // ── Armor filter (multi-select tiers) ─────────────────────────────────
        // Target the player if ANY of their armor pieces matches ANY checked
        // tier. ARMOR_NO_ARMOR matches players wearing nothing. Mask 0 = off.
        if (settings.armorFilterEnabled && settings.armorTierMask != 0) {
            auto tierBit = [](int id) -> int {
                if (id >= 298 && id <= 301) return AimAssist::ARMOR_LEATHER;
                if (id >= 302 && id <= 305) return AimAssist::ARMOR_CHAIN;
                if (id >= 306 && id <= 309) return AimAssist::ARMOR_IRON;
                if (id >= 310 && id <= 313) return AimAssist::ARMOR_DIAMOND;
                if (id >= 314 && id <= 317) return AimAssist::ARMOR_GOLD;
                return 0;
            };
            bool match = false;
            bool hasAnyArmor = false;
            for (const auto& piece : p.armor) {
                if (piece.id > 0) {
                    hasAnyArmor = true;
                    int b = tierBit(piece.id);
                    if (b && (settings.armorTierMask & b)) { match = true; break; }
                }
            }
            if (!match && (settings.armorTierMask & AimAssist::ARMOR_NO_ARMOR) && !hasAnyArmor)
                match = true;
            if (!match) { releasePlayerRef(p); continue; }
        }
        // ─────────────────────────────────────────────────────────────────────

        float yawToTarget = getAngles(localX, localY + 1.62, localZ,
                                      p.posX, p.posY + 0.9, p.posZ).first;
        float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));

        float maxYawDiff = (entityLock && sameAsLocked) ? std::max(90.0f, settings.fov * 0.5f) : (settings.fov * 0.5f);
        // Acquire close targets even at wide angles (see fast-path note above).
        if (dist < 3.0f)
            maxYawDiff = std::max(maxYawDiff, 150.0f);
        if (yawDiff > maxYawDiff) {
            releasePlayerRef(p);
            continue;
        }

        // A valid locked entity wins regardless of target or switch sort mode.
        if (entityLock && sameAsLocked) {
            if (bestPlayerObjLocal && env)
                env->DeleteLocalRef(bestPlayerObjLocal);
            bestPlayerObjLocal = env && p.instance ? env->NewLocalRef(p.instance) : nullptr;
            closestPlayer = p;
            found = true;
            lockedEntityFound = true;
        } else if (lockedEntityFound) {
            // Do not let later candidates replace the locked entity.
        } else if (settings.targetMode == TargetMode::DISTANCE) {
            if (dist < closestDistance) {
                closestDistance = dist;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        } else if (settings.targetMode == TargetMode::HEALTH) {
            if (p.health < lowestHealth) {
                lowestHealth = p.health;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        } else if (settings.targetMode == TargetMode::SWITCH) {
            // Switch mode: Always pick the best target based on switchSortBy criteria
            // Use composite score: primary criterion + small distance tie-breaker
            float score = FLT_MAX;
            float tieBreaker = dist * 0.001f;  // Small distance factor for tie-breaking

            switch (settings.switchSortBy) {
                case SortBy::DISTANCE:
                    score = dist;
                    break;
                case SortBy::HEALTH:
                    score = p.health + tieBreaker;
                    break;
                case SortBy::FOV:
                    score = yawDiff + tieBreaker;
                    break;
                case SortBy::HURT_TIME:
                    score = (float)p.hurtTime + tieBreaker;
                    break;
            }

            // Hysteresis: Give current target a score bonus to prevent rapid switching
            // Only switch if the new target is significantly better
            bool isCurrentTarget = cachedTarget && env && !env->IsSameObject(cachedTarget, NULL) &&
                                   env->IsSameObject(cachedTarget, p.instance);
            if (isCurrentTarget) {
                score -= 0.5f;  // Bonus for staying on current target
            }

            if (score < bestSwitchScore) {
                bestSwitchScore = score;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        } else  // TargetMode::FOV
        {
            if (yawDiff < minimalAngleDiff) {
                minimalAngleDiff = yawDiff;
                closestPlayer = p;
                found = true;
                if (env) {
                    if (bestPlayerObjLocal)
                        env->DeleteLocalRef(bestPlayerObjLocal);
                    bestPlayerObjLocal = p.instance ? env->NewLocalRef(p.instance) : nullptr;
                }
            }
        }

        releasePlayerRef(p);
    }

    if (found) {
        targetX = closestPlayer.posX;
        targetY = closestPlayer.posY;
        targetZ = closestPlayer.posZ;
        targetHitboxMinX = closestPlayer.hitboxMinX;
        targetHitboxMinY = closestPlayer.hitboxMinY;
        targetHitboxMinZ = closestPlayer.hitboxMinZ;
        targetHitboxMaxX = closestPlayer.hitboxMaxX;
        targetHitboxMaxY = closestPlayer.hitboxMaxY;
        targetHitboxMaxZ = closestPlayer.hitboxMaxZ;

        float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
        float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;
        targetHasHitbox = (hitboxWidth > 0.01f) && (hitboxHeight > 0.01f);
    } else {
        targetHasHitbox = false;
    }

    if (found && bestPlayerObjLocal) {
        if (cachedTarget) {
            // Check if it's the SAME object to avoid delete/new overhead?
            if (!Java::Env()->IsSameObject(cachedTarget, bestPlayerObjLocal)) {
                Java::Env()->DeleteGlobalRef(cachedTarget);
                cachedTarget = Java::Env()->NewGlobalRef(bestPlayerObjLocal);
                targetChangedThisFrame = true;
            }
        } else {
            cachedTarget = Java::Env()->NewGlobalRef(bestPlayerObjLocal);
            targetChangedThisFrame = true;
        }
    } else {
        if (cachedTarget) {
            Java::Env()->DeleteGlobalRef(cachedTarget);
            cachedTarget = nullptr;
        }
    }

    if (bestPlayerObjLocal && env) {
        env->DeleteLocalRef(bestPlayerObjLocal);
    }

    if (refreshedHitbox) {
        hitboxRefreshTimer.Start();
    }

    return found;
}

void AimAssist::updateSimulatedJitter() {
    float intensity = std::clamp(settings.jitterPercent, 0.0f, 100.0f) / 100.0f;
    if (intensity <= 0.0001f) {
        simulatedJitterYaw = simulatedJitterPitch = 0.0f;
        simulatedJitterTargetYaw = simulatedJitterTargetPitch = 0.0f;
        return;
    }

    if (simulatedJitterTimer.Elapsed<>() >= static_cast<float>(simulatedJitterIntervalMs)) {
        float amplitude = std::lerp(0.015f, 0.42f, intensity);
        float impulse = randFloat(0.0f, 1.0f) < (0.04f + intensity * 0.14f) ? randFloat(1.15f, 1.8f) : 1.0f;
        simulatedJitterTargetYaw = randFloat(-amplitude, amplitude) * impulse;
        simulatedJitterTargetPitch = randFloat(-amplitude * 0.65f, amplitude * 0.65f) * impulse;

        int intervalMin = static_cast<int>(std::lerp(70.0f, 11.0f, intensity));
        int intervalMax = static_cast<int>(std::lerp(140.0f, 38.0f, intensity));
        simulatedJitterIntervalMs = randInt(intervalMin, intervalMax);
        simulatedJitterTimer.Start();
    }

    float response = std::lerp(0.12f, 0.72f, intensity);
    simulatedJitterYaw = std::lerp(simulatedJitterYaw, simulatedJitterTargetYaw, response);
    simulatedJitterPitch = std::lerp(simulatedJitterPitch, simulatedJitterTargetPitch, response);
}

void AimAssist::applyAimController(float yawDiff, float pitchDiff) {
    float dt = controllerTimer.Elapsed<std::chrono::duration<float, std::milli>>() / 1000.0f;
    controllerTimer.Start();
    // dt must describe the module cadence, nothing else. Anything long enough to
    // be a stall (or a timer left running through an early-out) collapses to one
    // loop period: a large dt makes alpha jump toward 1.0 and the whole remaining
    // error gets applied in a single update, which is exactly the visible snap.
    constexpr float kLoopPeriodSec = 0.002f;  // ModuleManager cadence
    if (!(dt > 0.0001f) || dt > 0.02f)
        dt = kLoopPeriodSec;

    const float speed = std::clamp(settings.speed, 1.0f, 120.0f);
    float rate = 3.0f + (speed / 120.0f) * 13.0f;
    if (settings.aimMode == AimMode::ADAPTIVE) {
        const float error = std::sqrt(yawDiff * yawDiff + pitchDiff * pitchDiff);
        rate *= std::clamp(0.75f + error / 90.0f, 0.75f, 1.55f);
    } else if (settings.aimMode == AimMode::BLATANT) {
        rate = 18.0f + (speed / 120.0f) * 10.0f;
    }

    // Exponential response is frame-rate independent: the module may run every
    // 2 ms, but the same time constant is used on every machine.
    const float alpha = 1.0f - std::exp(-rate * dt);
    float yawStep = yawDiff * alpha;
    float pitchStep = settings.horizontalOnly ? 0.0f : pitchDiff * alpha;
    if (std::abs(yawDiff) < 0.03f)
        yawStep = yawDiff;
    if (std::abs(pitchDiff) < 0.03f)
        pitchStep = pitchDiff;

    updateSimulatedJitter();
    if (settings.packetRotation) {
        const float gcd = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(mc.getMouseSensitivity());
        // Whole mouse steps, because the sent rotation has to stay on the same grid the
        // rendered one is on -- an offset that is not a multiple of the step is visible
        // to a gcd check even though the rotation itself looks fine.
        const auto steps = [gcd](float degrees) { return std::round(degrees / gcd); };
        // ...and no more steps than the target's angular size can absorb. Past that the
        // offset is not jitter any more, it is a rotation the server raytraces off the
        // hitbox while the camera sits dead centre on it, and the miss is invisible from
        // the player's side. A budget under one step means the target is too small at
        // this range to afford any offset, so the true rotation goes out unmodified.
        const float yawCap = std::floor(packetYawBudget_.load(std::memory_order_acquire) / gcd);
        const float pitchCap = std::floor(packetPitchBudget_.load(std::memory_order_acquire) / gcd);
        const float yawSteps = std::clamp(steps(simulatedJitterYaw), -yawCap, yawCap);
        const float pitchSteps =
            settings.horizontalOnly ? 0.0f : std::clamp(steps(simulatedJitterPitch), -pitchCap, pitchCap);
        serverYawOffset_.store(yawSteps * gcd, std::memory_order_relaxed);
        serverPitchOffset_.store(pitchSteps * gcd, std::memory_order_relaxed);
        hasServerRotation_.store(true, std::memory_order_release);
    }

    // Fold the jitter in as a bounded offset. `desired` is what the rotation
    // should currently carry (nothing at all while the jitter rides on the packet
    // instead, or on pitch in horizontal-only), and only the difference against
    // what was already applied becomes a step. That keeps the offset at exactly
    // +-amplitude no matter how often this runs, instead of accumulating it once
    // per loop into a drift the tracking error has to fight.
    const float desiredJitterYaw = settings.packetRotation ? 0.0f : simulatedJitterYaw;
    const float desiredJitterPitch =
        (settings.packetRotation || settings.horizontalOnly) ? 0.0f : simulatedJitterPitch;
    yawStep += desiredJitterYaw - appliedJitterYaw_;
    pitchStep += desiredJitterPitch - appliedJitterPitch_;
    appliedJitterYaw_ = desiredJitterYaw;
    appliedJitterPitch_ = desiredJitterPitch;

    // Quantize what actually reaches the rotation onto the mouse GCD grid.
    //
    // A real mouse reports whole counts, so every rotation a legitimate client can
    // produce is an exact multiple of (sens * 0.6 + 0.2)^3 * 1.2. This controller
    // runs every ~2 ms and its raw step is a small fraction of one such unit, so
    // writing it straight through gave the server a pitch delta no mouse could ever
    // send — a standing gcd violation, and worst on pitch, where tracking steps are
    // small enough that the off-grid remainder dominates them.
    //
    // The residual carries the leftover fraction, so sub-step motion accumulates
    // until it is worth a whole step rather than being rounded away. Frames that owe
    // nothing write nothing, which is also what a real mouse does.
    const float gcdNow = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(cachedMouseSensitivity);
    g_cachedGCD = gcdNow;
    yawRotationResidual += yawStep;
    pitchRotationResidual += pitchStep;
    float yawApply = applyGCDStrict(static_cast<float>(yawRotationResidual), gcdNow);
    float pitchApply = applyGCDStrict(static_cast<float>(pitchRotationResidual), gcdNow);
    varyStepCounts(yawApply, pitchApply, gcdNow, rng);
    yawRotationResidual -= yawApply;
    pitchRotationResidual -= pitchApply;

    if (std::abs(yawApply) > 0.0001f) {
        const float currentYaw = player.getYaw();
        const float nextYaw = currentYaw + yawApply;
        player.setYaw(nextYaw);
        player.setPrevYaw(std::lerp(currentYaw, nextYaw, 0.82f));
        markRotationWrite();
    }
    if (!settings.horizontalOnly && std::abs(pitchApply) > 0.0001f) {
        const float currentPitch = player.getPitch();
        // Stop just shy of straight up/down, and only ever on a whole step.
        const float nextPitch = limitPitchOnGrid(currentPitch + pitchApply, gcdNow);
        pitchApply = nextPitch - currentPitch;
        if (std::abs(pitchApply) > 0.0001f) {
            player.setPitch(nextPitch);
            player.setPrevPitch(std::lerp(currentPitch, nextPitch, 0.82f));
            markRotationWrite();
        }
    }
}

void AimAssist::onOutgoingPacket(JNIEnv* env, jobject packet) {
    if (!enabled || !settings.packetRotation || !env)
        return;
    if (!hasServerRotation_.load(std::memory_order_acquire))
        return;

    resolveAimC03(env);
    if (!g_aimC03 || !env->IsInstanceOf(packet, g_aimC03))
        return;

    const float offY = serverYawOffset_.load(std::memory_order_relaxed);
    const float offP = serverPitchOffset_.load(std::memory_order_relaxed);

    // Compute the rotation we are about to send (render rotation + jitter offset).
    float finalYaw = 0.0f, finalPitch = 0.0f;
    bool haveYaw = g_aim_fid_yaw != nullptr;
    bool havePitch = g_aim_fid_pitch != nullptr && !settings.horizontalOnly;
    if (haveYaw)
        finalYaw = env->GetFloatField(packet, g_aim_fid_yaw) + offY;
    if (havePitch) {
        const float raw = env->GetFloatField(packet, g_aim_fid_pitch) + offP;
        finalPitch = limitPitchOnGrid(
            raw, g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(cachedMouseSensitivity));
    }

    // Anti-duplicate (Intave AimDuplicateLook360). We force rotating=true on every
    // packet, so two consecutive look packets that carry byte-identical yaw+pitch
    // read as a "duplicate rotating look" and flag. That happens when the render
    // rotation is static (locked on a stationary target) AND the quantized jitter
    // offset lands on the same value two ticks running (guaranteed when jitter=0).
    // A real mouse never repeats a rotation exactly while the hand is moving, so
    // nudge the SENT rotation by one alternating GCD step to guarantee a fresh
    // value. The nudge stays on the mouse GCD grid (no gcd-mismatch flag) and only
    // touches the packet, so the first-person camera stays smooth. Network-thread
    // only, so plain statics are safe here.
    static float s_lastSentYaw = 0.0f;
    static float s_lastSentPitch = 0.0f;
    static bool s_haveLastSent = false;
    static float s_ditherSign = 1.0f;

    if (s_haveLastSent && haveYaw) {
        bool yawDup = std::abs(finalYaw - s_lastSentYaw) < 1e-4f;
        bool pitchDup = havePitch ? (std::abs(finalPitch - s_lastSentPitch) < 1e-4f) : true;
        if (yawDup && pitchDup) {
            float gcd = g_cachedGCD > 0.0001f ? g_cachedGCD : calculateGCD(cachedMouseSensitivity);
            if (gcd < 0.0001f)
                gcd = 0.15f;
            // Yaw alone is enough to make the pair fresh, and yaw alone is what a hand
            // resting on a mouse actually produces. Stepping both axes by the same signed
            // amount every time would make them perfectly correlated, which is a pattern
            // in its own right.
            //
            // Direction is chosen so the step lands back inside the hitbox budget rather
            // than always alternating: at the far edge of the box the outward step is the
            // one that turns a hit into "attacked a player out of sight", and stepping
            // inward makes the rotation just as fresh. Alternation is only the tiebreak
            // when both directions are safe.
            const float budget = packetYawBudget_.load(std::memory_order_acquire);
            float sign = s_ditherSign;
            if (budget > 0.0f && std::abs(offY + sign * gcd) > budget &&
                std::abs(offY - sign * gcd) <= budget)
                sign = -sign;
            finalYaw += sign * gcd;
            s_ditherSign = -sign;
        }
    }

    if (haveYaw)
        env->SetFloatField(packet, g_aim_fid_yaw, finalYaw);
    if (havePitch)
        env->SetFloatField(packet, g_aim_fid_pitch, finalPitch);
    // Mark the packet as carrying a rotation so the server accepts the values.
    if (g_aim_fid_rotating)
        env->SetBooleanField(packet, g_aim_fid_rotating, JNI_TRUE);

    s_lastSentYaw = finalYaw;
    if (havePitch)
        s_lastSentPitch = finalPitch;
    s_haveLastSent = true;

    if (env->ExceptionCheck())
        env->ExceptionClear();
}

bool AimAssist::isHoldingWeapon() {
    int id = player.getHeldItemId();
    if (id == -1)
        return false;

    // Swords
    if (id == ItemIds::WOOD_SWORD || id == ItemIds::STONE_SWORD || id == ItemIds::IRON_SWORD ||
        id == ItemIds::DIAMOND_SWORD || id == ItemIds::GOLD_SWORD)
        return true;

    // Axes
    if (id == ItemIds::WOOD_AXE || id == ItemIds::STONE_AXE || id == ItemIds::IRON_AXE || id == ItemIds::DIAMOND_AXE ||
        id == ItemIds::GOLD_AXE)
        return true;

    return false;
}

bool AimAssist::getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ) {
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();
    float localYaw = player.getYaw();

    // Prefer precise hitbox data from PlayerInfo when available
    if (targetHasHitbox) {
        double minX = targetHitboxMinX;
        double maxX = targetHitboxMaxX;
        double minY = targetHitboxMinY;
        double maxY = targetHitboxMaxY;
        double minZ = targetHitboxMinZ;
        double maxZ = targetHitboxMaxZ;
        double height = maxY - minY;

        // AABB values are absolute world coordinates. If a client mapping or
        // stale entity snapshot supplies a box in a different origin, rebuild
        // its offsets around the current feet position instead of aiming at a
        // detached box (which looks like a nearby block lock).
        const double boxCenterX = (minX + maxX) * 0.5;
        const double boxCenterZ = (minZ + maxZ) * 0.5;
        if (std::abs(boxCenterX - targetX) > 1.5 || std::abs(boxCenterZ - targetZ) > 1.5) {
            const double halfW = std::clamp((maxX - minX) * 0.5, 0.25, 0.45);
            const double halfD = std::clamp((maxZ - minZ) * 0.5, 0.25, 0.45);
            minX = targetX - halfW;
            maxX = targetX + halfW;
            minZ = targetZ - halfD;
            maxZ = targetZ + halfD;
        }
        if (std::abs(minY - targetY) > 1.5 || maxY < targetY - 0.25) {
            minY = targetY;
            maxY = targetY + std::clamp(height, 1.5, 2.0);
            height = maxY - minY;
        }

        // Publish the repaired box. Whatever offsets the aim after this point has to
        // stay inside it, and the repair above is the only place that knows which of
        // the raw AABB and the rebuilt one actually got used.
        resolvedBox_ = {minX, minY, minZ, maxX, maxY, maxZ, true};

        if (settings.aimAtClosestPoint) {
            aimX = std::clamp(localX, minX, maxX);
            // Closest-point selection is horizontal only. Using the local eye
            // height vertically biases targets toward maxY (the head) whenever
            // the player is above them. Keep pitch on a stable body point.
            aimY = minY + height * 0.5;
            aimZ = std::clamp(localZ, minZ, maxZ);
            // Point-blank: if we're horizontally inside the hitbox the clamp
            // collapses the aim point onto our own position and the yaw becomes
            // undefined (atan2(0,0)), so the camera stops turning. Fall back to the
            // hitbox centre so it keeps tracking at very close range.
            double hdx = aimX - localX, hdz = aimZ - localZ;
            if (hdx * hdx + hdz * hdz < 0.02) {
                aimX = (minX + maxX) * 0.5;
                aimZ = (minZ + maxZ) * 0.5;
            }
            return true;
        }

        // Bones map (xR,yR,zR) in [0,1] to a point inside the AABB.
        int boneIdx = settings.aimBone;
        if (boneIdx < 0 || boneIdx >= kBoneCount)
            boneIdx = 0;
        const Bone& bone = kBones[boneIdx];
        double width = maxX - minX;
        double depth = maxZ - minZ;
        aimX = minX + width * bone.xRatio;
        aimZ = minZ + depth * bone.zRatio;
        aimY = height > 0.01 ? minY + height * bone.yRatio : targetY;
        (void)localYaw;  // no adaptive lateral nudge — aim stays exactly on the bone
        return true;
    }

    constexpr double HALF_WIDTH = 0.3;

    // No AABB arrived, so assume the vanilla player box around the feet position.
    resolvedBox_ = {targetX - HALF_WIDTH, targetY,      targetZ - HALF_WIDTH,
                    targetX + HALF_WIDTH, targetY + 1.8, targetZ + HALF_WIDTH,
                    true};

    if (settings.aimAtClosestPoint) {
        double minX = targetX - HALF_WIDTH;
        double maxX = targetX + HALF_WIDTH;
        // PlayerInfo.posY is the feet position. Build a normal player box and
        // aim at its chest when no precise AABB was available.
        double minY = targetY;
        double maxY = targetY + 1.8;
        double minZ = targetZ - HALF_WIDTH;
        double maxZ = targetZ + HALF_WIDTH;

        aimX = std::clamp(localX, minX, maxX);
        aimY = std::clamp(localY + (player.isSneaking() ? 1.54 : 1.62), minY + 0.3, maxY - 0.2);
        aimZ = std::clamp(localZ, minZ, maxZ);
        return true;
    } else {
        // Use one stable chest point. The former corner/FOV rejection could
        // return false as the target crossed a corner, causing reacquisition
        // oscillation and leaving the camera pointed at the last floor point.
        aimX = targetX;
        aimY = targetY + 0.9;
        aimZ = targetZ;
        return true;
    }
}

void AimAssist::onRenderMenu() {
    static const char* targetModes[] = {"Lowest Health", "Closest to Player", "Closest to Crosshair", "Switch"};
    static const char* sortByModes[] = {"Distance", "Health", "FOV", "Hurt Time"};
    int currentMode = static_cast<int>(settings.targetMode);
    int currentSortBy = static_cast<int>(settings.switchSortBy);

    bool enabledValue = enabled;
    ui::two_column(
        "Settings",
        [this, &currentMode, &currentSortBy, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();
            static const char* aimModes[] = {"Smooth", "Adaptive", "Blatant"};
            int am = static_cast<int>(settings.aimMode);
            if (ui::Combo("Mode", &am, aimModes, IM_ARRAYSIZE(aimModes)))
                settings.aimMode = static_cast<AimMode>(am);

            if (settings.aimMode == AimMode::BLATANT) {
                ImGui::TextDisabled("Fast lock with bounded smoothing");
            } else {
                if (settings.aimMode == AimMode::ADAPTIVE)
                    ImGui::TextDisabled("Speed auto-tunes to error, distance & motion");
                else
                    ui::SliderFloat("Speed", &settings.speed, 1.0f, 120.0f, "%.0f", true);
                ui::SliderInt("FOV", &settings.fov, 10, 360, "%d");
                ui::SliderFloat("Distance", &settings.distance, 3.0f, 8.0f, "%.1f");
                ui::SliderFloat("Prediction", &settings.prediction, 0.0f, 100.0f, "%.0f%%");
                ui::SliderInt("Reaction Time", &settings.reactionTime, 0, 200, "%d ms");
                ui::SliderFloat("Jitter", &settings.jitterPercent, 0.0f, 100.0f, "%.0f%%");
                ui::Checkbox("Smooth Camera", &settings.packetRotation);
                if (settings.packetRotation)
                    ImGui::TextDisabled("Jitter only on sent packet (1st person smooth)");
            }
        },
        "Options",
        [this, &currentMode, &currentSortBy]() {
            ui::Checkbox("Horizontal Only", &settings.horizontalOnly);
            ui::Checkbox("Always Aim", &settings.alwaysAim);
            ui::Checkbox("Break Blocks", &settings.breakBlocks);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            ui::Checkbox("Aim Closest Point", &settings.aimAtClosestPoint);
            if (!settings.aimAtClosestPoint) {
                const char* boneNames[kBoneCount];
                for (int i = 0; i < kBoneCount; ++i)
                    boneNames[i] = kBones[i].name;
                int boneIdx = std::clamp(settings.aimBone, 0, kBoneCount - 1);
                if (ui::Combo("Bone", &boneIdx, boneNames, kBoneCount))
                    settings.aimBone = boneIdx;
            }
            ui::Checkbox("Target Invisibles", &settings.invisibles);
            ui::Checkbox("Stop On Hitbox", &settings.stopOnHitbox);
            bool entityLock = entityLockEnabled_.load(std::memory_order_acquire);
            if (ui::Checkbox("Entity Lock", &entityLock))
                setEntityLock(entityLock);
            ImGui::Spacing();
            if (ui::Combo("Target Mode", &currentMode, targetModes, IM_ARRAYSIZE(targetModes))) {
                settings.targetMode = static_cast<TargetMode>(currentMode);
            }
            // Show Sort By option only when Switch mode is selected
            if (settings.targetMode == TargetMode::SWITCH) {
                if (ui::Combo("Sort By", &currentSortBy, sortByModes, IM_ARRAYSIZE(sortByModes))) {
                    settings.switchSortBy = static_cast<SortBy>(currentSortBy);
                }
                // Show Lock option only when Hurt Time is selected
                if (settings.switchSortBy == SortBy::HURT_TIME) {
                    ui::Checkbox("Lock", &settings.hurtTimeLock);
                    if (settings.hurtTimeLock) {
                        ImGui::TextDisabled("Instant aim on target switch");
                    } else {
                        ImGui::TextDisabled("Targets least recently hit player");
                    }
                }
            }
            ImGui::Spacing();
            // Armor Filter (multi-select): check every tier you want to target.
            // A player is aimed at if they wear ANY of the checked tiers.
            ImGui::Text("Armor Filter (target tiers):");
            auto tierCheckbox = [this](const char* label, int bit) {
                bool on = (settings.armorTierMask & bit) != 0;
                if (ui::Checkbox(label, &on)) {
                    if (on) settings.armorTierMask |= bit;
                    else settings.armorTierMask &= ~bit;
                }
            };
            tierCheckbox("No Armor", ARMOR_NO_ARMOR);
            tierCheckbox("Leather", ARMOR_LEATHER);
            tierCheckbox("Chainmail", ARMOR_CHAIN);
            tierCheckbox("Iron", ARMOR_IRON);
            tierCheckbox("Gold", ARMOR_GOLD);
            tierCheckbox("Diamond", ARMOR_DIAMOND);
            if (settings.armorTierMask == 0) {
                ImGui::TextDisabled("No tiers checked - targets everyone");
            } else {
                ImGui::TextDisabled("Targets players matching any checked tier");

                // Optional key to toggle the armor filter on/off in-game.
                ui::Binder("Armor Filter Key", &settings.armorFilterKeybind);
                if (settings.armorFilterKeybind != 0)
                    ImGui::TextDisabled("Filter is currently %s",
                                        settings.armorFilterEnabled ? "ON" : "OFF");
            }
        });
}

std::string AimAssist::getDisplayInfo() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", settings.speed);
    return buf;
}



void AimAssist::saveSettings(json::Value& node) const {
    node["horizontalOnly"] = settings.horizontalOnly;
    node["speed"] = settings.speed;
    node["fov"] = settings.fov;
    node["distance"] = settings.distance;
    node["reactionTime"] = settings.reactionTime;
    node["aimMode"] = static_cast<int>(settings.aimMode);
    node["targetMode"] = static_cast<int>(settings.targetMode);
    node["switchSortBy"] = static_cast<int>(settings.switchSortBy);
    node["hurtTimeLock"] = settings.hurtTimeLock;
    node["breakBlocks"] = settings.breakBlocks;
    node["weaponOnly"] = settings.weaponOnly;
    node["aimAtClosestPoint"] = settings.aimAtClosestPoint;
    node["aimBone"] = settings.aimBone;
    node["alwaysAim"] = settings.alwaysAim;
    node["invisibles"] = settings.invisibles;
    node["entityLock"] = settings.entityLock;
    node["stopOnHitbox"] = settings.stopOnHitbox;
    node["jitterPercent"] = settings.jitterPercent;
    node["prediction"] = settings.prediction;
    node["packetRotation"] = settings.packetRotation;
    node["armorFilter"] = static_cast<int>(settings.armorFilter);
    node["armorTierMask"] = settings.armorTierMask;
    node["armorFilterEnabled"] = settings.armorFilterEnabled;
    node["armorFilterKeybind"] = settings.armorFilterKeybind;
}

void AimAssist::loadSettings(const json::Value& node) {
    settings.horizontalOnly = node["horizontalOnly"].as_bool(settings.horizontalOnly);
    settings.speed = std::clamp(node["speed"].as_float(settings.speed), 1.0f, 120.0f);
    settings.fov = node["fov"].as_int(settings.fov);
    settings.distance = node["distance"].as_float(settings.distance);
    settings.reactionTime = node["reactionTime"].as_int(settings.reactionTime);
    settings.aimMode = static_cast<AimMode>(node["aimMode"].as_int(static_cast<int>(settings.aimMode)));
    settings.targetMode =
        static_cast<TargetMode>(node["targetMode"].as_int(static_cast<int>(settings.targetMode)));
    settings.switchSortBy =
        static_cast<SortBy>(node["switchSortBy"].as_int(static_cast<int>(settings.switchSortBy)));
    settings.hurtTimeLock = node["hurtTimeLock"].as_bool(settings.hurtTimeLock);
    settings.breakBlocks = node["breakBlocks"].as_bool(settings.breakBlocks);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    settings.aimAtClosestPoint = node["aimAtClosestPoint"].as_bool(settings.aimAtClosestPoint);
    settings.aimBone = std::clamp(node["aimBone"].as_int(settings.aimBone), 0, kBoneCount - 1);
    settings.alwaysAim = node["alwaysAim"].as_bool(settings.alwaysAim);
    settings.invisibles = node["invisibles"].as_bool(settings.invisibles);
    // Legacy "lockAim" was renamed to "entityLock"; honor either key so old
    // configs keep working. setEntityLock() also flips the atomic flag used
    // by the runtime.
    bool entityLock = node.has("entityLock") ? node["entityLock"].as_bool(settings.entityLock)
                                             : node["lockAim"].as_bool(settings.entityLock);
    setEntityLock(entityLock);
    settings.stopOnHitbox = node["stopOnHitbox"].as_bool(settings.stopOnHitbox);
    settings.jitterPercent =
        (std::clamp)(node["jitterPercent"].as_float(settings.jitterPercent), 0.0f, 100.0f);
    settings.prediction = (std::clamp)(node["prediction"].as_float(settings.prediction), 0.0f, 100.0f);
    settings.packetRotation = node["packetRotation"].as_bool(settings.packetRotation);
    // "antiBot" is still present in older configs and is intentionally ignored: the AntiBot
    // module owns that state now and carries its own node.
    settings.armorFilter =
        static_cast<ArmorFilter>(node["armorFilter"].as_int(static_cast<int>(settings.armorFilter)));
    if (node.has("armorTierMask")) {
        settings.armorTierMask = node["armorTierMask"].as_int(settings.armorTierMask);
    } else {
        // Migrate legacy single-select enum into the new multi-select bitmask.
        switch (settings.armorFilter) {
            case ArmorFilter::NONE:      settings.armorTierMask = ARMOR_NO_ARMOR; break;
            case ArmorFilter::LEATHER:   settings.armorTierMask = ARMOR_LEATHER;  break;
            case ArmorFilter::CHAINMAIL: settings.armorTierMask = ARMOR_CHAIN;    break;
            case ArmorFilter::IRON:      settings.armorTierMask = ARMOR_IRON;     break;
            case ArmorFilter::GOLD:      settings.armorTierMask = ARMOR_GOLD;     break;
            case ArmorFilter::DIAMOND:   settings.armorTierMask = ARMOR_DIAMOND;  break;
            default:                     settings.armorTierMask = 0;              break;
        }
    }
    settings.armorFilterEnabled = node["armorFilterEnabled"].as_bool(settings.armorFilterEnabled);
    settings.armorFilterKeybind = node["armorFilterKeybind"].as_int(settings.armorFilterKeybind);
}
