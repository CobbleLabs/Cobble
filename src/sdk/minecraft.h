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
	int hurtTime = 0;  // 0-10: ticks since last hit (0 = can take damage)
};

// Render position for WorldToScreen calculations
struct RenderPos
{
	double x = 0.0;
	double y = 0.0;
	double z = 0.0;
};

// Item slot info for inventory operations
struct SlotInfo
{
	int slotId = -1;
	int itemId = 0;         // 0 = empty, 282 = soup, 373 = potion, etc
	int stackSize = 0;
	int damage = 0;         // Potion metadata (used to identify potion type)
	bool isSplashPotion = false;
	bool isHealthPotion = false;  // Instant Health I or II
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

	// Mouse sensitivity (for proper aim assist scaling)
	float getMouseSensitivity();

	// Inventory operations for AutoRefill
	std::vector<SlotInfo> getInventorySlots();  // Get all 36 inventory slots
	void windowClick(int slotId, int button);   // Click inventory slot (shift-click)
	void openInventory();   // Open player inventory GUI (like pressing E)
	void closeInventory();  // Close current GUI
	bool isInventoryOpen(); // Check if inventory GUI is open

	// Hit detection for Hit Select module
	bool isTargetingEntity();           // Check if looking at an entity
	int getTargetedEntityHurtTime();    // Get hurtTime of targeted entity (0 = can take damage)

	// Movement control - release all movement keybinds
	void releaseMovementKeys();
	// Restore keys that are physically held down
	void restoreMovementKeys();

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
	
	// Cached for release/restore keys (thread-safe)
	jclass cls_keyBinding = nullptr;
	jmethodID mid_unPressAllKeys = nullptr;
	jmethodID mid_setKeyBindState = nullptr;
	jmethodID mid_getKeyCode = nullptr;

	friend void SDK_Init();
};

// Global SDK instance
extern Minecraft mc;

// Initialize SDK after Java::Init()
void SDK_Init();
