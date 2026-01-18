#include "player.h"
#include "../../util/logger.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"

LocalPlayer player;

void LocalPlayer::initFields() {
    if (fieldsInitialized || !instance)
        return;

    // Get Entity class (base class with position, motion, rotation)
    // Must use AssignClass because Minecraft uses custom ClassLoader
    if (!Java::AssignClass("net.minecraft.entity.Entity", entityCls)) {
        log_err("LocalPlayer: failed to find Entity class");
        return;
    }

    // Get EntityLivingBase class (has health, hurtTime, jump)
    Java::AssignClass("net.minecraft.entity.EntityLivingBase", livingCls);

    // Position fields (in Entity)
    fid_posX = Java::Env->GetFieldID(entityCls, "posX", "D");
    fid_posY = Java::Env->GetFieldID(entityCls, "posY", "D");
    fid_posZ = Java::Env->GetFieldID(entityCls, "posZ", "D");

    // Last tick position fields (for interpolation)
    fid_lastPosX = Java::Env->GetFieldID(entityCls, "lastTickPosX", "D");
    fid_lastPosY = Java::Env->GetFieldID(entityCls, "lastTickPosY", "D");
    fid_lastPosZ = Java::Env->GetFieldID(entityCls, "lastTickPosZ", "D");

    // Motion fields (in Entity)
    fid_motionX = Java::Env->GetFieldID(entityCls, "motionX", "D");
    fid_motionY = Java::Env->GetFieldID(entityCls, "motionY", "D");
    fid_motionZ = Java::Env->GetFieldID(entityCls, "motionZ", "D");

    // Rotation fields (in Entity)
    fid_rotationYaw = Java::Env->GetFieldID(entityCls, "rotationYaw", "F");
    fid_rotationPitch = Java::Env->GetFieldID(entityCls, "rotationPitch", "F");
    fid_prevRotationYaw = Java::Env->GetFieldID(entityCls, "prevRotationYaw", "F");
    fid_prevRotationPitch = Java::Env->GetFieldID(entityCls, "prevRotationPitch", "F");

    // On ground (in Entity)
    fid_onGround = Java::Env->GetFieldID(entityCls, "onGround", "Z");

    // Sprint/sneak methods (in Entity)
    mid_isSprinting = Java::Env->GetMethodID(entityCls, "isSprinting", "()Z");
    mid_setSprinting = Java::Env->GetMethodID(entityCls, "setSprinting", "(Z)V");
    mid_isSneaking = Java::Env->GetMethodID(entityCls, "isSneaking", "()Z");

    if (livingCls) {
        // Health (in EntityLivingBase)
        mid_getHealth = Java::Env->GetMethodID(livingCls, "getHealth", "()F");

        // HurtTime (in EntityLivingBase)
        fid_hurtTime = Java::Env->GetFieldID(livingCls, "hurtTime", "I");

        // Jump (in EntityLivingBase)
        mid_jump = Java::Env->GetMethodID(livingCls, "jump", "()V");

        // JumpTicks (in EntityLivingBase) - controls jump delay
        fid_jumpTicks = Java::Env->GetFieldID(livingCls, "jumpTicks", "I");
    }

    // EntityPlayerSP specific fields - for Blink (lastReportedPos)
    // Need to get EntityPlayerSP class for these
    jclass playerSPCls = nullptr;
    if (Java::AssignClass("net.minecraft.client.entity.EntityPlayerSP", playerSPCls)) {
        fid_lastReportedPosX = Java::Env->GetFieldID(playerSPCls, "lastReportedPosX", "D");
        fid_lastReportedPosY = Java::Env->GetFieldID(playerSPCls, "lastReportedPosY", "D");
        fid_lastReportedPosZ = Java::Env->GetFieldID(playerSPCls, "lastReportedPosZ", "D");
        fid_lastReportedYaw = Java::Env->GetFieldID(playerSPCls, "lastReportedYaw", "F");
        fid_lastReportedPitch = Java::Env->GetFieldID(playerSPCls, "lastReportedPitch", "F");

        if (fid_lastReportedPosX && fid_lastReportedPosY && fid_lastReportedPosZ) {
            log_ok("LocalPlayer: lastReportedPos fields initialized (Blink ready)");
        } else {
            // Clear exceptions if any
            if (Java::Env->ExceptionCheck())
                Java::Env->ExceptionClear();
            log_warn("LocalPlayer: lastReportedPos fields not available");
        }

        Java::Env->DeleteLocalRef(playerSPCls);
    }

    fieldsInitialized = true;
}

