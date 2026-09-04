#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../jni/java.h"
#include "../module.h"

// Forward declare
struct ImDrawList;

// Cached player data for rendering tracers
struct TracerPlayer {
    int entityId = 0;
    std::string name;
    double posX = 0, posY = 0, posZ = 0;
    double lastPosX = 0, lastPosY = 0, lastPosZ = 0;
    float distance = 0;
    float health = 20.0f;
    bool valid = false;
};

class Tracers : public Module {
public:
    Tracers();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        bool colorByDistance = true;
        bool colorByHealth = false;
        bool colorByTeam = false;
        float tracerColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // White default
        float thickness = 2.0f;
        float range = 128.0f;

        // Line start position (0 = center, 1 = bottom)
        int lineFrom = 0;  // 0=Center, 1=Bottom

        // Master alpha. Lines used to be drawn at up to 90% opacity with a solid
        // black halo behind them, which put a bundle of near-opaque cables through
        // the middle of the screen. These four keys are new, so a config saved by an
        // older build picks the calmer defaults up instead of keeping the loud look.
        float opacity = 0.6f;

        // Pixels of empty space around the start point before a line becomes
        // visible, followed by a ramp of ~1.3x that length. Measured along the
        // curve and scaled by resolution, so the crosshair stays readable no matter
        // how many players are on screen. 0 restores lines that touch the origin.
        float startGap = 55.0f;

        bool outline = true;  // thin dark edge for contrast on bright terrain
        bool endDot = true;   // small dot at the target end of the line
    } settings;

private:
    struct SmoothedPlayer {
        double x = 0, y = 0, z = 0;
        long long lastSeen = 0;
        bool initialized = false;
    };

    // Thread-safe player cache
    std::mutex cacheMutex;
    std::vector<TracerPlayer> cachedPlayers;
    std::unordered_map<int, SmoothedPlayer> smoothedPlayers;
    long long lastRenderTime = 0;

    // Render thread data
    double cachedRenderX = 0, cachedRenderY = 0, cachedRenderZ = 0;
    float cachedPartialTicks = 0.0f;

    // Local player position for fallback
    double cachedLocalX = 0, cachedLocalY = 0, cachedLocalZ = 0;
    double cachedLocalLastX = 0, cachedLocalLastY = 0, cachedLocalLastZ = 0;

    // World change detection
    jobject lastWorldRef = nullptr;
    void checkWorldChange();

    // Rendering safety
    long long lastUpdateTick = 0;
};

extern Tracers* tracers;
