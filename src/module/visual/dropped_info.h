#pragma once

#include "../module.h"
#include <mutex>

class DroppedInfo : public Module {
public:
    DroppedInfo();
    
    void onEnable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    
    struct Settings {
        float x = 20.0f;
        float y = 200.0f;
        float scale = 1.0f;
        bool background = true;
        bool showWaypoint = true;
        float waypointColor[4] = {0.0f, 1.0f, 0.0f, 1.0f}; // Green default
    } settings;

private:
    std::mutex mutex;
    int potionCount = 0;
    
    // Waypoint cache
    bool hasTarget = false;
    double targetX = 0, targetY = 0, targetZ = 0;
    
    // UI State
    bool dragging = false;
    float dragOffsetX = 0, dragOffsetY = 0;
};

extern DroppedInfo* droppedInfo;