void LocalPlayer::refresh() {
    // Safety check - if no valid JNI environment, don't try to refresh
    if (!Java::Env || !Java::Initialized) {
        if (instance) {
            instance = nullptr;  // Invalidate cached instance
        }
        return;
    }

    // Get fresh player instance from Minecraft
    jobject newInstance = mc.getPlayer();

    // Delete old local ref if exists
    if (instance) {
        Java::Env->DeleteLocalRef(instance);
        instance = nullptr;
    }

    // Check if we got a valid new instance
    if (newInstance) {
        // Verify the object is still valid (not garbage collected)
        if (Java::Env->ExceptionCheck()) {
            Java::Env->ExceptionClear();
            newInstance = nullptr;
        }
    }

    instance = newInstance;

    if (instance && !fieldsInitialized) {
        initFields();
    }
}

// Position getters
double LocalPlayer::getPosX() {
    if (!isValid() || !fid_posX)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_posX);
}

double LocalPlayer::getPosY() {
    if (!isValid() || !fid_posY)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_posY);
}

double LocalPlayer::getPosZ() {
    if (!isValid() || !fid_posZ)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_posZ);
}

// Last tick position getters (for interpolation)
double LocalPlayer::getLastPosX() {
    if (!isValid() || !fid_lastPosX)
        return getPosX();
    return Java::Env->GetDoubleField(instance, fid_lastPosX);
}

double LocalPlayer::getLastPosY() {
    if (!isValid() || !fid_lastPosY)
        return getPosY();
    return Java::Env->GetDoubleField(instance, fid_lastPosY);
}

double LocalPlayer::getLastPosZ() {
    if (!isValid() || !fid_lastPosZ)
        return getPosZ();
    return Java::Env->GetDoubleField(instance, fid_lastPosZ);
}

// Motion getters/setters
double LocalPlayer::getMotionX() {
    if (!isValid() || !fid_motionX)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_motionX);
}

double LocalPlayer::getMotionY() {
    if (!isValid() || !fid_motionY)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_motionY);
}

double LocalPlayer::getMotionZ() {
    if (!isValid() || !fid_motionZ)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_motionZ);
}

void LocalPlayer::setMotionX(double value) {
    if (!isValid() || !fid_motionX)
        return;
    Java::Env->SetDoubleField(instance, fid_motionX, value);
}

void LocalPlayer::setMotionY(double value) {
    if (!isValid() || !fid_motionY)
        return;
    Java::Env->SetDoubleField(instance, fid_motionY, value);
}

void LocalPlayer::setMotionZ(double value) {
    if (!isValid() || !fid_motionZ)
        return;
    Java::Env->SetDoubleField(instance, fid_motionZ, value);
}

// Rotation getters/setters
float LocalPlayer::getYaw() {
    if (!isValid() || !fid_rotationYaw)
        return 0.0f;
    return Java::Env->GetFloatField(instance, fid_rotationYaw);
}

float LocalPlayer::getPitch() {
    if (!isValid() || !fid_rotationPitch)
        return 0.0f;
    return Java::Env->GetFloatField(instance, fid_rotationPitch);
}

float LocalPlayer::getPrevYaw() {
    if (!isValid() || !fid_prevRotationYaw)
        return getYaw();
    return Java::Env->GetFloatField(instance, fid_prevRotationYaw);
}

float LocalPlayer::getPrevPitch() {
    if (!isValid() || !fid_prevRotationPitch)
        return getPitch();
    return Java::Env->GetFloatField(instance, fid_prevRotationPitch);
}

