#pragma once

#include "../../jni/java.h"
#include "../jni/jstring_util.h"
#include "../player/player.h"
#include <vector>
#include <map>
#include <string>
#include <atomic>

// Data for a remote player in the world
struct PlayerInfo
{
	jobject instance = nullptr;
	std::string uuid;       // Stable player identity (Entity.getUniqueID().toString())
	std::string name;
	std::string displayName;
	std::string profileName; // GameProfile name (stable, unspoofable)
	int teamColor = -1;
	double posX = 0.0;
	double posY = 0.0;
	double posZ = 0.0;
	double lastPosX = 0.0;
	double lastPosY = 0.0;
	double lastPosZ = 0.0;
	float health = 20.0f;
	float maxHealth = 20.0f;
	bool isInvisible = false;
	bool isNPC = false;  // NPC detection (bots, server entities)
	int hurtTime = 0;
	
	// Hitbox - bounding box dimensions (from AxisAlignedBB)
	// Standard player hitbox is 0.6 wide, 1.8 tall (1.62 when sneaking)
	float hitboxMinX = 0.0f;
	float hitboxMinY = 0.0f;
	float hitboxMinZ = 0.0f;
	float hitboxMaxX = 0.0f;
	float hitboxMaxY = 0.0f;
	float hitboxMaxZ = 0.0f;
	bool isSneaking = false;
	
	// Computed hitbox center
	float getHitboxCenterX() const { return (hitboxMinX + hitboxMaxX) / 2.0f; }
	float getHitboxCenterY() const { return (hitboxMinY + hitboxMaxY) / 2.0f; }
	float getHitboxCenterZ() const { return (hitboxMinZ + hitboxMaxZ) / 2.0f; }
	float getHitboxWidth() const { return hitboxMaxX - hitboxMinX; }
	float getHitboxHeight() const { return hitboxMaxY - hitboxMinY; }
	float getEyeHeight() const { return isSneaking ? 1.54f : 1.62f; }
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
	
	// Find a specific player by name and return their info (including UUID)
	// Returns empty PlayerInfo if not found
	// Use this to resolve name → UUID when adding friends
	PlayerInfo findPlayerByName(const std::string& name);

	// Check if local player can see target entity
	bool canEntityBeSeen(jobject target);
	
	// Render info for ESP/NameTags
	RenderPos getRenderPos();
	float getPartialTicks();

	// Mouse sensitivity (for proper aim assist scaling)
	float getMouseSensitivity();

	// Timer speed control (1.0 = normal, higher = faster game speed)
	void setTimerSpeed(float speed);

	// Inventory operations for AutoRefill
	std::vector<SlotInfo> getInventorySlots();  // Get all 36 inventory slots
	void windowClick(int slotId, int button);   // Click inventory slot (shift-click)
	void openInventory();   // Open player inventory GUI (like pressing E)
	void closeInventory();  // Close current GUI
	bool isInventoryOpen(); // Check if inventory GUI is open
	
	// Get local player's team color from their displayName
	// Returns -1 (UNKNOWN) if no color code found
	int getLocalPlayerTeamColor();

	// Hit detection for Hit Select module
	bool isTargetingEntity();           // Check if looking at an entity
	int getTargetedEntityHurtTime();    // Get hurtTime of targeted entity (0 = can take damage)
	PlayerInfo getTargetedPlayerInfo(); // Get info (UUID, name) of targeted player (for quick-add friend)

	// Movement control - release all movement keybinds
	void releaseMovementKeys();
	// Restore keys that are physically held down
	void restoreMovementKeys();

	// World block query for Bridge Assist
	// Returns block ID at given coordinates (0 = air)
	int getBlockId(int x, int y, int z);

	struct Vec3 {
		float x = 0, y = 0, z = 0;
		Vec3() = default;
		Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
	};
	
	enum class RAYTRACE_BLOCKS_RESULT {
		HIT,                  // hit a block
		HIT_FROM_AIRBLOCK,    // hit a block but ray origin was airblock
		NO_HIT,               // the void
		NO_HIT_FROM_AIRBLOCK, // the void but ray origin was airblock
		RAYTRACE_ERROR        // debugging purposes (renamed from ERROR to avoid Windows macro)
	};
	
	// Raytrace blocks
	// @param from beginning of ray
	// @param direction direction of ray (should be large value like 0,-1e6,0)
	// @param result if return value was HIT, the block position, else {-1.5..}
	// @param stopOnLiquid whether the ray should stop on liquid blocks

	RAYTRACE_BLOCKS_RESULT rayTraceBlocks(Vec3 from, Vec3 direction, Vec3& result, bool stopOnLiquid = false);
	
	// Get block position from game's objectMouseOver (more reliable than custom raycast)
	Vec3 getTargetedBlockPos();

	// Input control (JNI)
	void setKeyBindState(int keyCode, bool pressed);
	int getSneakKey(); // Returns key code for sneak keybind
	
	// Mouse grab control (for menu open/close)
	// Releases or grabs mouse at LWJGL level to prevent camera movement when menu is open
	void ungrabMouse();
	void grabMouse();

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
    jfieldID fid_pressed = nullptr; // KeyBinding.pressed (boolean)

    // Store key states to restore properly
    // Map of keyBindName -> wasPressed
    std::map<std::string, bool> savedKeyStates;
    bool keysReleased = false;

	friend void SDK_Init();
};

// Global SDK instance
extern Minecraft mc;

// Initialize SDK after Java::Init()
void SDK_Init();
