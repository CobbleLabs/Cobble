#pragma once

#include <mutex>
#include <vector>
#include "../../jni/java.h"
#include "../module.h"

// Forward declare
struct ImDrawList;

// Trajectory point for rendering (stored relative to player eye position)
struct TrajectoryPoint {
    float relX = 0, relY = 0, relZ = 0;  // Relative to player eye
    bool valid = false;
    bool hitBlock = false;
    bool hitPlayer = false;
};

class Trajectories : public Module {
public:
    Trajectories();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;

    struct Settings {
        float lineColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // White default
        float thickness = 2.0f;

        // Projectile toggles
        bool showBow = true;
        bool showEnderPearl = true;
        bool showEgg = true;
        bool showSnowball = true;
        bool showPotionHealing = true;
        bool showPotionPoison = true;
        bool showPotionSlowness = true;
        bool showExpBottle = true;
    } settings;

private:
    // Thread-safe trajectory cache
    std::mutex cacheMutex;
    std::vector<TrajectoryPoint> cachedTrajectory;

    // Render thread data
    double cachedRenderX = 0, cachedRenderY = 0, cachedRenderZ = 0;
    float cachedPartialTicks = 0.0f;

    // Local player cached data for trajectory calculation
    double cachedLocalX = 0, cachedLocalY = 0, cachedLocalZ = 0;
    float cachedYaw = 0, cachedPitch = 0;

    // Previous tick positions for smooth interpolation
    double prevLocalX = 0, prevLocalY = 0, prevLocalZ = 0;
    bool cachedSneaking = false;
    bool firstFrameAfterEnable = true;

    // World change detection
    jobject lastWorldRef = nullptr;
    void checkWorldChange();

    // Rendering safety
    long long lastUpdateTick = 0;

    // Item held detection
    int cachedHeldItemId = 0;

    // Bow charge tracking
    long long bowChargeStartTime = 0;  // When bow charge started (ms)
    bool wasBowCharging = false;       // Was charging last tick
    float cachedBowCharge = 0.0f;      // 0.0 to 1.0 charge level

    // Projectile physics constants
    struct ProjectileType {
        float gravity;             // Gravity per tick
        float initialSpeed;        // Initial velocity multiplier
        float drag;                // Air resistance (multiply velocity each tick)
        float upwardAngle = 0.0f;  // Degrees to add to pitch (for potions)
    };

    ProjectileType getProjectileType(int itemId, float bowCharge = 1.0f);
    bool isThrowableItem(int itemId);
    void simulateTrajectory();
    float getBowChargeLevel();  // Get current bow charge (0.0-1.0)
};

extern Trajectories* trajectories;