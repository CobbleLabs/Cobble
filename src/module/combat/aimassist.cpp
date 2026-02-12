#include "aimassist.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../../util/logger.hpp"
#include "../../util/team/friends_manager.h"

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

// Cached JNI lookups for fast-path position reads
static jclass g_entityCls = nullptr;
static jfieldID g_fid_posX = nullptr;
static jfieldID g_fid_posY = nullptr;
static jfieldID g_fid_posZ = nullptr;

static void ensure_entity_fields(jobject sample) {
    if (g_entityCls || !sample)
        return;
    jclass cls = Java::Env()->GetObjectClass(sample);
    if (!cls)
        return;
    g_entityCls = (jclass)Java::Env()->NewGlobalRef(cls);
    Java::Env()->DeleteLocalRef(cls);
    if (!g_entityCls)
        return;
    g_fid_posX = Java::Env()->GetFieldID(g_entityCls, "posX", "D");
    g_fid_posY = Java::Env()->GetFieldID(g_entityCls, "posY", "D");
    g_fid_posZ = Java::Env()->GetFieldID(g_entityCls, "posZ", "D");
}

static void cleanup_entity_fields() {
    if (g_entityCls) {
        Java::Env()->DeleteGlobalRef(g_entityCls);
        g_entityCls = nullptr;
    }
    g_fid_posX = g_fid_posY = g_fid_posZ = nullptr;
}

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

// Calculate floor divisor (what AC uses to detect asymmetry)
static float getFloorDivisor(float delta, float gcd) {
    if (gcd < 0.0001f || std::abs(delta) < 0.0001f)
        return 0.0f;
    float divided = delta / gcd;
    return std::abs(std::round(divided) - divided);
}

// Balance floor divisors between axes to prevent AimAssistH/I detection
// AC flags when one axis is perfectly aligned (< 1e-4) while other isn't (> 0.03)
static void balanceFloorDivisors(float& yawDelta, float& pitchDelta, float gcd, std::mt19937& rng) {
    float yawDivisor = getFloorDivisor(yawDelta, gcd);
    float pitchDivisor = getFloorDivisor(pitchDelta, gcd);

    constexpr float PERFECT_THRESHOLD = 1e-4f;
    constexpr float OFFSET_THRESHOLD = 0.03f;

    std::uniform_real_distribution<float> microOffset(0.008f, 0.025f);

    // If yaw is too perfect while pitch isn't, add micro-offset to yaw
    if (yawDivisor < PERFECT_THRESHOLD && pitchDivisor > OFFSET_THRESHOLD) {
        float offset = microOffset(rng) * gcd;
        yawDelta += (yawDelta >= 0 ? offset : -offset);
    }
    // If pitch is too perfect while yaw isn't, add micro-offset to pitch
    else if (pitchDivisor < PERFECT_THRESHOLD && yawDivisor > OFFSET_THRESHOLD) {
        float offset = microOffset(rng) * gcd;
        pitchDelta += (pitchDelta >= 0 ? offset : -offset);
    }
    // If both are too perfect, add small offset to both (prevents AimAssistJ detection)
    else if (yawDivisor < PERFECT_THRESHOLD && pitchDivisor < PERFECT_THRESHOLD) {
        if (std::abs(yawDelta) > 0.5f || std::abs(pitchDelta) > 0.5f) {
            float offset = microOffset(rng) * gcd * 0.5f;
            yawDelta += offset;
            pitchDelta += offset;
        }
    }
}

// Tick skip with Poisson-like distribution (not periodic)
static bool shouldSkipTick(std::mt19937& rng) {
    // Exponential inter-arrival time instead of periodic counter
    static float nextSkipTime = 0.f;
    static Timer skipTimer;

    if (skipTimer.Elapsed<>() > nextSkipTime) {
        // Exponential distribution with mean ~150ms between skips
        std::exponential_distribution<float> exp(1.0f / 150.0f);
        nextSkipTime = std::clamp(exp(rng), 50.f, 500.f);
        skipTimer.Start();
        return true;
    }
    return false;
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
}

void AimAssist::onEnable() {
    log_ok("AimAssist enabled");

    // CRITICAL: Clean up any stale JNI references from previous session
    // This fixes the intermittent "no target detected" bug after game state changes
    if (cachedTarget) {
        if (Java::Env() && !Java::Env()->IsSameObject(cachedTarget, NULL)) {
            Java::Env()->DeleteGlobalRef(cachedTarget);
        }
        cachedTarget = nullptr;
    }
    cleanup_entity_fields();  // Reset cached field IDs to force refresh

    hasLockedTarget = false;
    pitchdiffSpeed = 0.f;
    yawdiffSpeed = 0.f;
    cursorAccumY = 0.0;
    targetYawSpeed = 0.f;
    targetPitchSpeed = 0.f;
    reactionPrimed = false;
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
}

