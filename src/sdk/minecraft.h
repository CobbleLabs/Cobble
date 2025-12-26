#pragma once

#include "../jni/java.h"
#include "jstring_util.h"
#include "player.h"

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
