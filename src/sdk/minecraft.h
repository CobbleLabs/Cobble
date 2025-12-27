#pragma once

#include "../jni/java.h"
#include "jstring_util.h"
#include "player.h"
#include <vector>

// Data for a remote player in the world
struct PlayerInfo
{
	jobject instance = nullptr;
	std::string name;
	double posX = 0.0;
	double posY = 0.0;
	double posZ = 0.0;
	double lastPosX = 0.0;
	double lastPosY = 0.0;
	double lastPosZ = 0.0;
	float health = 20.0f;
	float maxHealth = 20.0f;
	bool isInvisible = false;
};

// Render position for WorldToScreen calculations
struct RenderPos
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

// Simplified wrapper for net.minecraft.client.Minecraft
// Only exposes what's needed for basic ghost client
struct Minecraft
{
	bool isValid() const { return instance != nullptr && cls != nullptr; }

	// Get local player instance (thePlayer)
	jobject getPlayer();
	
	// Get current world (theWorld)
	jobject getWorld();

	// Check if GUI is open (chat, inventory, etc)
	bool isInGui();

	// Mouse click (for autoclicker)
	void clickMouse();

	// Click delays
	int getRightClickDelay();
	void setRightClickDelay(int value);
	void setLeftClickCounter(int value);

	// MouseOver - check what player is looking at
	bool isTargetingBlock();

	// Get all players in the world (excluding local player)
	std::vector<PlayerInfo> getPlayerList();

	// Check if local player can see target entity
	bool canEntityBeSeen(jobject target);
	
	// Render info for ESP/NameTags
	RenderPos getRenderPos();
	float getPartialTicks();

	jclass cls = nullptr;
	jobject instance = nullptr;

private:
	// Cached IDs (filled in SDK_Init)
	jmethodID mid_getMinecraft = nullptr;
	jfieldID fid_thePlayer = nullptr;
	jfieldID fid_theWorld = nullptr;
	jfieldID fid_currentScreen = nullptr;
	jmethodID mid_clickMouse = nullptr;
	jfieldID fid_rightClickDelayTimer = nullptr;
	jfieldID fid_leftClickCounter = nullptr;

	friend void SDK_Init();
};

// Global SDK instance
extern Minecraft mc;

// Initialize SDK after Java::Init()
void SDK_Init();
