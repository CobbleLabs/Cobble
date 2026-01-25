#pragma once

#include <chrono>
#include <deque>
#include <unordered_map>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

// Backtrack module - hit players at their previous positions
//
// Two modes:
// 1. Manual - stores N ticks of position history, renders ghost hitboxes
// 2. LagBased - delays incoming entity position packets when advantageous

enum class BacktrackMode { Manual = 0, LagBased = 1 };

struct PositionSnapshot {
    double x, y, z;
    float yaw, pitch;
    int hurtTime;
    int tickAge;  // How many ticks old this snapshot is
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
        // Mode
        BacktrackMode mode = BacktrackMode::Manual;

        // Manual mode settings
        int ticks = 6;           // Number of previous positions to store (1-20)
        bool renderPreviousTicks = true;  // Render ghost at past positions

        // Lag Based mode settings
        int latency = 200;       // Added lag in ms (50-500)
        bool renderServerPos = true;  // Render actual server position

        // Distance
        float minDistance = 1.0f;
        float maxDistance = 4.0f;

        // Timing (Manual mode)
        int maxHurtTime = 200;
        float cooldown = 0.5f;

        // Conditions
        bool weaponOnly = false;

        // Visual (shared)
        bool showBacktrackBox = true;
        float boxColor[4] = {1.0f, 0.3f, 0.3f, 0.5f};      // Primary box color
        float shadowColor[4] = {0.5f, 0.5f, 0.5f, 0.3f};   // Ghost/shadow color
        bool filledBox = false;
    } settings;

    // Public access for other modules
    bool hasBacktrackedPosition() const { return isBacktracking; }
    void getBacktrackedPosition(double& x, double& y, double& z) const {
        x = backtrackedX;
        y = backtrackedY;
        z = backtrackedZ;
    }

    // Lag Based mode - called from BacktrackGate
    bool shouldDelayPacket() const;
    void onPacketDelayed();
    void flushDelayedPackets();

    // Get all stored positions for a target (for rendering)
    const std::deque<PositionSnapshot>* getPositionHistory(const std::string& uuid) const;

private:
    // Position history per entity (keyed by UUID)
    std::unordered_map<std::string, std::deque<PositionSnapshot>> entityHistory;

    // Current backtrack state
    bool isBacktracking = false;
    bool isLagging = false;  // Lag Based mode active
    double backtrackedX = 0, backtrackedY = 0, backtrackedZ = 0;
    double currentRealX = 0, currentRealY = 0, currentRealZ = 0;  // Real position (for Lag Based indicator)
    std::string currentTargetUUID;
    std::chrono::steady_clock::time_point lastTargetChange;

    // Lag Based mode state
    std::chrono::steady_clock::time_point lagStartTime;
    int delayedPacketCount = 0;

    // Helpers
    bool isHoldingWeapon();
    void recordPosition(const PlayerInfo& target);
    bool findBacktrackedPosition(double playerX, double playerY, double playerZ,
                                 double currentTargetX, double currentTargetY, double currentTargetZ);
    void clearHistory();
    void clearEntityHistory(const std::string& uuid);

    // Rendering helpers (uses unsigned int instead of ImU32 to avoid header dependency)
    void renderBox3D(double x, double y, double z, unsigned int color, unsigned int fillColor, bool filled);
    void renderManualModeVisuals();
    void renderLagBasedModeVisuals();
};

extern Backtrack* backtrack;
