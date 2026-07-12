#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "../../jni/java.h"
#include "../jni/jstring_util.h"
#include "../player/player.h"

// Data for a remote player in the world
struct PlayerInfo {
    jobject instance = nullptr;
    int entityId = 0;
    std::string uuid;  // Stable player identity (Entity.getUniqueID().toString())
    std::string name;
    std::string displayName;
    std::string profileName;  // GameProfile name (stable, unspoofable)
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
    bool isOnLocalTeam = false;  // Vanilla scoreboard comparison against the local player
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

    struct ArmorItem {
        int id = 0;
        int damage = 0;
        int maxDamage = 0;
        int protection = 0;  // Protection enchant level (0 = none)
        int unbreaking = 0;
        int fireProtection = 0;
        int projectileProtection = 0;
        int blastProtection = 0;
        int featherFalling = 0;
    };

    struct PotionEffectInfo {
        int id = 0;
        int duration = 0;
        int amplifier = 0;
    };

    // Currently held weapon (sword/axe/bow). id == 0 means none / not synced by server.
    struct HeldItem {
        int id = 0;
        int damage = 0;
        int maxDamage = 0;
        int sharpness = 0;  // Sharpness enchant level (0 = none)
        int unbreaking = 0;
        int fireAspect = 0;
        int knockback = 0;
        int power = 0;
        int punch = 0;
        int flame = 0;
        int infinity = 0;
    };

    std::vector<ArmorItem> armor;
    std::vector<PotionEffectInfo> potions;
    HeldItem weapon;
    int totalHealthPotions = 0;
};

struct ItemEntityInfo {
    jobject instance = nullptr; // Caller must delete
    double posX = 0, posY = 0, posZ = 0;
    int itemId = 0;
    int damage = 0; // Metadata
    int stackSize = 0;
    int maxStackSize = 64;
    bool hasCustomName = false;  // Item carries a server-set NBT name
    std::string customName;      // Raw display name, may include § color codes
    bool isStackable() const { return maxStackSize > 1; }
};

// Render position for WorldToScreen calculations
struct RenderPos {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// One captured non-air block for a Cobble Spectate world snapshot. Coordinates
// are relative to the snapshot origin (host position).
struct BlockSample {
    int16_t dx = 0, dy = 0, dz = 0;
    uint16_t id = 0;
};

// Item slot info for inventory operations
struct SlotInfo {
    int slotId = -1;
    int itemId = 0;  // 0 = empty, 282 = soup, 373 = potion, etc
    int stackSize = 0;
    int damage = 0;           // Durability damage or potion metadata
    int protectionLevel = 0;  // Protection enchantment level (0 = none)
    bool isSplashPotion = false;
    bool isHealthPotion = false;  // Instant Health I or II
};

// Simplified wrapper for net.minecraft.client.Minecraft
// Only exposes what's needed for basic ghost client
struct Minecraft {
    bool isValid() const { return instance != nullptr && cls != nullptr; }

    // Get local player instance (thePlayer)
    jobject getPlayer();

    // Get current world (theWorld)
    jobject getWorld();

    // Check if GUI is open (chat, inventory, etc)
    bool isInGui();

    // Mouse click (for autoclicker)
    void clickMouse();
    bool attackEntity(jobject target);
    void rightClickMouse();
    void updateMouseOver();

    // Click delays
    int getRightClickDelay();
    void setRightClickDelay(int value);
    void setLeftClickCounter(int value);

    // MouseOver - check what player is looking at
    bool isTargetingBlock();

    // Get all players in the world (excluding local player)
    std::vector<PlayerInfo> getPlayerList();

    // Get all item entities
    std::vector<ItemEntityInfo> getItemEntityList();

    // Find a specific player by name and return their info (including UUID)
    // Returns empty PlayerInfo if not found
    // Use this to resolve name → UUID when adding friends
    PlayerInfo findPlayerByName(const std::string& name);

    // Check if local player can see target entity
    bool canEntityBeSeen(jobject target);

    // Render info for ESP/NameTags
    RenderPos getRenderPos();
    float getPartialTicks();

    // renderMode: 0 = Fill (solid flat color), 1 = Chams (stencil outline aura)
    void renderGhostEntity(int entityId, double wx, double wy, double wz, float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 0.42f, bool disableDepth = false, int renderMode = 0);

    // Mouse sensitivity (for proper aim assist scaling)
    float getMouseSensitivity();

    // Real in-game FPS (reads the static Minecraft.debugFPS field).
    // Must be called on the main JNI thread; cache it for the render thread.
    int getGameFps();

    // Timer speed control (1.0 = normal, higher = faster game speed)
    void setTimerSpeed(float speed);

