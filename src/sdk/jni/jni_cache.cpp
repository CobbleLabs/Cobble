#include "jni_cache.h"
#include "../../util/logger.hpp"

namespace JniCache {

// =============================================================================
// Static storage for all cached IDs
// =============================================================================

static bool g_initialized = false;

// Classes
jclass cls_Minecraft = nullptr;
jclass cls_WorldClient = nullptr;
jclass cls_EntityPlayerSP = nullptr;
jclass cls_EntityClientPlayerMP = nullptr;
jclass cls_Entity = nullptr;
jclass cls_EntityLivingBase = nullptr;
jclass cls_EntityPlayer = nullptr;
jclass cls_EntityArmorStand = nullptr;
jclass cls_GameSettings = nullptr;
jclass cls_KeyBinding = nullptr;
jclass cls_MouseHelper = nullptr;
jclass cls_RenderManager = nullptr;
jclass cls_MovingObjectPosition = nullptr;
jclass cls_MovingObjectType = nullptr;
jclass cls_BlockPos = nullptr;
jclass cls_Vec3 = nullptr;
jclass cls_Block = nullptr;
jclass cls_IBlockState = nullptr;
jclass cls_Timer = nullptr;
jclass cls_GuiScreen = nullptr;
jclass cls_GuiInventory = nullptr;
jclass cls_Container = nullptr;
jclass cls_Slot = nullptr;
jclass cls_ItemStack = nullptr;
jclass cls_IChatComponent = nullptr;
jclass cls_List = nullptr;
jclass cls_Mouse = nullptr;

// Minecraft IDs
jmethodID mid_getMinecraft = nullptr;
jmethodID mid_clickMouse = nullptr;
jmethodID mid_getNetHandler = nullptr;
jfieldID fid_thePlayer = nullptr;
jfieldID fid_theWorld = nullptr;
jfieldID fid_currentScreen = nullptr;
jfieldID fid_rightClickDelayTimer = nullptr;
jfieldID fid_leftClickCounter = nullptr;
jfieldID fid_objectMouseOver = nullptr;
jfieldID fid_gameSettings = nullptr;
jfieldID fid_renderManager = nullptr;
jfieldID fid_mouseHelper = nullptr;
jfieldID fid_inGameHasFocus = nullptr;
jfieldID fid_timer = nullptr;
jfieldID fid_playerController = nullptr;

// Entity IDs
jfieldID fid_posX = nullptr;
jfieldID fid_posY = nullptr;
jfieldID fid_posZ = nullptr;
jfieldID fid_lastTickPosX = nullptr;
jfieldID fid_lastTickPosY = nullptr;
jfieldID fid_lastTickPosZ = nullptr;
jmethodID mid_isInvisible = nullptr;
jmethodID mid_getUniqueID = nullptr;

// java.util.UUID IDs
jclass cls_UUID = nullptr;
jmethodID mid_uuidToString = nullptr;

// NetHandlerPlayClient IDs (for TabList lookup)
jclass cls_NetHandlerPlayClient = nullptr;
jmethodID mid_getPlayerInfoByUUID = nullptr;

// NetworkPlayerInfo IDs
jclass cls_NetworkPlayerInfo = nullptr;
jmethodID mid_getNetworkGameProfile = nullptr;

jmethodID mid_getHealth = nullptr;
jmethodID mid_getMaxHealth = nullptr;
jfieldID fid_hurtTime = nullptr;
jfieldID fid_hurtResistantTime = nullptr;
jfieldID fid_maxHurtResistantTime = nullptr;
jmethodID mid_canEntityBeSeen = nullptr;
jfieldID fid_boundingBox = nullptr;
jmethodID mid_isSneaking = nullptr;

// AxisAlignedBB class and fields
jclass cls_AxisAlignedBB = nullptr;
jfieldID fid_aabb_minX = nullptr;
jfieldID fid_aabb_minY = nullptr;
jfieldID fid_aabb_minZ = nullptr;
jfieldID fid_aabb_maxX = nullptr;
jfieldID fid_aabb_maxY = nullptr;
jfieldID fid_aabb_maxZ = nullptr;

// EntityPlayer IDs
jmethodID mid_getName = nullptr;
jmethodID mid_getDisplayName = nullptr;
jmethodID mid_getGameProfile = nullptr;
jfieldID fid_inventory = nullptr;
jfieldID fid_openContainer = nullptr;

// GameProfile IDs (com.mojang.authlib.GameProfile)
jclass cls_GameProfile = nullptr;
jmethodID mid_getProfileName = nullptr;

// GameSettings IDs
jfieldID fid_mouseSensitivity = nullptr;
jfieldID fid_keyBindForward = nullptr;
jfieldID fid_keyBindBack = nullptr;
jfieldID fid_keyBindLeft = nullptr;
jfieldID fid_keyBindRight = nullptr;
jfieldID fid_keyBindJump = nullptr;
jfieldID fid_keyBindSneak = nullptr;
jfieldID fid_keyBindSprint = nullptr;
jfieldID fid_keyBindAttack = nullptr;
jfieldID fid_keyBindUseItem = nullptr;

// KeyBinding IDs
jmethodID mid_setKeyBindState = nullptr;
jmethodID mid_unPressAllKeys = nullptr;
jmethodID mid_getKeyCode = nullptr;
jfieldID fid_pressed = nullptr;

// MouseHelper IDs
jmethodID mid_ungrabMouseCursor = nullptr;
jmethodID mid_grabMouseCursor = nullptr;

// RenderManager IDs
jfieldID fid_renderPosX_static = nullptr;
jfieldID fid_renderPosY_static = nullptr;
jfieldID fid_renderPosZ_static = nullptr;
jfieldID fid_renderPosX = nullptr;
jfieldID fid_renderPosY = nullptr;
jfieldID fid_renderPosZ = nullptr;

// Timer IDs
jfieldID fid_renderPartialTicks = nullptr;
jfieldID fid_timerSpeed = nullptr;

// MovingObjectPosition IDs
jfieldID fid_typeOfHit = nullptr;
jfieldID fid_entityHit = nullptr;
jfieldID fid_blockX = nullptr;
jfieldID fid_blockY = nullptr;
jfieldID fid_blockZ = nullptr;
jmethodID mid_getBlockPos = nullptr;

// MovingObjectType IDs
jfieldID fid_BLOCK = nullptr;
jfieldID fid_ENTITY = nullptr;

// World IDs
jfieldID fid_playerEntities = nullptr;
jfieldID fid_loadedEntityList = nullptr;
jmethodID mid_getBlockState = nullptr;
jmethodID mid_getBlockWorld = nullptr;  // 1.7.10: World.getBlock(int,int,int)
jmethodID mid_rayTraceBlocks = nullptr;

// BlockPos IDs
jmethodID mid_blockPosInit = nullptr;
jmethodID mid_getX = nullptr;
jmethodID mid_getY = nullptr;
jmethodID mid_getZ = nullptr;

// IBlockState IDs
jmethodID mid_getBlock = nullptr;

// Block IDs
jmethodID mid_getIdFromBlock = nullptr;

// Vec3 IDs
jmethodID mid_vec3Init = nullptr;

// Inventory IDs
jfieldID fid_mainInventory = nullptr;
jfieldID fid_inventorySlots = nullptr;
jfieldID fid_windowId = nullptr;
jmethodID mid_getStackInSlot = nullptr;
jmethodID mid_windowClick = nullptr;

// ItemStack IDs
jmethodID mid_getItem = nullptr;
jfieldID fid_stackSize = nullptr;
jmethodID mid_getItemDamage = nullptr;
jfieldID fid_itemDamage = nullptr;

// Slot IDs
jmethodID mid_getStack = nullptr;

// List IDs
jmethodID mid_listSize = nullptr;
jmethodID mid_listGet = nullptr;

// IChatComponent IDs
jmethodID mid_getFormattedText = nullptr;
jmethodID mid_getUnformattedText = nullptr;

// LWJGL Mouse IDs
jmethodID mid_setGrabbed = nullptr;

// =============================================================================
// Helper macros for cleaner initialization
// =============================================================================

#define CACHE_CLASS(var, name)                      \
    do {                                            \
        jclass local = nullptr;                     \
        if (Java::AssignClass(name, local)) {       \
            var = (jclass)env->NewGlobalRef(local); \
            env->DeleteLocalRef(local);             \
        }                                           \
    } while (0)

#define CACHE_CLASS_CRITICAL(var, name)                                \
    do {                                                               \
        jclass local = nullptr;                                        \
        if (!Java::AssignClass(name, local)) {                         \
            log_err("JniCache: Failed to find critical class: " name); \
            return false;                                              \
        }                                                              \
        var = (jclass)env->NewGlobalRef(local);                        \
        env->DeleteLocalRef(local);                                    \
    } while (0)

#define TRY_METHOD(var, cls, name, sig)         \
    do {                                        \
        var = env->GetMethodID(cls, name, sig); \
        if (!var)                               \
            env->ExceptionClear();              \
    } while (0)

#define TRY_STATIC_METHOD(var, cls, name, sig)        \
    do {                                              \
        var = env->GetStaticMethodID(cls, name, sig); \
        if (!var)                                     \
            env->ExceptionClear();                    \
    } while (0)

#define TRY_FIELD(var, cls, name, sig)         \
    do {                                       \
        var = env->GetFieldID(cls, name, sig); \
        if (!var)                              \
            env->ExceptionClear();             \
    } while (0)

#define TRY_STATIC_FIELD(var, cls, name, sig)        \
    do {                                             \
        var = env->GetStaticFieldID(cls, name, sig); \
        if (!var)                                    \
            env->ExceptionClear();                   \
    } while (0)

// =============================================================================
// Initialization
// =============================================================================

bool Init() {
    if (g_initialized)
        return true;

    JNIEnv* env = Java::Env();
    if (!env) {
        log_err("JniCache::Init: No JNI environment");
        return false;
    }

    bool is_1_7_10 = (Java::Version == MinecraftVersion::MC_1_7_10);

    // =========================================================================
    // Critical classes (fail if not found)
    // =========================================================================

    CACHE_CLASS_CRITICAL(cls_Minecraft, "net.minecraft.client.Minecraft");
    CACHE_CLASS_CRITICAL(cls_Entity, "net.minecraft.entity.Entity");
    CACHE_CLASS_CRITICAL(cls_EntityPlayer, "net.minecraft.entity.player.EntityPlayer");

    // =========================================================================
    // Optional classes (log warning if not found)
    // =========================================================================

    CACHE_CLASS(cls_WorldClient, "net.minecraft.client.multiplayer.WorldClient");
    CACHE_CLASS(cls_EntityLivingBase, "net.minecraft.entity.EntityLivingBase");
    CACHE_CLASS(cls_EntityArmorStand, "net.minecraft.entity.item.EntityArmorStand");
    CACHE_CLASS(cls_GameSettings, "net.minecraft.client.settings.GameSettings");
    CACHE_CLASS(cls_KeyBinding, "net.minecraft.client.settings.KeyBinding");
    CACHE_CLASS(cls_MouseHelper, "net.minecraft.util.MouseHelper");
    CACHE_CLASS(cls_RenderManager, "net.minecraft.client.renderer.entity.RenderManager");
    CACHE_CLASS(cls_MovingObjectPosition, "net.minecraft.util.MovingObjectPosition");
    CACHE_CLASS(cls_Vec3, "net.minecraft.util.Vec3");
    CACHE_CLASS(cls_GuiScreen, "net.minecraft.client.gui.GuiScreen");
    CACHE_CLASS(cls_GuiInventory, "net.minecraft.client.gui.inventory.GuiInventory");
    CACHE_CLASS(cls_Container, "net.minecraft.inventory.Container");
    CACHE_CLASS(cls_Slot, "net.minecraft.inventory.Slot");
    CACHE_CLASS(cls_ItemStack, "net.minecraft.item.ItemStack");
    CACHE_CLASS(cls_Block, "net.minecraft.block.Block");
    CACHE_CLASS(cls_Timer, "net.minecraft.util.Timer");

    // Version-specific classes
    if (is_1_7_10) {
        CACHE_CLASS(cls_EntityClientPlayerMP, "net.minecraft.client.entity.EntityClientPlayerMP");
        CACHE_CLASS(cls_IChatComponent, "net.minecraft.util.IChatComponent");
    } else {
        CACHE_CLASS(cls_EntityPlayerSP, "net.minecraft.client.entity.EntityPlayerSP");
        CACHE_CLASS(cls_BlockPos, "net.minecraft.util.BlockPos");
        CACHE_CLASS(cls_IBlockState, "net.minecraft.block.state.IBlockState");
        CACHE_CLASS(cls_IChatComponent, "net.minecraft.util.IChatComponent");
    }

    // Java classes
    cls_List = env->FindClass("java/util/List");
    if (cls_List)
        cls_List = (jclass)env->NewGlobalRef(cls_List);

    // LWJGL (may not be available)
    jclass mouseLocal = env->FindClass("org/lwjgl/input/Mouse");
    if (mouseLocal) {
        cls_Mouse = (jclass)env->NewGlobalRef(mouseLocal);
        env->DeleteLocalRef(mouseLocal);
    }
    env->ExceptionClear();

    // =========================================================================
    // Minecraft class IDs
    // =========================================================================

    TRY_STATIC_METHOD(mid_getMinecraft, cls_Minecraft, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    TRY_METHOD(mid_clickMouse, cls_Minecraft, "clickMouse", "()V");
    TRY_METHOD(mid_getNetHandler, cls_Minecraft, "getNetHandler",
               "()Lnet/minecraft/client/network/NetHandlerPlayClient;");

    // Version-specific player field
    if (is_1_7_10) {
        TRY_FIELD(fid_thePlayer, cls_Minecraft, "thePlayer", "Lnet/minecraft/client/entity/EntityClientPlayerMP;");
    } else {
        TRY_FIELD(fid_thePlayer, cls_Minecraft, "thePlayer", "Lnet/minecraft/client/entity/EntityPlayerSP;");
    }

    TRY_FIELD(fid_theWorld, cls_Minecraft, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");
    TRY_FIELD(fid_currentScreen, cls_Minecraft, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");
    TRY_FIELD(fid_rightClickDelayTimer, cls_Minecraft, "rightClickDelayTimer", "I");
    TRY_FIELD(fid_leftClickCounter, cls_Minecraft, "leftClickCounter", "I");
    TRY_FIELD(fid_objectMouseOver, cls_Minecraft, "objectMouseOver", "Lnet/minecraft/util/MovingObjectPosition;");
    TRY_FIELD(fid_gameSettings, cls_Minecraft, "gameSettings", "Lnet/minecraft/client/settings/GameSettings;");
    TRY_FIELD(fid_renderManager, cls_Minecraft, "renderManager",
              "Lnet/minecraft/client/renderer/entity/RenderManager;");
    TRY_FIELD(fid_mouseHelper, cls_Minecraft, "mouseHelper", "Lnet/minecraft/util/MouseHelper;");
    TRY_FIELD(fid_inGameHasFocus, cls_Minecraft, "inGameHasFocus", "Z");
    TRY_FIELD(fid_timer, cls_Minecraft, "timer", "Lnet/minecraft/util/Timer;");
    TRY_FIELD(fid_playerController, cls_Minecraft, "playerController",
              "Lnet/minecraft/client/multiplayer/PlayerControllerMP;");

    // =========================================================================
    // Entity class IDs
    // =========================================================================

    if (cls_Entity) {
        TRY_FIELD(fid_posX, cls_Entity, "posX", "D");
        TRY_FIELD(fid_posY, cls_Entity, "posY", "D");
        TRY_FIELD(fid_posZ, cls_Entity, "posZ", "D");
        TRY_FIELD(fid_lastTickPosX, cls_Entity, "lastTickPosX", "D");
        TRY_FIELD(fid_lastTickPosY, cls_Entity, "lastTickPosY", "D");
        TRY_FIELD(fid_lastTickPosZ, cls_Entity, "lastTickPosZ", "D");
        TRY_METHOD(mid_isInvisible, cls_Entity, "isInvisible", "()Z");
        TRY_METHOD(mid_getUniqueID, cls_Entity, "getUniqueID", "()Ljava/util/UUID;");
        TRY_FIELD(fid_boundingBox, cls_Entity, "boundingBox", "Lnet/minecraft/util/AxisAlignedBB;");
        if (!fid_boundingBox) {
            TRY_FIELD(fid_boundingBox, cls_Entity, "entityBoundingBox",
                      "Lnet/minecraft/util/AxisAlignedBB;");  // 1.8.x field name
        }
    }

    // java.util.UUID class (for player identity)
    jclass uuidLocal = env->FindClass("java/util/UUID");
    if (uuidLocal) {
        cls_UUID = (jclass)env->NewGlobalRef(uuidLocal);
        env->DeleteLocalRef(uuidLocal);
        if (cls_UUID) {
            TRY_METHOD(mid_uuidToString, cls_UUID, "toString", "()Ljava/lang/String;");
        }
    }
    env->ExceptionClear();

    // NetHandlerPlayClient class (for TabList lookup by UUID)
    CACHE_CLASS(cls_NetHandlerPlayClient, "net.minecraft.client.network.NetHandlerPlayClient");
    if (cls_NetHandlerPlayClient) {
        // getPlayerInfo(UUID) -> NetworkPlayerInfo
        TRY_METHOD(mid_getPlayerInfoByUUID, cls_NetHandlerPlayClient, "getPlayerInfo",
                   "(Ljava/util/UUID;)Lnet/minecraft/client/network/NetworkPlayerInfo;");
        log_debug("[JniCache] NetHandlerPlayClient.getPlayerInfo(UUID): %s", mid_getPlayerInfoByUUID ? "OK" : "FAILED");
    } else {
        log_debug("[JniCache] NetHandlerPlayClient class: FAILED");
    }

    // NetworkPlayerInfo class (contains real GameProfile)
    CACHE_CLASS(cls_NetworkPlayerInfo, "net.minecraft.client.network.NetworkPlayerInfo");
    if (cls_NetworkPlayerInfo) {
        // getGameProfile() -> com.mojang.authlib.GameProfile
        TRY_METHOD(mid_getNetworkGameProfile, cls_NetworkPlayerInfo, "getGameProfile",
                   "()Lcom/mojang/authlib/GameProfile;");
        log_debug("[JniCache] NetworkPlayerInfo.getGameProfile(): %s", mid_getNetworkGameProfile ? "OK" : "FAILED");
    } else {
        log_debug("[JniCache] NetworkPlayerInfo class: FAILED");
    }

    // Log Minecraft.getNetHandler status
    log_debug("[JniCache] Minecraft.getNetHandler(): %s", mid_getNetHandler ? "OK" : "FAILED");
    log_debug("[JniCache] GameProfile.getName(): %s", mid_getProfileName ? "OK" : "FAILED");

    // AxisAlignedBB class and fields (for hitbox)
    CACHE_CLASS(cls_AxisAlignedBB, "net.minecraft.util.AxisAlignedBB");
    if (cls_AxisAlignedBB) {
        TRY_FIELD(fid_aabb_minX, cls_AxisAlignedBB, "minX", "D");
        TRY_FIELD(fid_aabb_minY, cls_AxisAlignedBB, "minY", "D");
        TRY_FIELD(fid_aabb_minZ, cls_AxisAlignedBB, "minZ", "D");
        TRY_FIELD(fid_aabb_maxX, cls_AxisAlignedBB, "maxX", "D");
        TRY_FIELD(fid_aabb_maxY, cls_AxisAlignedBB, "maxY", "D");
        TRY_FIELD(fid_aabb_maxZ, cls_AxisAlignedBB, "maxZ", "D");
    }

    // =========================================================================
    // EntityLivingBase class IDs
    // =========================================================================

    if (cls_EntityLivingBase) {
        TRY_METHOD(mid_getHealth, cls_EntityLivingBase, "getHealth", "()F");
        TRY_METHOD(mid_getMaxHealth, cls_EntityLivingBase, "getMaxHealth", "()F");
        TRY_FIELD(fid_hurtTime, cls_EntityLivingBase, "hurtTime", "I");
        TRY_FIELD(fid_hurtResistantTime, cls_EntityLivingBase, "hurtResistantTime", "I");
        TRY_FIELD(fid_maxHurtResistantTime, cls_EntityLivingBase, "maxHurtResistantTime", "I");
        TRY_METHOD(mid_canEntityBeSeen, cls_EntityLivingBase, "canEntityBeSeen", "(Lnet/minecraft/entity/Entity;)Z");
        TRY_METHOD(mid_isSneaking, cls_EntityLivingBase, "isSneaking", "()Z");
    }

    // =========================================================================
    // EntityPlayer class IDs
    // =========================================================================

    if (cls_EntityPlayer) {
        if (is_1_7_10) {
            TRY_METHOD(mid_getName, cls_EntityPlayer, "getCommandSenderName", "()Ljava/lang/String;");
        } else {
            TRY_METHOD(mid_getName, cls_EntityPlayer, "getName", "()Ljava/lang/String;");
        }

        // getDisplayName has different return types per version
        if (is_1_7_10) {
            TRY_METHOD(mid_getDisplayName, cls_EntityPlayer, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;");
        } else {
            TRY_METHOD(mid_getDisplayName, cls_EntityPlayer, "getDisplayName", "()Lnet/minecraft/util/IChatComponent;");
            if (!mid_getDisplayName) {
                TRY_METHOD(mid_getDisplayName, cls_EntityPlayer, "getDisplayNameString", "()Ljava/lang/String;");
            }
        }

        // getGameProfile for real Mojang username (can't be modified by server plugins)
        TRY_METHOD(mid_getGameProfile, cls_EntityPlayer, "getGameProfile", "()Lcom/mojang/authlib/GameProfile;");
        if (!mid_getGameProfile) {
            // 1.7.10 obfuscated name
            TRY_METHOD(mid_getGameProfile, cls_EntityPlayer, "func_146103_bH", "()Lcom/mojang/authlib/GameProfile;");
        }

        TRY_FIELD(fid_inventory, cls_EntityPlayer, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
        TRY_FIELD(fid_openContainer, cls_EntityPlayer, "openContainer", "Lnet/minecraft/inventory/Container;");
    }

    // =========================================================================
    // GameProfile class IDs (com.mojang.authlib.GameProfile)
    // =========================================================================

    // Load GameProfile class from Mojang's authlib
    // Use CACHE_CLASS which uses the game's classloader (FindClass doesn't find authlib classes)
    CACHE_CLASS(cls_GameProfile, "com.mojang.authlib.GameProfile");
    if (cls_GameProfile) {
        log_debug("[JniCache] GameProfile class: OK");
        TRY_METHOD(mid_getProfileName, cls_GameProfile, "getName", "()Ljava/lang/String;");
        if (!mid_getProfileName) {
            // Obfuscated in older versions
            TRY_METHOD(mid_getProfileName, cls_GameProfile, "func_111285_a", "()Ljava/lang/String;");
        }
        log_debug("[JniCache] GameProfile.getName() method: %s", mid_getProfileName ? "OK" : "FAILED");
    } else {
        log_debug("[JniCache] GameProfile class: FAILED");
    }
    env->ExceptionClear();

    // =========================================================================
    // GameSettings class IDs
    // =========================================================================

    if (cls_GameSettings) {
        TRY_FIELD(fid_mouseSensitivity, cls_GameSettings, "mouseSensitivity", "F");
        TRY_FIELD(fid_keyBindForward, cls_GameSettings, "keyBindForward", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindBack, cls_GameSettings, "keyBindBack", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindLeft, cls_GameSettings, "keyBindLeft", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindRight, cls_GameSettings, "keyBindRight", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindJump, cls_GameSettings, "keyBindJump", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindSneak, cls_GameSettings, "keyBindSneak", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindSprint, cls_GameSettings, "keyBindSprint", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindAttack, cls_GameSettings, "keyBindAttack", "Lnet/minecraft/client/settings/KeyBinding;");
        TRY_FIELD(fid_keyBindUseItem, cls_GameSettings, "keyBindUseItem", "Lnet/minecraft/client/settings/KeyBinding;");
    }

    // =========================================================================
    // KeyBinding class IDs
    // =========================================================================

    if (cls_KeyBinding) {
        TRY_STATIC_METHOD(mid_setKeyBindState, cls_KeyBinding, "setKeyBindState", "(IZ)V");
        TRY_STATIC_METHOD(mid_unPressAllKeys, cls_KeyBinding, "unPressAllKeys", "()V");
        if (!mid_unPressAllKeys) {
            TRY_STATIC_METHOD(mid_unPressAllKeys, cls_KeyBinding, "func_74505_d", "()V");
        }
        TRY_METHOD(mid_getKeyCode, cls_KeyBinding, "getKeyCode", "()I");
        TRY_FIELD(fid_pressed, cls_KeyBinding, "pressed", "Z");
    }

    // =========================================================================
    // MouseHelper class IDs
    // =========================================================================

    if (cls_MouseHelper) {
        TRY_METHOD(mid_ungrabMouseCursor, cls_MouseHelper, "ungrabMouseCursor", "()V");
        TRY_METHOD(mid_grabMouseCursor, cls_MouseHelper, "grabMouseCursor", "()V");
    }

    // =========================================================================
    // RenderManager class IDs (version-specific: static vs instance)
    // =========================================================================

    if (cls_RenderManager) {
        if (is_1_7_10) {
            TRY_STATIC_FIELD(fid_renderPosX_static, cls_RenderManager, "renderPosX", "D");
            TRY_STATIC_FIELD(fid_renderPosY_static, cls_RenderManager, "renderPosY", "D");
            TRY_STATIC_FIELD(fid_renderPosZ_static, cls_RenderManager, "renderPosZ", "D");
        } else {
            TRY_FIELD(fid_renderPosX, cls_RenderManager, "renderPosX", "D");
            TRY_FIELD(fid_renderPosY, cls_RenderManager, "renderPosY", "D");
            TRY_FIELD(fid_renderPosZ, cls_RenderManager, "renderPosZ", "D");
        }
    }

    // =========================================================================
    // Timer class IDs
    // =========================================================================

    if (cls_Timer) {
        TRY_FIELD(fid_renderPartialTicks, cls_Timer, "renderPartialTicks", "F");
        if (!fid_renderPartialTicks) {
            // SRG/obfuscated fallback
            TRY_FIELD(fid_renderPartialTicks, cls_Timer, "field_74281_c", "F");
        }

        // timerSpeed controls game tick speed (default 1.0)
        TRY_FIELD(fid_timerSpeed, cls_Timer, "timerSpeed", "F");
        if (!fid_timerSpeed) {
            // SRG/obfuscated fallback
            TRY_FIELD(fid_timerSpeed, cls_Timer, "field_74278_d", "F");
        }
    }

    // =========================================================================
    // MovingObjectPosition class IDs
    // =========================================================================

    if (cls_MovingObjectPosition) {
        TRY_FIELD(fid_typeOfHit, cls_MovingObjectPosition, "typeOfHit",
                  "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
        TRY_FIELD(fid_entityHit, cls_MovingObjectPosition, "entityHit", "Lnet/minecraft/entity/Entity;");

        // 1.7.10 has direct blockX/Y/Z fields
        if (is_1_7_10) {
            TRY_FIELD(fid_blockX, cls_MovingObjectPosition, "blockX", "I");
            TRY_FIELD(fid_blockY, cls_MovingObjectPosition, "blockY", "I");
            TRY_FIELD(fid_blockZ, cls_MovingObjectPosition, "blockZ", "I");
        } else {
            TRY_METHOD(mid_getBlockPos, cls_MovingObjectPosition, "getBlockPos", "()Lnet/minecraft/util/BlockPos;");
            if (!mid_getBlockPos) {
                TRY_METHOD(mid_getBlockPos, cls_MovingObjectPosition, "func_178782_a",
                           "()Lnet/minecraft/util/BlockPos;");
            }
        }
    }

    // =========================================================================
    // MovingObjectType class IDs
    // =========================================================================

    CACHE_CLASS(cls_MovingObjectType, "net.minecraft.util.MovingObjectPosition$MovingObjectType");
    if (cls_MovingObjectType) {
        TRY_STATIC_FIELD(fid_BLOCK, cls_MovingObjectType, "BLOCK",
                         "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
        TRY_STATIC_FIELD(fid_ENTITY, cls_MovingObjectType, "ENTITY",
                         "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");

        if (!fid_BLOCK) {
            // Obfuscated fallback (field_76322_a = MISS, field_76323_b = BLOCK, field_76321_c = ENTITY)
            TRY_STATIC_FIELD(fid_BLOCK, cls_MovingObjectType, "field_76323_b",
                             "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
            TRY_STATIC_FIELD(fid_ENTITY, cls_MovingObjectType, "field_76321_c",
                             "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
        }
    }

    // MovingObjectType enum (nested class of MovingObjectPosition)
    CACHE_CLASS(cls_MovingObjectType, "net.minecraft.util.MovingObjectPosition$MovingObjectType");
    if (cls_MovingObjectType) {
        TRY_STATIC_FIELD(fid_ENTITY, cls_MovingObjectType, "ENTITY",
                         "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
        TRY_STATIC_FIELD(fid_BLOCK, cls_MovingObjectType, "BLOCK",
                         "Lnet/minecraft/util/MovingObjectPosition$MovingObjectType;");
    }

    // =========================================================================
    // World class IDs
    // =========================================================================

    if (cls_WorldClient) {
        TRY_FIELD(fid_playerEntities, cls_WorldClient, "playerEntities", "Ljava/util/List;");

        // loadedEntityList contains ALL entities (mobs, animals, players)
        TRY_FIELD(fid_loadedEntityList, cls_WorldClient, "loadedEntityList", "Ljava/util/List;");
        if (!fid_loadedEntityList) {
            // Try parent class World
            jclass cls_World = nullptr;
            if (Java::AssignClass("net.minecraft.world.World", cls_World) && cls_World) {
                TRY_FIELD(fid_loadedEntityList, cls_World, "loadedEntityList", "Ljava/util/List;");
                env->DeleteLocalRef(cls_World);
            }
        }

        if (!is_1_7_10) {
            TRY_METHOD(mid_getBlockState, cls_WorldClient, "getBlockState",
                       "(Lnet/minecraft/util/BlockPos;)Lnet/minecraft/block/state/IBlockState;");
        } else {
            // 1.7.10: World.getBlock(int x, int y, int z) -> Block
            TRY_METHOD(mid_getBlockWorld, cls_WorldClient, "getBlock", "(III)Lnet/minecraft/block/Block;");
            if (!mid_getBlockWorld) {
                TRY_METHOD(mid_getBlockWorld, cls_WorldClient, "func_147439_a", "(III)Lnet/minecraft/block/Block;");
            }
        }

        // rayTraceBlocks has multiple signatures
        TRY_METHOD(mid_rayTraceBlocks, cls_WorldClient, "rayTraceBlocks",
                   "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;Z)Lnet/minecraft/util/MovingObjectPosition;");
        if (!mid_rayTraceBlocks) {
            TRY_METHOD(mid_rayTraceBlocks, cls_WorldClient, "rayTraceBlocks",
                       "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;");
        }
        if (!mid_rayTraceBlocks) {
            TRY_METHOD(mid_rayTraceBlocks, cls_WorldClient, "func_72933_a",
                       "(Lnet/minecraft/util/Vec3;Lnet/minecraft/util/Vec3;)Lnet/minecraft/util/MovingObjectPosition;");
        }
    }

    // =========================================================================
    // BlockPos class IDs (1.8.x only)
    // =========================================================================

    if (cls_BlockPos) {
        TRY_METHOD(mid_blockPosInit, cls_BlockPos, "<init>", "(III)V");
        TRY_METHOD(mid_getX, cls_BlockPos, "getX", "()I");
        TRY_METHOD(mid_getY, cls_BlockPos, "getY", "()I");
        TRY_METHOD(mid_getZ, cls_BlockPos, "getZ", "()I");

        // Try SRG names if deobfuscated names fail
        if (!mid_getX)
            TRY_METHOD(mid_getX, cls_BlockPos, "func_177958_n", "()I");
        if (!mid_getY)
            TRY_METHOD(mid_getY, cls_BlockPos, "func_177956_o", "()I");
        if (!mid_getZ)
            TRY_METHOD(mid_getZ, cls_BlockPos, "func_177952_p", "()I");
    }

    // =========================================================================
    // IBlockState class IDs
    // =========================================================================

    if (cls_IBlockState) {
        TRY_METHOD(mid_getBlock, cls_IBlockState, "getBlock", "()Lnet/minecraft/block/Block;");
    }

    // =========================================================================
    // Block class IDs
    // =========================================================================

    if (cls_Block) {
        TRY_STATIC_METHOD(mid_getIdFromBlock, cls_Block, "getIdFromBlock", "(Lnet/minecraft/block/Block;)I");
    }

    // =========================================================================
    // Vec3 class IDs
    // =========================================================================

    if (cls_Vec3) {
        TRY_METHOD(mid_vec3Init, cls_Vec3, "<init>", "(DDD)V");
    }

    // =========================================================================
    // Container class IDs
    // =========================================================================

    if (cls_Container) {
        TRY_FIELD(fid_inventorySlots, cls_Container, "inventorySlots", "Ljava/util/List;");
        TRY_FIELD(fid_windowId, cls_Container, "windowId", "I");
    }

    // =========================================================================
    // Slot class IDs
    // =========================================================================

    if (cls_Slot) {
        TRY_METHOD(mid_getStack, cls_Slot, "getStack", "()Lnet/minecraft/item/ItemStack;");
    }

    // =========================================================================
    // ItemStack class IDs
    // =========================================================================

    if (cls_ItemStack) {
        TRY_METHOD(mid_getItem, cls_ItemStack, "getItem", "()Lnet/minecraft/item/Item;");
        TRY_FIELD(fid_stackSize, cls_ItemStack, "stackSize", "I");
        TRY_METHOD(mid_getItemDamage, cls_ItemStack, "getItemDamage", "()I");
        if (!mid_getItemDamage) {
            TRY_METHOD(mid_getItemDamage, cls_ItemStack, "getMetadata", "()I");
        }

        TRY_FIELD(fid_itemDamage, cls_ItemStack, "itemDamage", "I");
        if (!fid_itemDamage) {
            TRY_FIELD(fid_itemDamage, cls_ItemStack, "field_77991_e", "I");
        }
    }

    // =========================================================================
    // List class IDs
    // =========================================================================

    if (cls_List) {
        TRY_METHOD(mid_listSize, cls_List, "size", "()I");
        TRY_METHOD(mid_listGet, cls_List, "get", "(I)Ljava/lang/Object;");
    }

    // =========================================================================
    // IChatComponent class IDs
    // =========================================================================

    if (cls_IChatComponent) {
        TRY_METHOD(mid_getFormattedText, cls_IChatComponent, "getFormattedText", "()Ljava/lang/String;");
        TRY_METHOD(mid_getUnformattedText, cls_IChatComponent, "getUnformattedTextForChat", "()Ljava/lang/String;");
        if (!mid_getUnformattedText) {
            TRY_METHOD(mid_getUnformattedText, cls_IChatComponent, "getUnformattedText", "()Ljava/lang/String;");
        }
    }

    // =========================================================================
    // LWJGL Mouse class IDs
    // =========================================================================

    if (cls_Mouse) {
        TRY_STATIC_METHOD(mid_setGrabbed, cls_Mouse, "setGrabbed", "(Z)V");
    }

    g_initialized = true;
    log_ok("JniCache initialized");
    return true;
}

// =============================================================================
// Cleanup
// =============================================================================

void Cleanup() {
    if (!g_initialized)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

// Delete all GlobalRefs
#define DELETE_GLOBAL(var)         \
    if (var) {                     \
        env->DeleteGlobalRef(var); \
        var = nullptr;             \
    }

    DELETE_GLOBAL(cls_Minecraft);
    DELETE_GLOBAL(cls_WorldClient);
    DELETE_GLOBAL(cls_EntityPlayerSP);
    DELETE_GLOBAL(cls_EntityClientPlayerMP);
    DELETE_GLOBAL(cls_Entity);
    DELETE_GLOBAL(cls_EntityLivingBase);
    DELETE_GLOBAL(cls_EntityPlayer);
    DELETE_GLOBAL(cls_EntityArmorStand);
    DELETE_GLOBAL(cls_GameSettings);
    DELETE_GLOBAL(cls_KeyBinding);
    DELETE_GLOBAL(cls_MouseHelper);
    DELETE_GLOBAL(cls_RenderManager);
    DELETE_GLOBAL(cls_MovingObjectPosition);
    DELETE_GLOBAL(cls_MovingObjectType);
    DELETE_GLOBAL(cls_BlockPos);
    DELETE_GLOBAL(cls_Vec3);
    DELETE_GLOBAL(cls_Block);
    DELETE_GLOBAL(cls_IBlockState);
    DELETE_GLOBAL(cls_Timer);
    DELETE_GLOBAL(cls_AxisAlignedBB);
    DELETE_GLOBAL(cls_GuiScreen);
    DELETE_GLOBAL(cls_GuiInventory);
    DELETE_GLOBAL(cls_Container);
    DELETE_GLOBAL(cls_Slot);
    DELETE_GLOBAL(cls_ItemStack);
    DELETE_GLOBAL(cls_IChatComponent);
    DELETE_GLOBAL(cls_List);
    DELETE_GLOBAL(cls_Mouse);

#undef DELETE_GLOBAL

    g_initialized = false;
    log_debug("JniCache cleaned up");
}

bool IsInitialized() {
    return g_initialized;
}

}  // namespace JniCache
