#pragma once

#include "../module.h"
#include "../../jni/java.h"

#include <atomic>
#include <mutex>
#include <random>

class TimerRange : public Module {
public:
    enum class PacketMode {
        PRE = 0,
        POST = 1
    };

    TimerRange();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void onOutgoingPacket(JNIEnv* env, jobject packet, PacketMode phase);

    struct Settings {
        PacketMode mode = PacketMode::PRE;
        float minSpeed = 0.8f;
        float maxSpeed = 1.2f;
        int intervalTicks = 5;
        bool mousePressed = false;
        bool weaponOnly = false;
    } settings;

private:
    void selectNextSpeed(bool force);

    std::mt19937 rng;
    std::mutex speedMutex;
    int movementTicks = 0;
    std::atomic<float> currentSpeed{1.0f};
};

extern TimerRange* timerRange;
