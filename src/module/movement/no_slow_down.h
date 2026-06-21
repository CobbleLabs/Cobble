#pragma once

#include "../module.h"
#include "../../jni/java.h"

#include <cstdint>

class NoSlowDown : public Module {
public:
    enum class PacketMode { Vanilla, NCP, Hypixel, AAC };
    enum class HeldItemType { None, Sword, Bow, Consume };

    PacketMode packetMode = PacketMode::Hypixel;

    float blockForward = 1.0f;
    float blockStrafe = 1.0f;
    float consumeForward = 1.0f;
    float consumeStrafe = 1.0f;
    float bowForward = 1.0f;
    float bowStrafe = 1.0f;

    NoSlowDown();

    void onUpdate() override;
    void onRenderMenu() override;
    void applyMovement(JNIEnv* env, jobject playerInstance);
    void handlePacketMode();

private:
    bool initializeFields(JNIEnv* env, jobject playerInstance);
    bool isKeyPressed(JNIEnv* env, jobject gameSettings, jfieldID keyBindField);
    bool isPlayerUsingItem(JNIEnv* env, jobject playerInstance);

    HeldItemType getHeldItemType();

    jfieldID movementInputField = nullptr;
    jfieldID moveForwardField = nullptr;
    jfieldID moveStrafeField = nullptr;
    jfieldID entityMoveForwardField = nullptr;
    jfieldID entityMoveStrafeField = nullptr;
    jfieldID itemInUseCountField = nullptr;

    // Shared packet timer; each mode selects its own interval.
    uint32_t packetTimer = 0;
};

extern NoSlowDown* noSlowDown;