void LocalPlayer::setYaw(float value) {
    if (!isValid() || !fid_rotationYaw)
        return;
    Java::Env->SetFloatField(instance, fid_rotationYaw, value);
}

void LocalPlayer::setPitch(float value) {
    if (!isValid() || !fid_rotationPitch)
        return;
    Java::Env->SetFloatField(instance, fid_rotationPitch, value);
}

void LocalPlayer::setPrevYaw(float value) {
    if (!isValid() || !fid_prevRotationYaw)
        return;
    Java::Env->SetFloatField(instance, fid_prevRotationYaw, value);
}

void LocalPlayer::setPrevPitch(float value) {
    if (!isValid() || !fid_prevRotationPitch)
        return;
    Java::Env->SetFloatField(instance, fid_prevRotationPitch, value);
}

// State
bool LocalPlayer::isOnGround() {
    if (!isValid() || !fid_onGround)
        return false;
    return Java::Env->GetBooleanField(instance, fid_onGround);
}

void LocalPlayer::setOnGround(bool value) {
    if (!isValid() || !fid_onGround)
        return;
    Java::Env->SetBooleanField(instance, fid_onGround, value);
}

bool LocalPlayer::isSprinting() {
    if (!isValid() || !mid_isSprinting)
        return false;
    return Java::Env->CallBooleanMethod(instance, mid_isSprinting);
}

void LocalPlayer::setSprinting(bool value) {
    if (!isValid() || !mid_setSprinting)
        return;
    Java::Env->CallVoidMethod(instance, mid_setSprinting, value);
}

bool LocalPlayer::isSneaking() {
    if (!isValid() || !mid_isSneaking)
        return false;
    return Java::Env->CallBooleanMethod(instance, mid_isSneaking);
}

// Combat
float LocalPlayer::getHealth() {
    if (!isValid() || !mid_getHealth)
        return 0.0f;
    return Java::Env->CallFloatMethod(instance, mid_getHealth);
}

int LocalPlayer::getHurtTime() {
    if (!isValid() || !fid_hurtTime)
        return 0;
    return Java::Env->GetIntField(instance, fid_hurtTime);
}

void LocalPlayer::jump() {
    if (!isValid() || !mid_jump)
        return;
    Java::Env->CallVoidMethod(instance, mid_jump);
}

void LocalPlayer::setJumpTicks(int value) {
    if (!isValid() || !fid_jumpTicks || !Java::Env)
        return;
    Java::Env->SetIntField(instance, fid_jumpTicks, value);
    if (Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
    }
}

int LocalPlayer::getHeldItemId() {
    if (!isValid())
        return -1;

    // Chain: player.inventory.getCurrentItem().getItem() -> Item.getIdFromItem(item)
    // Uses method calls instead of caching to keep implementation simple

    // Get EntityPlayer class for inventory field
    jclass playerCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls))
        return -1;

    // Get InventoryPlayer class
    jclass inventoryCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
        Java::Env->DeleteLocalRef(playerCls);
        return -1;
    }

    // Get ItemStack class
    jclass itemStackCls = nullptr;
    if (!Java::AssignClass("net.minecraft.item.ItemStack", itemStackCls)) {
        Java::Env->DeleteLocalRef(playerCls);
        Java::Env->DeleteLocalRef(inventoryCls);
        return -1;
    }

    // Get Item class
    jclass itemCls = nullptr;
    if (!Java::AssignClass("net.minecraft.item.Item", itemCls)) {
        Java::Env->DeleteLocalRef(playerCls);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        return -1;
    }

    int result = -1;

    // player.inventory field
    jfieldID fid_inventory =
        Java::Env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
    if (!fid_inventory)
        goto cleanup;

    {
        jobject inventory = Java::Env->GetObjectField(instance, fid_inventory);
        if (!inventory)
            goto cleanup;

        // inventory.getCurrentItem()
        jmethodID mid_getCurrentItem =
            Java::Env->GetMethodID(inventoryCls, "getCurrentItem", "()Lnet/minecraft/item/ItemStack;");
        if (!mid_getCurrentItem) {
            Java::Env->DeleteLocalRef(inventory);
            goto cleanup;
        }

        jobject itemStack = Java::Env->CallObjectMethod(inventory, mid_getCurrentItem);
        Java::Env->DeleteLocalRef(inventory);

        if (!itemStack)
            goto cleanup;  // Empty hand

        // itemStack.getItem()
        jmethodID mid_getItem = Java::Env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");
        if (!mid_getItem) {
            Java::Env->DeleteLocalRef(itemStack);
            goto cleanup;
        }

        jobject item = Java::Env->CallObjectMethod(itemStack, mid_getItem);
        Java::Env->DeleteLocalRef(itemStack);

        if (!item)
            goto cleanup;

        // Item.getIdFromItem(item) - static method
        jmethodID mid_getIdFromItem =
            Java::Env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
        if (!mid_getIdFromItem) {
            Java::Env->DeleteLocalRef(item);
            goto cleanup;
        }

        result = Java::Env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
        Java::Env->DeleteLocalRef(item);
    }

