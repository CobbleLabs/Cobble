#pragma once

#include <chrono>
#include "../module.h"

// Blink module - hybrid movement buffering via PacketInterceptor
// Server sees continuous movement with a controlled delay (no hard freeze)

class Blink : public Module {
public:
    Blink();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;

    struct Settings {
        float maxDuration = 5.0f;  // Auto-disable after N seconds
        bool disableOnAttack = true;
        bool showDebug = true;  // Show local vs server position
    } settings;

private:
    std::chrono::steady_clock::time_point startTime;
    bool wasAttacking = false;
    int tickCounter = 0;

    // Stored frozen position (where server thinks we are)
    double serverX = 0, serverY = 0, serverZ = 0;
};

extern Blink* blink;
