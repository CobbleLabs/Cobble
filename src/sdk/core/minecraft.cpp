// Minecraft SDK - Core module
// Contains SDK_Init and basic Minecraft instance methods

#include "minecraft.h"
#include "../../util/inventory/inventory_hooks.h"
#include "../../util/logger.hpp"
#include "../jni/jni_cache.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

Minecraft mc;

void SDK_Init() {
    if (!Java::Initialized) {
        log_err("SDK_Init: java not initialized");
        return;
    }

    // Get Minecraft class
    if (!Java::AssignClass("net.minecraft.client.Minecraft", mc.cls)) {
        log_err("SDK_Init: failed to find Minecraft class");
        return;
    }

    // getMinecraft() -> Minecraft instance
    mc.mid_getMinecraft = Java::Env()->GetStaticMethodID(mc.cls, "getMinecraft", "()Lnet/minecraft/client/Minecraft;");
    if (!mc.mid_getMinecraft) {
        log_err("SDK_Init: failed to find getMinecraft");
        return;
    }

    mc.instance = Java::Env()->CallStaticObjectMethod(mc.cls, mc.mid_getMinecraft);
    if (!mc.instance) {
        log_err("SDK_Init: getMinecraft returned null");
        return;
    }

    // thePlayer field - signature depends on version
    const char* playerSig = (Java::Version == MinecraftVersion::MC_1_7_10)
                                ? "Lnet/minecraft/client/entity/EntityClientPlayerMP;"
                                : "Lnet/minecraft/client/entity/EntityPlayerSP;";
    mc.fid_thePlayer = Java::Env()->GetFieldID(mc.cls, "thePlayer", playerSig);

    // theWorld field
    mc.fid_theWorld = Java::Env()->GetFieldID(mc.cls, "theWorld", "Lnet/minecraft/client/multiplayer/WorldClient;");

    // currentScreen field (to detect if GUI is open)
    mc.fid_currentScreen = Java::Env()->GetFieldID(mc.cls, "currentScreen", "Lnet/minecraft/client/gui/GuiScreen;");

    // clickMouse method
    mc.mid_clickMouse = Java::Env()->GetMethodID(mc.cls, "clickMouse", "()V");

    // Click delay fields
    mc.fid_rightClickDelayTimer = Java::Env()->GetFieldID(mc.cls, "rightClickDelayTimer", "I");
    mc.fid_leftClickCounter = Java::Env()->GetFieldID(mc.cls, "leftClickCounter", "I");

    // KeyBinding cache for releaseMovementKeys (legacy, kept for compatibility)
    jclass kbCls = nullptr;
    if (Java::AssignClass("net.minecraft.client.settings.KeyBinding", kbCls)) {
        mc.cls_keyBinding = (jclass)Java::Env()->NewGlobalRef(kbCls);

        mc.mid_unPressAllKeys = Java::Env()->GetStaticMethodID(mc.cls_keyBinding, "unPressAllKeys", "()V");
        if (!mc.mid_unPressAllKeys) {
            Java::Env()->ExceptionClear();
            mc.mid_unPressAllKeys = Java::Env()->GetStaticMethodID(mc.cls_keyBinding, "func_74505_d", "()V");
        }

        mc.mid_setKeyBindState = Java::Env()->GetStaticMethodID(mc.cls_keyBinding, "setKeyBindState", "(IZ)V");
        mc.mid_getKeyCode = Java::Env()->GetMethodID(mc.cls_keyBinding, "getKeyCode", "()I");
        mc.fid_pressed = Java::Env()->GetFieldID(mc.cls_keyBinding, "pressed", "Z");
    }

    // Initialize JNI cache (new centralized system)
    if (!JniCache::Init()) {
        log_warn("JniCache::Init failed - some features may not work");
    }

    // Initialize player (will lazy-load fields on first refresh)
    player.refresh();

    // Initialize inventory hooks for potion detection
    if (!InventoryHooks::install()) {
        log_warn("[SDK] Inventory hooks installation failed - potion detection disabled");
    }

    log_ok("SDK initialized");
}

// =============================================================================
// Core Minecraft instance methods
// =============================================================================

jobject Minecraft::getPlayer() {
    if (!isValid() || !fid_thePlayer)
        return nullptr;
    return Java::Env()->GetObjectField(instance, fid_thePlayer);
}

jobject Minecraft::getWorld() {
    if (!isValid() || !fid_theWorld)
        return nullptr;
    return Java::Env()->GetObjectField(instance, fid_theWorld);
}

bool Minecraft::isInGui() {
    if (!isValid() || !fid_currentScreen)
        return false;
    jobject screen = Java::Env()->GetObjectField(instance, fid_currentScreen);
    bool result = (screen != nullptr);
    if (screen)
        Java::Env()->DeleteLocalRef(screen);
    return result;
}

// =============================================================================
// getItemEntityList - Get all item entities
// =============================================================================