void AimAssist::onDisable() {
    log_ok("AimAssist disabled");
    hasLockedTarget = false;

    if (cachedTarget) {
        Java::Env()->DeleteGlobalRef(cachedTarget);
        cachedTarget = nullptr;
    }
    cleanup_entity_fields();
    cursorAccumY = 0.0;
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
    clickStartedThisFrame = false;
    if (!player.isValid())
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;

    bool mouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (!mouseDown && settings.lockAim) {
        if (cachedTarget) {
            Java::Env()->DeleteGlobalRef(cachedTarget);
            cachedTarget = nullptr;
        }
        hasLockedTarget = false;
    }

    if (!settings.alwaysAim && !mouseDown) {
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
            return;
        }
    }

    // Weapon-only check
    if (settings.weaponOnly && !isHoldingWeapon()) {
        return;
    }

    float localYaw = player.getYaw();
    float localPitch = player.getPitch();
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();
    targetChangedThisFrame = false;

    bool getNewTarget = true;

    // Switch mode: ALWAYS get a new target (evaluates every frame)
    // Other modes: Check if locked target is still valid
    if (settings.targetMode == TargetMode::SWITCH) {
        getNewTarget = true;  // Always evaluate for better target
    } else if (settings.lockAim && hasLockedTarget) {
        float yawToTarget = getAngles(localX, localY, localZ, lockedTargetX, lockedTargetY, lockedTargetZ).first;
        float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));
        double dist = std::sqrt((lockedTargetX - localX) * (lockedTargetX - localX) +
                                (lockedTargetY - localY) * (lockedTargetY - localY) +
                                (lockedTargetZ - localZ) * (lockedTargetZ - localZ));

        if (yawDiff <= settings.fov && dist <= settings.distance) {
            getNewTarget = false;
        }
    }

    double targetX = lockedTargetX;
    double targetY = lockedTargetY;
    double targetZ = lockedTargetZ;

    hasLockedTarget = false;

    // HYBRID LOOP LOGIC
    // 1. If we have a cached GlobalRef target (cachedTarget), update its position FAST (every frame).
    // 2. If it is valid and in range: use it.
    // 3. If invalid or timer expired: run findTarget (slow scan).
    bool validCache = false;
    bool forceScan = false;

    if (cachedTarget) {
        if (Java::Env()->IsSameObject(cachedTarget, NULL)) {
            Java::Env()->DeleteGlobalRef(cachedTarget);
            cachedTarget = nullptr;
        } else {
            ensure_entity_fields(cachedTarget);

            // Update position from cached target (Fast JNI)
            if (!g_fid_posX || !g_fid_posY || !g_fid_posZ) {
                Java::Env()->ExceptionClear();
                Java::Env()->DeleteGlobalRef(cachedTarget);
                cachedTarget = nullptr;
                forceScan = true;
            } else {
                double tX = Java::Env()->GetDoubleField(cachedTarget, g_fid_posX);
                double tY = Java::Env()->GetDoubleField(cachedTarget, g_fid_posY);
                double tZ = Java::Env()->GetDoubleField(cachedTarget, g_fid_posZ);

                if (Java::Env()->ExceptionCheck()) {
                    Java::Env()->ExceptionClear();
                    Java::Env()->DeleteGlobalRef(cachedTarget);
                    cachedTarget = nullptr;
                    forceScan = true;
                } else {
                    // Validate distance/etc slightly? (Optional fast check)
                    // For now trust it until slow scan overrides it.
                    targetX = tX;
                    targetY = tY;
                    targetZ = tZ;
                    validCache = true;
                    hasLockedTarget = true;
                }
            }
        }
    }

    // Run slow scan if needed (or if we have no target)
    // getNewTarget variable from above logic helps decide if we WANT a new target.
    // findTarget now handles the 25ms rate limiting internally.

    if (forceScan) {
        validCache = false;
    }

    if (!validCache || getNewTarget) {
        // findTarget will return true ONLY if it updated the target (slow scan or cache hit inside it).
        // But since we moved cache logic here, findTarget should mostly be for SCANNING.
        // Let's rely on findTarget for the heavy lifting still, but use the persistence we added here.

        if (findTarget(targetX, targetY, targetZ)) {
            hasLockedTarget = true;
            if (!prevFrameHadTarget || targetChangedThisFrame) {
                targetRampTimer.Start();  // ramp up speed when acquiring a target
                yawdiffSpeed = 0.f;
                pitchdiffSpeed = 0.f;
                reactionPrimed = false;
                targetSpeedVariance = randFloat(0.96f, 1.04f);
                aimHumanizer = randFloat(0.94f, 1.06f);
                hitboxRefreshTimer.Start();
            }
        }
    }

    if (!hasLockedTarget) {
        prevFrameHadTarget = false;
        lastMouseDown = mouseDown;
        return;
    }

    lockedTargetX = targetX;
    lockedTargetY = targetY;
    lockedTargetZ = targetZ;

    double aimTargetX = targetX;
    double aimTargetY = targetY;
    double aimTargetZ = targetZ;

    // When hitbox is unavailable (common with invisible players), aim at eye height
    // Eye height is ~1.62 blocks above foot position (1.54 when sneaking)
    if (!targetHasHitbox) {
        aimTargetY += 1.62;
    }

    double aimX, aimY, aimZ;
    if (!getAimPoint(aimTargetX, aimTargetY, aimTargetZ, aimX, aimY, aimZ)) {
        return;
    }

    auto [yawTarget, pitchTarget] = getAngles(localX, localY, localZ, aimX, aimY, aimZ);

    float yawDiff = wrapTo180(-(localYaw - yawTarget));
    float pitchDiff = wrapTo180(-(localPitch - pitchTarget));

    // Stop On Hitbox: If crosshair is already inside the hitbox, let user aim freely
    if (settings.stopOnHitbox && targetHasHitbox) {
        float hitboxWidth = targetHitboxMaxX - targetHitboxMinX;
        float hitboxHeight = targetHitboxMaxY - targetHitboxMinY;

        if (hitboxWidth > 0.01f && hitboxHeight > 0.01f) {
            // Calculate angles to hitbox edges
            float yawToMinX = getAngles(localX, localY, localZ, targetHitboxMinX, aimY, targetHitboxMinZ).first;
            float yawToMaxX = getAngles(localX, localY, localZ, targetHitboxMaxX, aimY, targetHitboxMaxZ).first;
            float pitchToMinY = getAngles(localX, localY, localZ, aimX, targetHitboxMinY, aimZ).second;
            float pitchToMaxY = getAngles(localX, localY, localZ, aimX, targetHitboxMaxY, aimZ).second;

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
                return;
            }
        }
    }

    // Hurt Time Lock: Instant aim when switching targets
    if (settings.targetMode == TargetMode::SWITCH && settings.switchSortBy == SortBy::HURT_TIME &&
        settings.hurtTimeLock && targetChangedThisFrame) {
        // Apply instant aim - skip all smoothing
        player.setYaw(yawTarget);
        player.setPrevYaw(yawTarget);
        if (!settings.horizontalOnly) {
            float clampedPitch = std::clamp(pitchTarget, -90.0f, 90.0f);
            player.setPitch(clampedPitch);
            player.setPrevPitch(clampedPitch);
        }
        prevFrameHadTarget = hasLockedTarget;
        lastMouseDown = mouseDown;
        return;
    }

    float speedSetting = std::max(settings.speed, 1.0f);
    float speed = speedSetting * 3.0f;
    cachedMouseSensitivity = mc.getMouseSensitivity();

    applyAimRand(yawDiff, pitchDiff, speed);
    prevFrameHadTarget = hasLockedTarget;
    lastMouseDown = mouseDown;
}

