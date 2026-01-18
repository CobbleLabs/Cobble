#pragma once

#include <chrono>
#include <deque>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

// Backtrack module - keeps targets in range by showing their past positions
//
// How it works:
// 1. Stores a history of target positions (where they WERE)
// 2. When target moves out of range, shows their past position as a hitbox
// 3. Attacks to that position are still valid due to server latency tolerance
// 4. Does NOT block any packets - purely client-side position tracking

struct PositionSnapshot {
    double x, y, z;
    float yaw, pitch;
    int hurtTime;
    std::chrono::steady_clock::time_point timestamp;
};

class Backtrack : public Module {
public:
    Backtrack();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;

    struct Settings {
        // Distance
        float minDistance = 1.0f;  // Don't backtrack if closer
        float maxDistance = 4.0f;  // Don't backtrack if too far even in history

        // Timing
        int maxDelay = 200;     // Maximum backtrack time in ms
        int maxHurtTime = 200;  // Max hurt time to allow backtrack (ms)
        float cooldown = 0.5f;  // Cooldown after target changes

        // Conditions
        bool weaponOnly = false;

        // Visual
        bool showBacktrackBox = true;
        float boxColor[4] = {1.0f, 0.3f, 0.3f, 0.5f};
        bool filledBox = false;
    } settings;

    // Public access for other modules (like AimAssist)
    bool hasBacktrackedPosition() const { return isBacktracking; }
    void getBacktrackedPosition(double& x, double& y, double& z) const {
        x = backtrackedX;
        y = backtrackedY;
        z = backtrackedZ;
    }

private:
    // Position history for each target (keyed by UUID or position)
    std::deque<PositionSnapshot> positionHistory;

    // Current backtrack state
    bool isBacktracking = false;
    double backtrackedX = 0, backtrackedY = 0, backtrackedZ = 0;
    std::string currentTargetUUID;
    std::chrono::steady_clock::time_point lastTargetChange;

    // Helpers
    bool isHoldingWeapon();
    void recordPosition(const PlayerInfo& target);
    bool findBacktrackedPosition(double playerX, double playerY, double playerZ, double currentTargetX,
                                 double currentTargetY, double currentTargetZ);
    void clearHistory();
};

extern Backtrack* backtrack;
