#pragma once

#include "../jni/java.h"
#include "version.h"

// Simplified wrapper for the local player (EntityPlayerSP/EntityClientPlayerMP)
// Flattened hierarchy with only essential ghost client functionality
struct LocalPlayer
{
	bool isValid() const { return instance != nullptr; }

	// Update instance from Minecraft.thePlayer
	void refresh();

	// Position
	double getPosX();
	double getPosY();
	double getPosZ();

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
	void setYaw(float value);
	void setPitch(float value);

	// State
	bool isOnGround();
	void setOnGround(bool value);
	bool isSprinting();
	void setSprinting(bool value);
	bool isSneaking();

	// Combat
	float getHealth();
	int getHurtTime();
	void jump();

	// Inventory (for weapon detection)
	int getHeldItemId();

	jobject instance = nullptr;

private:
	jclass cls = nullptr;
	jclass entityCls = nullptr;
	jclass livingCls = nullptr;

	// Fields - filled on first refresh
	jfieldID fid_posX = nullptr;
	jfieldID fid_posY = nullptr;
	jfieldID fid_posZ = nullptr;
	jfieldID fid_motionX = nullptr;
	jfieldID fid_motionY = nullptr;
	jfieldID fid_motionZ = nullptr;
	jfieldID fid_rotationYaw = nullptr;
	jfieldID fid_rotationPitch = nullptr;
	jfieldID fid_onGround = nullptr;
	jfieldID fid_hurtTime = nullptr;

	// Methods
	jmethodID mid_isSprinting = nullptr;
	jmethodID mid_setSprinting = nullptr;
	jmethodID mid_isSneaking = nullptr;
	jmethodID mid_getHealth = nullptr;
	jmethodID mid_jump = nullptr;

	bool fieldsInitialized = false;
	void initFields();

	friend void SDK_Init();
};

extern LocalPlayer player;
