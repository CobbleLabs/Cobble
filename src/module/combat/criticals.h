#pragma once

#include "../module.h"
#include "../../jni/java.h"

class Criticals : public Module {
public:
    Criticals();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;

    struct Settings {
        int maxDelayMs = 100;           // Delay between criticals (50-500ms)
        int chance = 80;                // Chance to do critical (0-100%)
        bool onlyWhileAttacking = true; // Only apply when LMB pressed
        bool lookingAtPlayer = true;    // Only apply when looking at a player
        bool onlyWithSword = false;     // Only apply when holding sword
    } settings;

private:
    // State tracking
    unsigned long lastCriticalTime = 0;
    bool wasAttacking = false;
    
    // JNI cached IDs
    bool jniInitialized = false;
    jclass cls_C04Packet = nullptr;
    jmethodID mid_C04Init = nullptr;
    bool is17 = false;  // True if 1.7.10 constructor (5 params)
    
    // Helper methods
    bool initJNI();
    void sendCriticalPackets(double x, double y, double z);
    bool isHoldingSword();
    bool isLookingAtPlayer();
    bool canDoCritical();
    bool checkChance();
};

extern Criticals* criticals;
