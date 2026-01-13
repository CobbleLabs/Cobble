#pragma once

#include "../../jni/java.h"

// Centralized JNI ID cache for better performance
// All IDs are initialized once in JniCache::Init() and reused
namespace JniCache {

// =============================================================================
// Classes (stored as GlobalRefs for thread safety)
// =============================================================================

// Core classes
extern jclass cls_Minecraft;
extern jclass cls_WorldClient;
extern jclass cls_EntityPlayerSP;        // 1.8.x local player
extern jclass cls_EntityClientPlayerMP;  // 1.7.10 local player

// Entity hierarchy
extern jclass cls_Entity;
extern jclass cls_EntityLivingBase;
extern jclass cls_EntityPlayer;
extern jclass cls_EntityArmorStand;

// Settings & Input
extern jclass cls_GameSettings;
extern jclass cls_KeyBinding;
extern jclass cls_MouseHelper;

// World & Rendering
extern jclass cls_RenderManager;
extern jclass cls_MovingObjectPosition;
extern jclass cls_MovingObjectType;
extern jclass cls_BlockPos;  // 1.8.x only
extern jclass cls_Vec3;
extern jclass cls_Block;
extern jclass cls_IBlockState;
extern jclass cls_Timer;

// GUI & Inventory
extern jclass cls_GuiScreen;
extern jclass cls_GuiInventory;
extern jclass cls_Container;
extern jclass cls_Slot;
extern jclass cls_ItemStack;

// Chat
extern jclass cls_IChatComponent;

// Java utils
extern jclass cls_List;

// LWJGL (for mouse grab fallback)
extern jclass cls_Mouse;

// =============================================================================
// Minecraft class IDs
// =============================================================================

// Static methods
extern jmethodID mid_getMinecraft;

// Instance methods
extern jmethodID mid_clickMouse;
extern jmethodID mid_getNetHandler;  // getNetHandler() -> NetHandlerPlayClient

// Fields
extern jfieldID fid_thePlayer;
extern jfieldID fid_theWorld;
extern jfieldID fid_currentScreen;
extern jfieldID fid_rightClickDelayTimer;
extern jfieldID fid_leftClickCounter;
extern jfieldID fid_objectMouseOver;
extern jfieldID fid_gameSettings;
extern jfieldID fid_renderManager;
extern jfieldID fid_mouseHelper;
extern jfieldID fid_inGameHasFocus;
extern jfieldID fid_timer;
extern jfieldID fid_playerController;

// =============================================================================
// Entity class IDs
// =============================================================================

// Fields (Entity)
extern jfieldID fid_posX;
extern jfieldID fid_posY;
extern jfieldID fid_posZ;
extern jfieldID fid_lastTickPosX;
extern jfieldID fid_lastTickPosY;
extern jfieldID fid_lastTickPosZ;

// Methods (Entity)
extern jmethodID mid_isInvisible;
extern jmethodID mid_getUniqueID;  // Entity.getUniqueID() -> java.util.UUID

// java.util.UUID class
extern jclass cls_UUID;
extern jmethodID mid_uuidToString;  // UUID.toString() -> String

// =============================================================================
// NetHandlerPlayClient class IDs (for TabList lookup)
// =============================================================================

extern jclass cls_NetHandlerPlayClient;
extern jmethodID mid_getPlayerInfoByUUID;  // getPlayerInfo(UUID) -> NetworkPlayerInfo

// =============================================================================
// NetworkPlayerInfo class IDs
// =============================================================================

extern jclass cls_NetworkPlayerInfo;
extern jmethodID mid_getNetworkGameProfile;  // getGameProfile() -> GameProfile

// =============================================================================
// EntityLivingBase class IDs
// =============================================================================

extern jmethodID mid_getHealth;
extern jmethodID mid_getMaxHealth;
extern jfieldID fid_hurtTime;
extern jfieldID fid_hurtResistantTime;
extern jfieldID fid_maxHurtResistantTime;
extern jmethodID mid_canEntityBeSeen;

// Bounding box (Entity.boundingBox -> AxisAlignedBB)
extern jfieldID fid_boundingBox;
extern jmethodID mid_isSneaking;

// AxisAlignedBB fields (for hitbox dimensions)
extern jclass cls_AxisAlignedBB;
extern jfieldID fid_aabb_minX;
extern jfieldID fid_aabb_minY;
extern jfieldID fid_aabb_minZ;
extern jfieldID fid_aabb_maxX;
extern jfieldID fid_aabb_maxY;
extern jfieldID fid_aabb_maxZ;

// =============================================================================
// EntityPlayer class IDs
// =============================================================================

extern jmethodID mid_getName;
extern jmethodID mid_getDisplayName;
extern jmethodID mid_getGameProfile;  // EntityPlayer.getGameProfile() -> GameProfile
extern jfieldID fid_inventory;
extern jfieldID fid_openContainer;

// =============================================================================
// GameProfile class IDs (com.mojang.authlib.GameProfile)
// =============================================================================

extern jclass cls_GameProfile;
extern jmethodID mid_getProfileName;  // GameProfile.getName() -> String (real name)

// =============================================================================
// GameSettings class IDs
// =============================================================================

extern jfieldID fid_mouseSensitivity;
extern jfieldID fid_keyBindForward;
extern jfieldID fid_keyBindBack;
extern jfieldID fid_keyBindLeft;
extern jfieldID fid_keyBindRight;
extern jfieldID fid_keyBindJump;
extern jfieldID fid_keyBindSneak;
extern jfieldID fid_keyBindAttack;
extern jfieldID fid_keyBindUseItem;

// =============================================================================
// KeyBinding class IDs
// =============================================================================

extern jmethodID mid_setKeyBindState;
extern jmethodID mid_unPressAllKeys;
extern jmethodID mid_getKeyCode;
extern jfieldID fid_pressed;

// =============================================================================
// MouseHelper class IDs
// =============================================================================

extern jmethodID mid_ungrabMouseCursor;
extern jmethodID mid_grabMouseCursor;

// =============================================================================
// RenderManager class IDs
// =============================================================================

// Static fields (1.7.10)
extern jfieldID fid_renderPosX_static;
extern jfieldID fid_renderPosY_static;
extern jfieldID fid_renderPosZ_static;

// Instance fields (1.8.x)
extern jfieldID fid_renderPosX;
extern jfieldID fid_renderPosY;
extern jfieldID fid_renderPosZ;

// =============================================================================
// Timer class IDs
// =============================================================================

// Timer instance fields
extern jfieldID fid_renderPartialTicks;
extern jfieldID fid_timerSpeed;

// =============================================================================
// MovingObjectPosition class IDs
// =============================================================================

extern jfieldID fid_typeOfHit;
extern jfieldID fid_entityHit;
extern jfieldID fid_blockX;  // 1.7.10
extern jfieldID fid_blockY;
extern jfieldID fid_blockZ;
extern jmethodID mid_getBlockPos;  // 1.8.x

// =============================================================================
// MovingObjectType enum IDs
// =============================================================================

extern jfieldID fid_BLOCK;
extern jfieldID fid_ENTITY;

// =============================================================================
// World class IDs
// =============================================================================

extern jfieldID fid_playerEntities;
extern jmethodID mid_getBlockState;
extern jmethodID mid_getBlockWorld;  // 1.7.10: World.getBlock(int,int,int) -> Block
extern jmethodID mid_rayTraceBlocks;

// =============================================================================
// BlockPos class IDs (1.8.x)
// =============================================================================

extern jmethodID mid_blockPosInit;
extern jmethodID mid_getX;
extern jmethodID mid_getY;
extern jmethodID mid_getZ;

// =============================================================================
// IBlockState class IDs
// =============================================================================

extern jmethodID mid_getBlock;

// =============================================================================
// Block class IDs
// =============================================================================

extern jmethodID mid_getIdFromBlock;

// =============================================================================
// Vec3 class IDs
// =============================================================================

extern jmethodID mid_vec3Init;

// =============================================================================
// Inventory & Container class IDs
// =============================================================================

extern jfieldID fid_mainInventory;
extern jfieldID fid_inventorySlots;
extern jfieldID fid_windowId;
extern jmethodID mid_getStackInSlot;
extern jmethodID mid_windowClick;

// =============================================================================
// ItemStack class IDs
// =============================================================================

extern jmethodID mid_getItem;
extern jfieldID fid_stackSize;
extern jmethodID mid_getItemDamage;

// =============================================================================
// Slot class IDs
// =============================================================================

extern jmethodID mid_getStack;

// =============================================================================
// List class IDs
// =============================================================================

extern jmethodID mid_listSize;
extern jmethodID mid_listGet;

// =============================================================================
// IChatComponent class IDs
// =============================================================================

extern jmethodID mid_getFormattedText;
extern jmethodID mid_getUnformattedText;

// =============================================================================
// LWJGL Mouse class IDs
// =============================================================================

extern jmethodID mid_setGrabbed;
// =============================================================================
// Initialization & Cleanup
// =============================================================================

// Initialize all cached IDs. Call from SDK_Init() after Java::Init()
// Returns true if core IDs were found, false on critical failure
bool Init();

// Cleanup all GlobalRefs. Call before DLL unload
void Cleanup();

// Check if cache is initialized
bool IsInitialized();

// Quick sanity check - verifies core classes are still valid
// Returns false if cache appears corrupted
inline bool quickCheck() {
    return cls_Minecraft && cls_Entity && cls_EntityPlayer && 
           mid_getMinecraft && fid_thePlayer;
}

}  // namespace JniCache