cleanup:
    Java::Env->DeleteLocalRef(playerCls);
    Java::Env->DeleteLocalRef(inventoryCls);
    Java::Env->DeleteLocalRef(itemStackCls);
    Java::Env->DeleteLocalRef(itemCls);

    return result;
}

int LocalPlayer::getHeldItemDamage() {
    if (!isValid())
        return -1;

    // Chain: player.inventory.getCurrentItem().getMetadata()

    // Get EntityPlayer class for inventory field
    jclass playerCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.EntityPlayer", playerCls))
        return -1;

    // Get InventoryPlayer class
    jclass inventoryCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
        Java::Env->DeleteLocalRef(playerCls);
        return -1;
    }

    // Get ItemStack class
    jclass itemStackCls = nullptr;
    if (!Java::AssignClass("net.minecraft.item.ItemStack", itemStackCls)) {
        Java::Env->DeleteLocalRef(playerCls);
        Java::Env->DeleteLocalRef(inventoryCls);
        return -1;
    }

    int result = -1;

    // player.inventory field
    jfieldID fid_inventory =
        Java::Env->GetFieldID(playerCls, "inventory", "Lnet/minecraft/entity/player/InventoryPlayer;");
    if (!fid_inventory)
        goto cleanup_damage;

    {
        jobject inventory = Java::Env->GetObjectField(instance, fid_inventory);
        if (!inventory)
            goto cleanup_damage;

        // inventory.getCurrentItem()
        jmethodID mid_getCurrentItem =
            Java::Env->GetMethodID(inventoryCls, "getCurrentItem", "()Lnet/minecraft/item/ItemStack;");
        if (!mid_getCurrentItem) {
            Java::Env->DeleteLocalRef(inventory);
            goto cleanup_damage;
        }

        jobject itemStack = Java::Env->CallObjectMethod(inventory, mid_getCurrentItem);
        Java::Env->DeleteLocalRef(inventory);

        if (!itemStack)
            goto cleanup_damage;  // Empty hand

        // itemStack.getMetadata() - returns the damage/metadata value
        jmethodID mid_getMetadata = Java::Env->GetMethodID(itemStackCls, "getMetadata", "()I");
        if (!mid_getMetadata) {
            Java::Env->DeleteLocalRef(itemStack);
            goto cleanup_damage;
        }

        result = Java::Env->CallIntMethod(itemStack, mid_getMetadata);
        Java::Env->DeleteLocalRef(itemStack);
    }

cleanup_damage:
    Java::Env->DeleteLocalRef(playerCls);
    Java::Env->DeleteLocalRef(inventoryCls);
    Java::Env->DeleteLocalRef(itemStackCls);

    return result;
}

