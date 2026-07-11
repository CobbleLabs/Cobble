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
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        // Mode
        BacktrackMode mode = BacktrackMode::Manual;

        // Manual mode settings
        int ticks = 6;           // Number of previous positions to store (1-20)
        bool renderPreviousTicks = true;  // Render ghost at past positions

        // Lag Based mode settings — a dynamic latency WINDOW. Each lag session
        // rolls an effective latency in [min, max] so the delay isn't a constant
        // (harder to read, and the on-screen readout shows the range, not a fixed
        // exact ping).
        int latencyMin = 120;    // Min added lag in ms (50-500)
        int latencyMax = 260;    // Max added lag in ms (50-500)
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

    // Effective (rolled) latency for the current lag session, in ms. Used by the
    // packet gate so the actual hold matches the dynamic window.
    int getCurrentLatency() const { return currentLatency; }

    // Read the current 3D-ghost render target (entity id + backtracked world pos)
    // for the injected render hook. Returns false when nothing should be drawn.
    // Called from the render thread; the small race with the tick thread is benign.
    bool getGhostRenderTarget(int& entityId, double& x, double& y, double& z) const;

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
    int ghostEntityId = -1;  // Entity id of the current backtrack target (for the 3D ghost render)
    std::string currentTargetUUID;
    std::chrono::steady_clock::time_point lastTargetChange;

    // Lag Based mode state
    std::chrono::steady_clock::time_point lagStartTime;
    int delayedPacketCount = 0;
    int currentLatency = 200;  // Effective latency this lag session, rolled in [min,max]

    // Helpers
    void rollLatency();  // pick currentLatency uniformly within [latencyMin, latencyMax]
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
