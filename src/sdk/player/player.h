#pragma once

#include "../../jni/java.h"
#include "../version.h"
#include <mutex>

// Simplified wrapper for the local player (EntityPlayerSP/EntityClientPlayerMP)
// Flattened hierarchy with only essential ghost client functionality
struct LocalPlayer {
    bool isValid() const { return instance != nullptr; }

    // Update instance from Minecraft.thePlayer
    void refresh();
    void cleanup();

    // Position
    double getPosX();
    double getPosY();
    double getPosZ();

    // Last tick position (for interpolation)
    double getLastPosX();
    double getLastPosY();
    double getLastPosZ();

    // Motion (for velocity module)
    double getMotionX();
    double getMotionY();
    double getMotionZ();
    void setMotionX(double value);
    void setMotionY(double value);
    void setMotionZ(double value);

    // Rotation
    float getYaw();
    float getPitch();
    float getPrevYaw();
    float getPrevPitch();
    void setYaw(float value);
    void setPitch(float value);
    void setPrevYaw(float value);
    void setPrevPitch(float value);

    // State
    bool isOnGround();
    void setOnGround(bool value);
    bool isSprinting();
    void setSprinting(bool value);
    bool isSneaking();
    bool isUsingItem();  // True when eating/drinking (prevents sprint)

    // Ticks the held item has been in use — for a bow, the draw progress the
    // server will read when the release packet arrives. 0 when nothing is in use.
    int getItemInUseTicks();

    // Combat
    float getHealth();
    float getMaxHealth();
    int getHurtTime();
    int getHurtResistantTime();
    int getMaxHurtResistantTime();
    void jump();
    void setJumpTicks(int value);

    // Inventory (for weapon detection)
    int getHeldItemId();
    int getHeldItemDamage();        // Get metadata/damage value of held item (for potion types)
    int getItemInSlot(int slot);    // Get item ID in specific hotbar/inventory slot (0-35)
    int getCurrentSlot();           // Get current hotbar slot (0-8)
    void setCurrentSlot(int slot);  // Set current hotbar slot (0-8)

    // lastReportedPos (prevents sending movement packets)
    double getLastReportedPosX();
    double getLastReportedPosY();
    double getLastReportedPosZ();
    bool hasLastReportedPosition() const;
    void setLastReportedPosX(double value);
    void setLastReportedPosY(double value);
    void setLastReportedPosZ(double value);
    float getLastReportedYaw();
    float getLastReportedPitch();
    void setLastReportedYaw(float value);
    void setLastReportedPitch(float value);

    jobject instance = nullptr;
    mutable std::recursive_mutex instanceMutex;

    // Exposed so modules (e.g. NoSlowDown) can call isUsingItem() with a
    // specific JNIEnv rather than the internal Java::Env() wrapper.
    jmethodID mid_isUsingItem = nullptr;

private:
    jclass cls = nullptr;
    jclass entityCls = nullptr;
    jclass livingCls = nullptr;

    // Fields - filled on first refresh
    jfieldID fid_posX = nullptr;
    jfieldID fid_posY = nullptr;
    jfieldID fid_posZ = nullptr;
    jfieldID fid_lastPosX = nullptr;
    jfieldID fid_lastPosY = nullptr;
    jfieldID fid_lastPosZ = nullptr;
    jfieldID fid_motionX = nullptr;
    jfieldID fid_motionY = nullptr;
    jfieldID fid_motionZ = nullptr;
    jfieldID fid_rotationYaw = nullptr;
    jfieldID fid_rotationPitch = nullptr;
    jfieldID fid_prevRotationYaw = nullptr;
    jfieldID fid_prevRotationPitch = nullptr;
    jfieldID fid_onGround = nullptr;
    jfieldID fid_hurtTime = nullptr;
    jfieldID fid_hurtResistantTime = nullptr;
    jfieldID fid_maxHurtResistantTime = nullptr;
    jfieldID fid_jumpTicks = nullptr;
    jfieldID fid_itemInUseCount = nullptr;

    // Methods
    jmethodID mid_isSprinting = nullptr;
    jmethodID mid_setSprinting = nullptr;
    jmethodID mid_isSneaking = nullptr;
    jmethodID mid_getHealth = nullptr;
    jmethodID mid_jump = nullptr;

    jfieldID fid_lastReportedPosX = nullptr;
    jfieldID fid_lastReportedPosY = nullptr;
    jfieldID fid_lastReportedPosZ = nullptr;
    jfieldID fid_lastReportedYaw = nullptr;
    jfieldID fid_lastReportedPitch = nullptr;

    // Cached inventory fields/methods (prevents JNI leak in getHeldItemId)
    jclass cls_Item = nullptr;                // GlobalRef
    jfieldID fid_playerInventory = nullptr;
    jmethodID mid_getCurrentItem = nullptr;
    jmethodID mid_getItem = nullptr;
    jmethodID mid_getIdFromItem = nullptr;
    jmethodID mid_getMetadata = nullptr;

    bool fieldsInitialized = false;
    void initFields();

    friend void SDK_Init();
};

extern LocalPlayer player;