int LocalPlayer::getItemInSlot(int slot) {
    if (!isValid())
        return -1;
    if (slot < 0 || slot > 35)
        return -1;  // Valid slots: 0-35 (hotbar + inventory)

    // Get InventoryPlayer class
    jclass inventoryCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
        if (Java::Env->ExceptionCheck())
            Java::Env->ExceptionClear();
        return -1;
    }

    // Get ItemStack class
    jclass itemStackCls = nullptr;
    if (!Java::AssignClass("net.minecraft.item.ItemStack", itemStackCls)) {
        Java::Env->DeleteLocalRef(inventoryCls);
        if (Java::Env->ExceptionCheck())
            Java::Env->ExceptionClear();
        return -1;
    }

    // Get Item class
    jclass itemCls = nullptr;
    if (!Java::AssignClass("net.minecraft.item.Item", itemCls)) {
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        if (Java::Env->ExceptionCheck())
            Java::Env->ExceptionClear();
        return -1;
    }

    // Get player.inventory field - use JniCache which is already initialized
    if (!JniCache::fid_inventory) {
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    jobject inventory = Java::Env->GetObjectField(instance, JniCache::fid_inventory);
    if (!inventory || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    // Get inventory.mainInventory field (array of ItemStack)
    jfieldID fid_mainInventory =
        Java::Env->GetFieldID(inventoryCls, "mainInventory", "[Lnet/minecraft/item/ItemStack;");
    if (!fid_mainInventory || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    jobjectArray mainInventory = (jobjectArray)Java::Env->GetObjectField(inventory, fid_mainInventory);
    if (!mainInventory || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    // Check array bounds
    jsize arrayLength = Java::Env->GetArrayLength(mainInventory);
    if (slot >= arrayLength) {
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    // Get ItemStack from array
    jobject itemStack = Java::Env->GetObjectArrayElement(mainInventory, slot);
    if (Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    if (!itemStack) {
        // Empty slot - not an error
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    // Get itemStack.getItem() method
    jmethodID mid_getItem = Java::Env->GetMethodID(itemStackCls, "getItem", "()Lnet/minecraft/item/Item;");
    if (!mid_getItem || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(itemStack);
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    jobject item = Java::Env->CallObjectMethod(itemStack, mid_getItem);
    if (Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(itemStack);
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    if (!item) {
        Java::Env->DeleteLocalRef(itemStack);
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    // Get Item.getIdFromItem static method
    jmethodID mid_getIdFromItem =
        Java::Env->GetStaticMethodID(itemCls, "getIdFromItem", "(Lnet/minecraft/item/Item;)I");
    if (!mid_getIdFromItem || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(item);
        Java::Env->DeleteLocalRef(itemStack);
        Java::Env->DeleteLocalRef(mainInventory);
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        Java::Env->DeleteLocalRef(itemStackCls);
        Java::Env->DeleteLocalRef(itemCls);
        return -1;
    }

    int itemId = Java::Env->CallStaticIntMethod(itemCls, mid_getIdFromItem, item);
    if (Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        itemId = -1;
    }

    // Cleanup
    Java::Env->DeleteLocalRef(item);
    Java::Env->DeleteLocalRef(itemStack);
    Java::Env->DeleteLocalRef(mainInventory);
    Java::Env->DeleteLocalRef(inventory);
    Java::Env->DeleteLocalRef(inventoryCls);
    Java::Env->DeleteLocalRef(itemStackCls);
    Java::Env->DeleteLocalRef(itemCls);

    return itemId;
}

// ============================================================================
// Hotbar Slot Control - For AutoTool module
// Gets/sets the currently selected hotbar slot (0-8)
// ============================================================================

int LocalPlayer::getCurrentSlot() {
    if (!isValid())
        return 0;

    // Get InventoryPlayer class
    jclass inventoryCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
        if (Java::Env->ExceptionCheck())
            Java::Env->ExceptionClear();
        return 0;
    }

    // Get player.inventory using JniCache (already initialized for both versions)
    if (!JniCache::fid_inventory) {
        Java::Env->DeleteLocalRef(inventoryCls);
        return 0;
    }

    jobject inventory = Java::Env->GetObjectField(instance, JniCache::fid_inventory);
    if (!inventory || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventoryCls);
        return 0;
    }

    // Get currentItem field from inventory
    jfieldID fid_currentItem = Java::Env->GetFieldID(inventoryCls, "currentItem", "I");
    if (!fid_currentItem || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        return 0;
    }

    int currentSlot = Java::Env->GetIntField(inventory, fid_currentItem);

    Java::Env->DeleteLocalRef(inventory);
    Java::Env->DeleteLocalRef(inventoryCls);

    return currentSlot;
}

void LocalPlayer::setCurrentSlot(int slot) {
    if (!isValid())
        return;
    if (slot < 0 || slot > 8)
        return;  // Valid hotbar slots: 0-8

    // Get InventoryPlayer class
    jclass inventoryCls = nullptr;
    if (!Java::AssignClass("net.minecraft.entity.player.InventoryPlayer", inventoryCls)) {
        if (Java::Env->ExceptionCheck())
            Java::Env->ExceptionClear();
        return;
    }

    // Get player.inventory using JniCache (already initialized for both versions)
    if (!JniCache::fid_inventory) {
        Java::Env->DeleteLocalRef(inventoryCls);
        return;
    }

    jobject inventory = Java::Env->GetObjectField(instance, JniCache::fid_inventory);
    if (!inventory || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventoryCls);
        return;
    }

    // Get currentItem field from inventory
    jfieldID fid_currentItem = Java::Env->GetFieldID(inventoryCls, "currentItem", "I");
    if (!fid_currentItem || Java::Env->ExceptionCheck()) {
        Java::Env->ExceptionClear();
        Java::Env->DeleteLocalRef(inventory);
        Java::Env->DeleteLocalRef(inventoryCls);
        return;
    }

    // Set the current slot
    Java::Env->SetIntField(inventory, fid_currentItem, slot);

    Java::Env->DeleteLocalRef(inventory);
    Java::Env->DeleteLocalRef(inventoryCls);
}

// ============================================================================
// Blink - lastReportedPos manipulation
// These fields control when Minecraft sends C03PacketPlayer
// By keeping lastReportedPos = currentPos, no movement packets are sent
// ============================================================================

double LocalPlayer::getLastReportedPosX() {
    if (!isValid() || !fid_lastReportedPosX)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_lastReportedPosX);
}

double LocalPlayer::getLastReportedPosY() {
    if (!isValid() || !fid_lastReportedPosY)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_lastReportedPosY);
}

double LocalPlayer::getLastReportedPosZ() {
    if (!isValid() || !fid_lastReportedPosZ)
        return 0.0;
    return Java::Env->GetDoubleField(instance, fid_lastReportedPosZ);
}

void LocalPlayer::setLastReportedPosX(double value) {
    if (!isValid() || !fid_lastReportedPosX)
        return;
    Java::Env->SetDoubleField(instance, fid_lastReportedPosX, value);
}

void LocalPlayer::setLastReportedPosY(double value) {
    if (!isValid() || !fid_lastReportedPosY)
        return;
    Java::Env->SetDoubleField(instance, fid_lastReportedPosY, value);
}

void LocalPlayer::setLastReportedPosZ(double value) {
    if (!isValid() || !fid_lastReportedPosZ)
        return;
    Java::Env->SetDoubleField(instance, fid_lastReportedPosZ, value);
}

float LocalPlayer::getLastReportedYaw() {
    if (!isValid() || !fid_lastReportedYaw)
        return 0.0f;
    return Java::Env->GetFloatField(instance, fid_lastReportedYaw);
}

float LocalPlayer::getLastReportedPitch() {
    if (!isValid() || !fid_lastReportedPitch)
        return 0.0f;
    return Java::Env->GetFloatField(instance, fid_lastReportedPitch);
}

void LocalPlayer::setLastReportedYaw(float value) {
    if (!isValid() || !fid_lastReportedYaw)
        return;
    Java::Env->SetFloatField(instance, fid_lastReportedYaw, value);
}

void LocalPlayer::setLastReportedPitch(float value) {
    if (!isValid() || !fid_lastReportedPitch)
        return;
    Java::Env->SetFloatField(instance, fid_lastReportedPitch, value);
}