std::vector<ItemEntityInfo> Minecraft::getItemEntityList() {
    std::vector<ItemEntityInfo> result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    jobject world = getWorld();
    if (!world)
        return result;

    // Use loadedEntityList (contains all entities)
    if (!JniCache::fid_loadedEntityList) {
        env->DeleteLocalRef(world);
        return result;
    }

    jobject entityList = env->GetObjectField(world, JniCache::fid_loadedEntityList);
    env->DeleteLocalRef(world);

    if (!entityList)
        return result;

    int size = env->CallIntMethod(entityList, JniCache::mid_listSize);
    if (size <= 0) {
        env->DeleteLocalRef(entityList);
        return result;
    }

    result.reserve(size / 4); // Heuristic

    for (int i = 0; i < size; i++) {
        jobject entity = env->CallObjectMethod(entityList, JniCache::mid_listGet, i);
        if (!entity)
            continue;

        // Check if EntityItem
        if (JniCache::cls_EntityItem && env->IsInstanceOf(entity, JniCache::cls_EntityItem)) {
             jobject itemStack = env->CallObjectMethod(entity, JniCache::mid_getEntityItem);
             if (itemStack) {
                 ItemEntityInfo info;
                 info.instance = entity; // Caller must delete

                 // Position
                 if (JniCache::fid_posX) info.posX = env->GetDoubleField(entity, JniCache::fid_posX);
                 if (JniCache::fid_posY) info.posY = env->GetDoubleField(entity, JniCache::fid_posY);
                 if (JniCache::fid_posZ) info.posZ = env->GetDoubleField(entity, JniCache::fid_posZ);

                 // Stack Size
                 if (JniCache::fid_stackSize) {
                     info.stackSize = env->GetIntField(itemStack, JniCache::fid_stackSize);
                 }

                 // Item ID and Max Stack
                 if (JniCache::mid_getItem) {
                     jobject item = env->CallObjectMethod(itemStack, JniCache::mid_getItem);
                     if (item) {
                         if (JniCache::mid_getIdFromItem) {
                             info.itemId = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem, item);
                         }
                         if (JniCache::mid_getItemStackLimit) {
                             info.maxStackSize = env->CallIntMethod(item, JniCache::mid_getItemStackLimit);
                         }
                         env->DeleteLocalRef(item);
                     }
                 }
                 
                 // Damage/Metadata
                 if (JniCache::mid_getItemDamage) {
                     info.damage = env->CallIntMethod(itemStack, JniCache::mid_getItemDamage);
                 }
                 
                 env->DeleteLocalRef(itemStack);
                 result.push_back(info);
                 continue; // entity ownership passed to info
             }
        }
        
        // If we reached here, we didn't use the entity
        env->DeleteLocalRef(entity);
    }

    env->DeleteLocalRef(entityList);
    return result;
}

void Minecraft::clickMouse() {
    if (!isValid() || !mid_clickMouse)
        return;
    Java::Env()->CallVoidMethod(instance, mid_clickMouse);
}

int Minecraft::getRightClickDelay() {
    if (!isValid() || !fid_rightClickDelayTimer)
        return 0;
    return Java::Env()->GetIntField(instance, fid_rightClickDelayTimer);
}

void Minecraft::setRightClickDelay(int value) {
    if (!isValid() || !fid_rightClickDelayTimer)
        return;
    Java::Env()->SetIntField(instance, fid_rightClickDelayTimer, value);
}

void Minecraft::setLeftClickCounter(int value) {
    if (!isValid() || !fid_leftClickCounter || !Java::Env())
        return;
    Java::Env()->SetIntField(instance, fid_leftClickCounter, value);
    if (Java::Env()->ExceptionCheck()) {
        Java::Env()->ExceptionClear();
    }
}

void Minecraft::setTimerSpeed(float speed) {
    if (!isValid())
        return;
    if (!JniCache::fid_timer || !JniCache::fid_timerSpeed)
        return;

    jobject timer = Java::Env()->GetObjectField(instance, JniCache::fid_timer);
    if (!timer)
        return;

    Java::Env()->SetFloatField(timer, JniCache::fid_timerSpeed, speed);
    Java::Env()->DeleteLocalRef(timer);
}

void Minecraft::sendPacket(jobject packet) {
    if (!isValid() || !packet)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    // Get NetHandlerPlayClient connection
    jobject netHandler = env->CallObjectMethod(instance, JniCache::mid_getNetHandler);
    if (!netHandler)
        return;

    // Call addToSendQueue(packet)
    env->CallVoidMethod(netHandler, JniCache::mid_addToSendQueue, packet);

    env->DeleteLocalRef(netHandler);
}

void Minecraft::sendInventoryOpenPacket() {
    JNIEnv* env = Java::Env();
    if (!env || !JniCache::cls_C16PacketClientStatus || !JniCache::fid_C16EnumState_OPEN_INVENTORY_ACHIEVEMENT) {
        log_debug("Cannot send open packet: Missing classes/IDs");
        return;
    }

    // Get EnumState.OPEN_INVENTORY_ACHIEVEMENT
    jobject enumState =
        env->GetStaticObjectField(JniCache::cls_C16EnumState, JniCache::fid_C16EnumState_OPEN_INVENTORY_ACHIEVEMENT);
    if (!enumState)
        return;

    // New C16PacketClientStatus(enumState)
    jobject packet =
        env->NewObject(JniCache::cls_C16PacketClientStatus, JniCache::mid_C16PacketClientStatus_init, enumState);
    if (packet) {
        sendPacket(packet);
        env->DeleteLocalRef(packet);
    }

    env->DeleteLocalRef(enumState);
}

void Minecraft::sendInventoryClosePacket() {
    JNIEnv* env = Java::Env();
    if (!env || !JniCache::cls_C0DPacketCloseWindow) {
        log_debug("Cannot send close packet: Missing classes");
        return;
    }

    // We can use windowId 0 (player inventory) or try to get actual open container id?
    // Usually for client inventory loop, sending close for window 0 is correct if we pretended to open it.
    // However, C0D constructor takes int windowId.

    jobject packet = env->NewObject(JniCache::cls_C0DPacketCloseWindow, JniCache::mid_C0DPacketCloseWindow_init, 0);
    if (packet) {
        sendPacket(packet);
        env->DeleteLocalRef(packet);
    }
}
