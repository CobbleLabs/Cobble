#pragma once

#include <jni.h>
#include <atomic>
#include <chrono>
#include <random>
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
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void setEntityLock(bool enabled);
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum class TargetMode { HEALTH, DISTANCE, FOV, SWITCH };
    enum class SortBy { DISTANCE, HEALTH, FOV, HURT_TIME };

    struct Settings {
        bool horizontalOnly = true;
        bool invisibles = false;
        bool alwaysAim = false;
        bool breakBlocks = false;
        bool aimAtClosestPoint = false;
        bool entityLock = false;  // Keep the acquired entity until it becomes invalid
        bool weaponOnly = false;
        bool stopOnHitbox = false;  // Stops aim assist when crosshair is inside hitbox

        TargetMode targetMode = TargetMode::DISTANCE;
        SortBy switchSortBy = SortBy::DISTANCE;  // Only used when targetMode is SWITCH
        bool hurtTimeLock = false;               // Instant aim when switching targets in Hurt Time mode
        int fov = 180;
        float distance = 5.0f;
        float speed = 50.0f;
        float jitterPercent = 0.0f;
        int reactionTime = 50;  // ms
    } settings;

private:
    void releaseCachedTarget(JNIEnv* env);
    void resetTargetState(JNIEnv* env);
    void updateSimulatedJitter();

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

    // Apply randomized aim
    void applyAimRand(float yawDiff, float pitchDiff, float speed);

    // Get aim point (handles closest point vs center)
    bool getAimPoint(double targetX, double targetY, double targetZ, double& aimX, double& aimY, double& aimZ);

    // Boost systems
    AimBoost yawBoost{0.9f, 1.1f, 450, 600, true};
    AimBoost yawBoost2{1.0f, 1.3f, 800, 1000, false};
    AimBoost pitchBoost{0.8f, 1.2f, 450, 600, true};
    AimBoost pitchBoost2{0.7f, 1.3f, 600, 900, false};

    // Mouse + smoothing state
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

    Timer reactionTimeTimer;

    // Cached Target for hybrid tracking
    jobject cachedTarget = nullptr;
    std::atomic<bool> entityLockEnabled_{false};
    std::atomic<bool> targetResetRequested_{false};

    // Locked target position
    bool hasLockedTarget = false;
    double lockedTargetX = 0.0;
    double lockedTargetY = 0.0;
    double lockedTargetZ = 0.0;

    std::mt19937 rng;
};

extern AimAssist* aimAssist;
