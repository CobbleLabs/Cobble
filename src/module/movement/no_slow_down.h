#pragma once

#include "../module.h"
#include "../../jni/java.h"

#include <cstdint>

class NoSlowDown : public Module {
public:
    // Packet modes (ported from LiquidBounce JS)
    enum class PacketMode { Vanilla, NCP, Hypixel, AAC };

    // Item type for multiplier selection
    enum class HeldItemType { None, Sword, Bow, Consume };

    // Settings
    PacketMode packetMode     = PacketMode::Hypixel;

    // Per-item multipliers (forward / strafe)
    float blockForward   = 1.0f;   // Sword (blocking)
    float blockStrafe    = 1.0f;
    float consumeForward = 1.0f;   // Food / Potion / Milk
    float consumeStrafe  = 1.0f;
    float bowForward     = 1.0f;   // Bow
    float bowStrafe      = 1.0f;

    NoSlowDown();

    void onUpdate() override;
    void onRenderMenu() override;
    void handlePacketMode();

private:
    bool initializeFields();
    bool isKeyPhysicallyPressed(jobject gameSettings, jfieldID keyBindField);
    bool isPlayerUsingItem();

    // Item-type detection
    HeldItemType getHeldItemType();

    // JNI field IDs cached after first resolve
    jfieldID movementInputField     = nullptr;
    jfieldID moveForwardField       = nullptr;
    jfieldID moveStrafeField        = nullptr;
    jfieldID entityMoveForwardField = nullptr;
    jfieldID entityMoveStrafeField  = nullptr;
    jfieldID itemInUseCountField    = nullptr;

    // Packet-mode timer (for AAC 80 ms interval) — uint32_t avoids Windows.h in header
    uint32_t packetTimer   = 0;
    bool     packetSentPre = false;
};

extern NoSlowDown* noSlowDown;