    // Inventory operations for AutoRefill
    std::vector<SlotInfo> getInventorySlots();                 // Get all 36 inventory slots
    std::vector<SlotInfo> getArmorSlots();                     // Get 4 armor slots (0=helmet, 1=chest, 2=legs, 3=boots)
    void windowClick(int slotId, int button);                  // Click inventory slot (shift-click)
    void windowClickSwap(int containerSlot, int hotbarIndex);  // Swap container slot with hotbar (mode 2)
    void openInventory();                                      // Open player inventory GUI
    void closeInventory();                                     // Close current GUI
    bool isInventoryOpen();                                    // Check if any container GUI is open
    bool isPlayerInventoryOpen();                              // Check if player inventory (E key) is open

    // Get local player's team color from their displayName
    // Returns -1 (UNKNOWN) if no color code found
    int getLocalPlayerTeamColor();
    std::string getLocalPlayerName();

    // Hit detection for Hit Select module
    bool isTargetingEntity();            // Check if looking at an entity
    int getTargetedEntityHurtTime();     // Get hurtTime of targeted entity (0 = can take damage)
    PlayerInfo getTargetedPlayerInfo();  // Get info (UUID, name) of targeted player (for quick-add friend)

    // Movement control - release all movement keybinds
    void releaseMovementKeys();
    // Restore keys that are physically held down
    void restoreMovementKeys();
    void cleanup();

    // World block query for Bridge Assist
    // Returns block ID at given coordinates (0 = air)
    int getBlockId(int x, int y, int z);

    // Bulk-capture non-air blocks in a box around (originX,originY,originZ) by
    // reading chunk-section arrays directly (Cobble Spectate world snapshots).
    // Best-effort: leaves `out` empty if the JNI chunk layout can't be resolved.
    // Must run on the JVM thread.
    void captureBlocks(int radiusXZ, int radiusY, int originX, int originY, int originZ, size_t maxBlocks,
                       std::vector<BlockSample>& out);
    bool damageBlock(int x, int y, int z);

    // Damage the block under the crosshair using the game's own objectMouseOver
    // block position AND side. Because pos+facing match vanilla exactly, the
    // START/STOP digging packets stay consistent and the server accepts the
    // break instead of reverting it (avoids ghost blocks on fast break).
    bool damageTargetedBlock();

    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
    };

    enum class RAYTRACE_BLOCKS_RESULT {
        HIT,                   // hit a block
        HIT_FROM_AIRBLOCK,     // hit a block but ray origin was airblock
        NO_HIT,                // the void
        NO_HIT_FROM_AIRBLOCK,  // the void but ray origin was airblock
        RAYTRACE_ERROR         // debugging purposes (renamed from ERROR to avoid Windows macro)
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
    int getSneakKey();      // Returns key code for sneak keybind
    int getSprintKey();     // Returns key code for sprint keybind
    int getForwardKey();    // Returns key code for forward keybind
    int getJumpKey();       // Returns key code for jump keybind (default space)
    int getBackKey();       // Returns key code for back keybind
    int getInventoryKey();  // Returns key code for inventory keybind (default 'E')

    // Mouse grab control (for menu open/close)
    // Releases or grabs mouse at LWJGL level to prevent camera movement when menu is open
    void ungrabMouse();
    void grabMouse();

    // Send a packet to the server immediately (addToSendQueue)
    void sendPacket(jobject packet);
    int getLocalPlayerPing();
    bool isFirstPerson();
    std::string getCurrentServerIP();

    // Helper to open inventory purely via packet (Silent Mode)
    void sendInventoryOpenPacket();

    // Helper to close inventory purely via packet (Silent Mode)
    void sendInventoryClosePacket();

    jclass cls = nullptr;
    jobject instance = nullptr;

private:
    // Cached IDs (filled in SDK_Init)
    jmethodID mid_getMinecraft = nullptr;
    jfieldID fid_thePlayer = nullptr;
    jfieldID fid_theWorld = nullptr;
    jfieldID fid_currentScreen = nullptr;
    jmethodID mid_clickMouse = nullptr;
    jmethodID mid_rightClickMouse = nullptr;
    jfieldID fid_rightClickDelayTimer = nullptr;
    jfieldID fid_leftClickCounter = nullptr;

    // Cached for release/restore keys (thread-safe)
    jclass cls_keyBinding = nullptr;
    jmethodID mid_unPressAllKeys = nullptr;
    jmethodID mid_setKeyBindState = nullptr;
    jmethodID mid_getKeyCode = nullptr;
    jfieldID fid_pressed = nullptr;  // KeyBinding.pressed (boolean)

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

// Poll for a world/server/dimension change (main JNI loop only).
// On change: re-resolves JNI IDs via Reinit(), clears stale SDK caches.
// Returns true on the tick a change is detected.
bool SDK_PollWorldChange();
