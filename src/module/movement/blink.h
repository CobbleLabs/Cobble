#pragma once
#include "../module.h"
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include <vector>
#include <mutex>
#include <windows.h>

struct Vec3d {
    double x, y, z;
};

class Blink : public Module {
public:
    Blink();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;

    // Called from blink_gate to check if packet should be blocked
    bool shouldBlockPacket();
    void queuePacket(JNIEnv* env, jobject packet);
    void flushQueuedPackets();
    void clearQueuedPackets();

    struct Settings {
        float timerLimit = 3.0f;                           // Max seconds for blink
        bool movementOnly = true;                          // Only C03PacketPlayer (fixed)
        bool showTimerHud = true;                          // Show timer progress bar
        bool showBreadcrumbs = true;                       // Show path line
        float blinkColor[4] = {1.0f, 0.55f, 0.0f, 0.78f};  // Orange default
    } settings;

    // Internal state
    bool isBlinking = false;
    ULONGLONG blinkStartTime = 0;

    // Positions for visualization (breadcrumbs)
    std::vector<Vec3d> pathPositions;
    RenderPos cachedRenderPos{};
    bool hasRenderPos = false;
    std::mutex pathMutex;

    // Blocked packets for flush on disable
    std::vector<jobject> queuedPackets;
    std::mutex queueMutex;
};

extern Blink* blink;