bool AimAssist::findTarget(double& targetX, double& targetY, double& targetZ) {
    // HYBRID TRACKING SYSTEM
    // 1. Fast Path (Every Frame): Update position of locked target directly (JNI)
    // 2. Slow Path (Scan Interval): Scan full player list for new/better targets
    static Timer scanTimer;
    JNIEnv* env = Java::Env();

    // CRITICAL: Clear any pending JNI exceptions that could cause silent failures
    // This is a common cause of the "aim assist stops detecting" bug
    if (env && env->ExceptionCheck()) {
        env->ExceptionClear();
        // If there was an exception, also clean up potentially corrupted cache
        if (cachedTarget) {
            env->DeleteGlobalRef(cachedTarget);
            cachedTarget = nullptr;
        }
        cleanup_entity_fields();
        targetHasHitbox = false;
        log_warn("[AimAssist] Cleared JNI exception - cache reset");
    }

    bool wantHitboxRefresh = settings.lockAim && cachedTarget && hitboxRefreshTimer.Elapsed<>() >= hitboxRefreshMs;

    // FAST PATH: If we have a cached target, update its position & validate
    if (cachedTarget && scanTimer.Elapsed<>() < 50 && !wantHitboxRefresh)  // Rescan every 50ms
    {
        // Check validity (basic)
        if (Java::Env()->IsSameObject(cachedTarget, NULL)) {
            Java::Env()->DeleteGlobalRef(cachedTarget);  // Clean up invalid ref
            cachedTarget = nullptr;                      // Invalid
            targetHasHitbox = false;
            prevFrameHadTarget = false;
        } else {
            ensure_entity_fields(cachedTarget);

            if (!g_fid_posX || !g_fid_posY || !g_fid_posZ) {
                Java::Env()->ExceptionClear();
                Java::Env()->DeleteGlobalRef(cachedTarget);
                cachedTarget = nullptr;
                targetHasHitbox = false;
                return false;
            }

            // Update position
            double cX = Java::Env()->GetDoubleField(cachedTarget, g_fid_posX);
            double cY = Java::Env()->GetDoubleField(cachedTarget, g_fid_posY);
            double cZ = Java::Env()->GetDoubleField(cachedTarget, g_fid_posZ);

            if (Java::Env()->ExceptionCheck()) {
                Java::Env()->ExceptionClear();
                Java::Env()->DeleteGlobalRef(cachedTarget);
                cachedTarget = nullptr;
                targetHasHitbox = false;
                prevFrameHadTarget = false;
                return false;
            }

            // Basic distance check to see if we should drop it immediately?
            // For now, assume stickiness for 50ms is fine.

            // If angle error is large, force slow scan to re-acquire smoothly
            float localYaw = player.getYaw();
            double localX = player.getPosX();
            double localY = player.getPosY();
            double localZ = player.getPosZ();
            float yawToTarget = getAngles(localX, localY, localZ, cX, cY, cZ).first;
            float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));
            if (yawDiff > 25.f) {
                return false;  // force slow scan/ramp
            }

            targetX = cX;
            targetY = cY;
            targetZ = cZ;
            return true;
        }
    }

    // SLOW PATH GUARD: If we didn't take the fast path, checks if we are allowed to scan
    if (scanTimer.Elapsed<>() < 50 && !wantHitboxRefresh) {
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
    int lowestHurtTime = INT_MAX;     // For HurtTime sorting (lower = better, 0 = can take damage)
    float bestSwitchScore = FLT_MAX;  // Combined score for Switch mode
    bool found = false;

    jobject bestPlayerObjLocal = nullptr;
    PlayerInfo closestPlayer;
    bool refreshedHitbox = false;

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

        // Refresh stored hitbox for lockAim to avoid stale dimensions
        if (sameAsLocked && settings.lockAim) {
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

        if (dist > settings.distance || dist < 0.2f) {
            continue;
        }
        if (p.isInvisible && !settings.invisibles) {
            continue;
        }
        if (p.isNPC) {
            continue;
        }

        if (friendsManager) {
            bool isApolloTeam = !p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid);
            if (isApolloTeam) {
                continue;
            }

            bool isApolloNametag = !p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid);
            if (isApolloNametag) {
                continue;
            }

            // Priority 3: Manual Friends
            bool isFriendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool isFriendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (isFriendByUUID || isFriendByName) {
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
            bool isScoreboardTeammate = friendsManager->isTeammate(p.uuid, playerColor, localColor);
            if (isScoreboardTeammate) {
                continue;
            }
        }

        float yawToTarget = getAngles(localX, localY, localZ, p.posX, p.posY, p.posZ).first;
        float yawDiff = std::abs(wrapTo180(-(localYaw - yawToTarget)));

        if ((int)yawDiff > settings.fov / 2) {
            continue;
        }

        if (settings.targetMode == TargetMode::DISTANCE) {
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
                    // Lower hurtTime = better (0 means they can take damage now)
                    // Add distance tie-breaker to prevent flickering when multiple players have same hurtTime
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

        if (p.instance && env) {
            env->DeleteLocalRef(p.instance);
        }
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
            // IsSameObject is cheap.
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

void AimAssist::applyAimRand(float yawDiff, float pitchDiff, float speed) {
    float absYawDiff = std::abs(yawDiff);
    float absPitchDiff = std::abs(pitchDiff);

    if (absYawDiff < 1.f && absPitchDiff < 1.f) {
        return;
    }

    float maxDiff = std::max(absYawDiff, absPitchDiff);
    float rawSpeedSetting = std::max(settings.speed, 1.0f);
    float speedNorm = std::clamp(rawSpeedSetting / 120.0f, 0.0f, 3.0f);
    bool highSpeedNoJitter = speedNorm >= 1.6f;
    float baseSpeed = std::max(speed, rawSpeedSetting * 3.0f);
    float jitterRange = (maxDiff < 10.f) ? 0.02f : (maxDiff < 30.f ? 0.05f : 0.08f);
    if (maxDiff < 3.f)
        jitterRange = 0.0f;  // no jitter when already aligned
    if (speedNorm >= 2.0f) {
        jitterRange =
            (maxDiff > 45.f) ? jitterRange * 0.2f : 0.0f;  // keep far sweeps a bit rough, lock-in stays steady
    } else if (speedNorm >= 1.5f) {
        jitterRange *= 0.25f;
    } else {
        float speedJitterScale = 1.0f / (1.0f + std::max(0.0f, speedNorm - 0.5f) * 3.0f);
        jitterRange *= std::clamp(speedJitterScale, 0.15f, 1.0f);
    }
    if (highSpeedNoJitter) {
        jitterRange = 0.0f;
    }
    yawDiff += randFloat(-jitterRange, jitterRange);
    pitchDiff += randFloat(-jitterRange, jitterRange);

    float alignment = std::clamp(std::max(absYawDiff, absPitchDiff) / 90.0f, 0.0f, 1.0f);
    float dynamicSpeed = std::lerp(baseSpeed * 0.35f, baseSpeed, alignment);
    dynamicSpeed *= targetSpeedVariance;

    // Ramp respects both time since acquisition and current angular error (larger error = slower ramp)
    float timeRamp = std::clamp(targetRampTimer.Elapsed<>() / targetRampDurationMs, 0.25f, 1.0f);
    float errorRamp = std::clamp(1.0f - (maxDiff / 90.0f), 0.25f, 1.0f);
    float clickRamp =
        settings.alwaysAim ? 1.0f : std::clamp(clickRampTimer.Elapsed<>() / clickRampDurationMs, 0.3f, 1.0f);
    float ramp = std::min({timeRamp, errorRamp, clickRamp});
    dynamicSpeed *= ramp;

    // Randomize deceleration zone per target (humans are inconsistent)
    static float currentDecelZone = 6.0f;
    if (targetChangedThisFrame) {
        currentDecelZone = randFloat(4.0f, 8.0f);  // Variable zone
    }
    if (maxDiff < currentDecelZone) {
        // Variable exponent for non-uniform deceleration curve
        float decelExponent = randFloat(1.2f, 1.8f);
        float decelFactor = std::pow(maxDiff / currentDecelZone, decelExponent);
        // Apply overshoot tendency from neuromotor state
        decelFactor *= g_neuroState.overshootFactor;
        dynamicSpeed *= std::max(0.2f, decelFactor);
    }

    // Non-periodic skips with exponential inter-arrival times
    if (shouldSkipTick(rng)) {
        return;
    }

    // Update neuromotor state every frame (scaled by speed setting)
    g_neuroState.update(rng, rawSpeedSetting);

    if (rawSpeedSetting >= 120.0f) {
        float extraAggro = std::clamp((rawSpeedSetting - 120.0f) / 200.0f, 0.0f, 1.0f);
        dynamicSpeed *= std::lerp(0.8f, 0.4f, extraAggro);  // dampen extremes to avoid flicks
    } else if (maxDiff < 20.f) {
        dynamicSpeed *= 0.85f;  // slow down near micro-adjust to prevent rebound
    }
    if (speedNorm > 1.6f) {
        float hs = std::clamp((speedNorm - 1.6f) / 1.4f, 0.0f, 1.0f);
        dynamicSpeed *= std::lerp(0.85f, 0.6f, hs);
    }

    if (maxDiff < 5.f && rawSpeedSetting > 150.0f) {
        dynamicSpeed *= 0.5f;
    }

    if (!reactionPrimed || reactionTimeTimer.Elapsed<>() > settings.reactionTime) {
        float pitchBoosts = pitchBoost.Delay() * pitchBoost2.Delay();
        float yawBoosts = yawBoost.Delay() * yawBoost2.Delay();

        if (!settings.horizontalOnly) {
            float desiredPitchSpeed = pitchDiff / (1000.f / (dynamicSpeed * pitchBoosts));
            if (std::abs(desiredPitchSpeed) < 0.03f)
                desiredPitchSpeed = desiredPitchSpeed < 0 ? -0.03f : 0.03f;
            targetPitchSpeed = desiredPitchSpeed;
        } else {
            targetPitchSpeed = 0.f;
        }

        float desiredYawSpeed = yawDiff / (1000.f / (dynamicSpeed * yawBoosts));
        if (std::abs(desiredYawSpeed) < 0.03f)
            desiredYawSpeed = desiredYawSpeed < 0 ? -0.03f : 0.03f;
        targetYawSpeed = desiredYawSpeed;

        reactionPrimed = true;
        reactionTimeTimer.Start();
    }

    float smoothingHuman = std::clamp(aimHumanizer, 0.92f, 1.08f);
    float smoothingStrength =
        std::clamp((0.08f + alignment * 0.18f + (std::min(rawSpeedSetting, 360.0f) / 400.0f) * 0.12f) * smoothingHuman,
                   0.08f, 0.35f);
    yawdiffSpeed = std::lerp(yawdiffSpeed, targetYawSpeed, smoothingStrength);
    pitchdiffSpeed = std::lerp(pitchdiffSpeed, targetPitchSpeed, smoothingStrength);

    static Timer pitchRandTimer;
    static int pitchrand = randInt(-1, 1);

    const int pitchUpdateMsMinLow = 100;
    const int pitchUpdateMsMaxLow = 300;
    const int pitchUpdateMsMinHigh = 400;
    const int pitchUpdateMsMaxHigh = 600;
    static int pitchUpdateMsMin = 100;
    static int pitchUpdateMsMax = 600;

    static int pitchupdatems = randInt(pitchUpdateMsMin, pitchUpdateMsMax);
    static float pitchrandsmooth = 0;
    static int pitchrandbegin = 0;

    if (highSpeedNoJitter) {
        pitchrandsmooth = 0.0f;
    }

    if (absYawDiff > absPitchDiff && absYawDiff > 5.f) {
        if ((int)pitchRandTimer.Elapsed<>() > pitchupdatems) {
            pitchrandbegin = pitchrand;

            float t = (std::clamp(absYawDiff, 10.f, 50.f) - 9.99f) / 40.f;
            pitchUpdateMsMin = (int)std::lerp((float)pitchUpdateMsMinLow, (float)pitchUpdateMsMinHigh, t);
            pitchUpdateMsMax = (int)std::lerp((float)pitchUpdateMsMaxLow, (float)pitchUpdateMsMaxHigh, t);
            int travelDistance = 1;
            pitchupdatems = randInt(pitchUpdateMsMin, pitchUpdateMsMax);

            if (pitchDiff > 0)
                pitchrand = randInt(1, travelDistance);
            else
                pitchrand = randInt(-travelDistance, -1);

            pitchRandTimer.Start();
        }

        pitchrandsmooth = slerp((float)pitchrandbegin, (float)pitchrand, pitchRandTimer.Elapsed<>() / pitchupdatems);
    } else {
        constexpr float partialTick = 1.0f;
        pitchrandsmooth = std::lerp(pitchrandsmooth, 0.f, std::clamp(partialTick * 0.1f, 0.f, 1.f));
    }

    float jitterFade = std::clamp(std::max(absYawDiff, absPitchDiff) / 45.0f, 0.0f, 1.0f);
    float pitchRandScale = 1.0f / (1.0f + std::max(0.0f, speedNorm - 0.8f) * 2.0f);
    if (speedNorm >= 1.5f) {
        pitchrandsmooth = 0.0f;
    } else {
        pitchrandsmooth *= jitterFade * std::clamp(pitchRandScale, 0.25f, 1.0f);
    }

    cursorAccumY += pitchdiffSpeed + pitchrandsmooth;

    // Apply pitch continuously like yaw (not integer-based)
    // This prevents gcdP: 0.0 detection when pitch changes are small
    float pitchAccum = (float)cursorAccumY;

    // Keep fractional part for smoother accumulation
    if (std::abs(cursorAccumY) >= 0.1) {
        cursorAccumY *= 0.7;  // Decay instead of hard reset
    }

    if (settings.horizontalOnly) {
        pitchAccum = 0.0f;
        cursorAccumY = 0.0;
    } else {
        // Humans NEVER have exactly 0.0 pitch movement - add micro-noise
        float baselinePitch = g_neuroState.driftPitch * 0.3f;
        if (std::abs(pitchAccum) < 0.001f) {
            pitchAccum = baselinePitch;  // Ensure never exactly 0
        }
    }

    // Ensure low in-game sensitivity doesn't overly throttle aim speed.
    float minSens = std::lerp(0.15f, 0.45f, std::clamp(speedNorm, 0.0f, 1.5f));
    float effectiveSens = std::max(cachedMouseSensitivity, minSens);
    float sensBase = effectiveSens * 0.6f + 0.2f;
    float sensMultiplier = sensBase * sensBase * sensBase * 8.0f;

    constexpr float kMouseGain = 0.10f;
    float yawDelta = yawdiffSpeed * sensMultiplier * kMouseGain;
    float pitchDelta = pitchAccum * sensMultiplier * kMouseGain;  // Use float, not int

    // Guard against overshoot: clamp step to fraction of remaining error
    auto clampStepToError = [](float delta, float remaining, float frac) -> float {
        float sign = delta >= 0.f ? 1.f : -1.f;
        float mag = std::abs(delta);
        float maxStep = std::max(0.2f, std::abs(remaining) * frac);
        mag = std::min(mag, maxStep);
        mag = std::min(mag, std::abs(remaining));
        return sign * mag;
    };

    float clampJitter = std::clamp(aimHumanizer, 0.9f, 1.1f);
    float firstFracYaw = 0.55f * clampJitter;
    float firstFracPitch = 0.6f * clampJitter;
    if (targetChangedThisFrame || maxDiff > 20.f) {
        firstFracYaw = 0.4f;
        firstFracPitch = 0.55f;
    }
    if (speedNorm > 1.5f) {
        firstFracYaw *= 0.5f;
        firstFracPitch *= 0.5f;
        if (speedNorm > 1.8f) {
            firstFracYaw *= 0.8f;
            firstFracPitch *= 0.8f;
        }
    }
    float yawDeltaErr = clampStepToError(yawDelta, yawDiff, firstFracYaw);
    float pitchDeltaErr = clampStepToError(pitchDelta, pitchDiff, firstFracPitch);
    bool yawErrClamped = std::abs(yawDeltaErr - yawDelta) > 1e-6f;
    bool pitchErrClamped = std::abs(pitchDeltaErr - pitchDelta) > 1e-6f;
    yawDelta = yawDeltaErr;
    pitchDelta = pitchDeltaErr;

    // Clamp per-frame rotation to keep motion buttery even on spikes
    float stepNorm = std::clamp(speedNorm, 0.0f, 2.2f);
    float yawStepBase = std::lerp(2.5f, 6.5f, std::min(stepNorm, 1.0f)) + std::max(0.0f, stepNorm - 1.0f) * 2.0f;
    float pitchStepBase = std::lerp(1.8f, 5.5f, std::min(stepNorm, 1.0f)) + std::max(0.0f, stepNorm - 1.0f) * 1.5f;
    if (stepNorm > 0.8f) {
        yawStepBase *= 0.85f;
        pitchStepBase *= 0.85f;
    }
    if (stepNorm > 1.2f) {
        yawStepBase *= 0.7f;
        pitchStepBase *= 0.7f;
        if (stepNorm > 1.6f) {
            yawStepBase *= 0.6f;
            pitchStepBase *= 0.6f;
        }
    }
    const float maxYawStep = std::clamp(yawStepBase, 2.5f, 11.5f);
    const float maxPitchStep = std::clamp(pitchStepBase, 1.8f, 9.5f);
    float yawDeltaStep = std::clamp(yawDelta, -maxYawStep, maxYawStep);
    float pitchDeltaStep = std::clamp(pitchDelta, -maxPitchStep, maxPitchStep);
    bool yawStepClamped = std::abs(yawDeltaStep - yawDelta) > 1e-6f;
    bool pitchStepClamped = std::abs(pitchDeltaStep - pitchDelta) > 1e-6f;
    yawDelta = yawDeltaStep;
    pitchDelta = pitchDeltaStep;

    static Timer debugLogTimer;
    if (debugLogTimer.Elapsed<>() > 300) {
        const char* yErrF = yawErrClamped ? "E" : "";
        const char* yStpF = yawStepClamped ? "S" : "";
        const char* pErrF = pitchErrClamped ? "E" : "";
        const char* pStpF = pitchStepClamped ? "S" : "";
        // Debug logging disabled - uncomment to debug aim calculations
        // log_debug("[AimAssist] spd=%.1f norm=%.2f dyn=%.2f ramp=%.2f (t=%.2f e=%.2f c=%.2f) jitter=%.4f diff(y=%.2f
        // p=%.2f max=%.2f) step(y=%.2f p=%.2f) delta(y=%.3f%s%s p=%.3f%s%s)", 	rawSpeedSetting, speedNorm,
        // dynamicSpeed, ramp, timeRamp, errorRamp, clickRamp, jitterRange, 	yawDiff, pitchDiff, maxDiff, maxYawStep,
        // maxPitchStep, 	yawDelta, yErrF, yStpF, pitchDelta, pErrF, pStpF);
        (void)yErrF;
        (void)yStpF;
        (void)pErrF;
        (void)pStpF;  // Suppress unused warnings
        debugLogTimer.Start();
    }

    // Refresh GCD periodically
    if (g_gcdRefreshTimer.Elapsed<>() > 500.0f) {
        g_cachedGCD = calculateGCD(cachedMouseSensitivity);
        g_gcdRefreshTimer.Start();
    }
    if (g_cachedGCD < 0.0001f) {
        g_cachedGCD = calculateGCD(cachedMouseSensitivity);
    }

    // Adding drift after GCD breaks the quantization pattern that AC expects
    yawDelta += g_neuroState.driftYaw;
    pitchDelta += settings.horizontalOnly ? 0.f : g_neuroState.driftPitch;

    // Apply STRICT GCD - no imperfection, real mice are quantized
    // AC (AimAssistB, AimAssistK) detects when sensitivity can't be determined
    yawDelta = applyGCDStrict(yawDelta, g_cachedGCD);
    pitchDelta = applyGCDStrict(pitchDelta, g_cachedGCD);

    // AimAssistH: flags when pitch is offset but yaw is perfect
    // AimAssistI: flags when yaw is offset but pitch is perfect
    balanceFloorDivisors(yawDelta, pitchDelta, g_cachedGCD, rng);

    bool applyYaw = std::abs(yawDelta) > 0.0001f;
    bool applyPitch = std::abs(pitchDelta) > 0.0001f;
    if (!applyYaw && !applyPitch) {
        // No rotation this frame - set prev slightly behind current (natural interpolation)
        float curYaw = player.getYaw();
        float curPitch = player.getPitch();
        // Use drift as offset instead of random (maintains physical coherence)
        player.setPrevYaw(curYaw - g_neuroState.driftYaw * 0.5f);
        player.setPrevPitch(curPitch - g_neuroState.driftPitch * 0.5f);
        return;
    }

    if (applyYaw) {
        float currentYaw = player.getYaw();
        float newYaw = currentYaw + yawDelta;
        player.setYaw(newYaw);
        // prevYaw = lerp towards newYaw (natural interpolation, not random)
        float prevLerp = randFloat(0.85f, 0.95f);
        player.setPrevYaw(std::lerp(currentYaw, newYaw, prevLerp));
    }

    if (applyPitch) {
        float currentPitch = player.getPitch();
        float finalPitch = std::clamp(currentPitch + pitchDelta, -90.0f, 90.0f);
        player.setPitch(finalPitch);
        // prevPitch = lerp towards finalPitch
        float prevLerp = randFloat(0.85f, 0.95f);
        player.setPrevPitch(std::lerp(currentPitch, finalPitch, prevLerp));
    } else {
        // Sync prevPitch with small drift-based offset
        float curPitch = player.getPitch();
        player.setPrevPitch(curPitch - g_neuroState.driftPitch * 0.3f);
    }
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

        if (settings.aimAtClosestPoint) {
            aimX = std::clamp(localX, minX, maxX);
            aimY = std::clamp(localY, minY, maxY);
            aimZ = std::clamp(localZ, minZ, maxZ);
            return true;
        }

        aimX = (minX + maxX) * 0.5;
        aimZ = (minZ + maxZ) * 0.5;
        double height = maxY - minY;
        aimY = height > 0.01 ? minY + height * 0.5 : targetY;
        return true;
    }

    constexpr double HALF_WIDTH = 0.3;

    if (settings.aimAtClosestPoint) {
        double minX = targetX - HALF_WIDTH;
        double maxX = targetX + HALF_WIDTH;
        double minY = targetY - 1.6;
        double maxY = targetY + 0.2;
        double minZ = targetZ - HALF_WIDTH;
        double maxZ = targetZ + HALF_WIDTH;

        aimX = std::clamp(localX, minX, maxX);
        aimY = std::clamp(localY, minY, maxY);
        aimZ = std::clamp(localZ, minZ, maxZ);
        return true;
    } else {
        double corners[4][3] = {
            {targetX - HALF_WIDTH, targetY - 1.6, targetZ + HALF_WIDTH},
            {targetX - HALF_WIDTH, targetY - 1.6, targetZ - HALF_WIDTH},
            {targetX + HALF_WIDTH, targetY + 0.2, targetZ - HALF_WIDTH},
            {targetX + HALF_WIDTH, targetY + 0.2, targetZ + HALF_WIDTH},
        };

        std::array<float, 4> yawDiffs;
        for (int i = 0; i < 4; i++) {
            float yawToCorner = getAngles(localX, localY, localZ, corners[i][0], corners[i][1], corners[i][2]).first;
            yawDiffs[i] = wrapTo180(-(localYaw - yawToCorner));
        }

        float yawDiffToPos =
            wrapTo180(-(localYaw - getAngles(localX, localY, localZ, targetX, targetY, targetZ).first));

        if (yawDiffToPos < 0) {
            float maxVal = yawDiffs[0];
            for (int i = 1; i < 4; i++)
                if (yawDiffs[i] > maxVal)
                    maxVal = yawDiffs[i];
            if (maxVal > 0) {
                return false;
            }
        } else {
            float minVal = yawDiffs[0];
            for (int i = 1; i < 4; i++)
                if (yawDiffs[i] < minVal)
                    minVal = yawDiffs[i];
            if (minVal < 0) {
                return false;
            }
        }

        aimX = targetX;
        aimY = targetY;
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
            ui::SliderFloat("Speed", &settings.speed, 1.0f, 120.0f, "%.0f", true);
            ui::SliderInt("FOV", &settings.fov, 10, 360, "%d");
            ui::SliderFloat("Distance", &settings.distance, 3.0f, 8.0f, "%.1f");
            ui::SliderInt("Reaction Time", &settings.reactionTime, 0, 200, "%d ms");
        },
        "Options",
        [this, &currentMode, &currentSortBy]() {
            ui::Checkbox("Always Aim", &settings.alwaysAim);
            ui::Checkbox("Break Blocks", &settings.breakBlocks);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            ui::Checkbox("Aim Closest Point", &settings.aimAtClosestPoint);
            ui::Checkbox("Target Invisibles", &settings.invisibles);
            ui::Checkbox("Stop On Hitbox", &settings.stopOnHitbox);
            ImGui::Spacing();
            if (ImGui::Combo("Target Mode", &currentMode, targetModes, IM_ARRAYSIZE(targetModes))) {
                settings.targetMode = static_cast<TargetMode>(currentMode);
            }
            if (settings.targetMode == TargetMode::DISTANCE || settings.targetMode == TargetMode::FOV) {
                ui::Checkbox("Lock Aim", &settings.lockAim);
            }
            // Show Sort By option only when Switch mode is selected
            if (settings.targetMode == TargetMode::SWITCH) {
                if (ImGui::Combo("Sort By", &currentSortBy, sortByModes, IM_ARRAYSIZE(sortByModes))) {
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
        });
}

std::string AimAssist::getDisplayInfo() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f", settings.speed);
    return buf;
}
