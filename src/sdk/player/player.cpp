#include "player.h"
#include "../../util/logger.hpp"
#include "../../util/aes_string.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"

LocalPlayer player;

void LocalPlayer::initFields() {
    if (fieldsInitialized || !instance)
        return;

    // Get Entity class (base class with position, motion, rotation)
    // Must use AssignClass because Minecraft uses custom ClassLoader
    if (!Java::AssignClass(OBFUSCATE_STRING("net.minecraft.entity.Entity"), entityCls)) {
        log_err("LocalPlayer: failed to find Entity class");
        return;
    }

    // Get EntityLivingBase class (has health, hurtTime, jump)
    Java::AssignClass(OBFUSCATE_STRING("net.minecraft.entity.EntityLivingBase"), livingCls);

    // Position fields (in Entity)
    fid_posX = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("posX").c_str(), OBFUSCATE_STRING("D").c_str());
    fid_posY = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("posY").c_str(), OBFUSCATE_STRING("D").c_str());
    fid_posZ = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("posZ").c_str(), OBFUSCATE_STRING("D").c_str());

    // Last tick position fields (for interpolation)
    fid_lastPosX = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("lastTickPosX").c_str(), OBFUSCATE_STRING("D").c_str());
    fid_lastPosY = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("lastTickPosY").c_str(), OBFUSCATE_STRING("D").c_str());
    fid_lastPosZ = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("lastTickPosZ").c_str(), OBFUSCATE_STRING("D").c_str());

    // Motion fields (in Entity)
    fid_motionX = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("motionX").c_str(), OBFUSCATE_STRING("D").c_str());
    fid_motionY = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("motionY").c_str(), OBFUSCATE_STRING("D").c_str());
    fid_motionZ = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("motionZ").c_str(), OBFUSCATE_STRING("D").c_str());

    // Rotation fields (in Entity)
    fid_rotationYaw = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("rotationYaw").c_str(), OBFUSCATE_STRING("F").c_str());
    fid_rotationPitch = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("rotationPitch").c_str(), OBFUSCATE_STRING("F").c_str());
    fid_prevRotationYaw = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("prevRotationYaw").c_str(), OBFUSCATE_STRING("F").c_str());
    fid_prevRotationPitch = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("prevRotationPitch").c_str(), OBFUSCATE_STRING("F").c_str());

    // On ground (in Entity)
    fid_onGround = Java::Env()->GetFieldID(entityCls, OBFUSCATE_STRING("onGround").c_str(), OBFUSCATE_STRING("Z").c_str());

    // Sprint/sneak methods (in Entity)
    mid_isSprinting = Java::Env()->GetMethodID(entityCls, OBFUSCATE_STRING("isSprinting").c_str(), OBFUSCATE_STRING("()Z").c_str());
    mid_setSprinting = Java::Env()->GetMethodID(entityCls, OBFUSCATE_STRING("setSprinting").c_str(), OBFUSCATE_STRING("(Z)V").c_str());
    mid_isSneaking = Java::Env()->GetMethodID(entityCls, OBFUSCATE_STRING("isSneaking").c_str(), OBFUSCATE_STRING("()Z").c_str());

    if (livingCls) {
        // Health (in EntityLivingBase)
        mid_getHealth = Java::Env()->GetMethodID(livingCls, OBFUSCATE_STRING("getHealth").c_str(), OBFUSCATE_STRING("()F").c_str());

        // HurtTime (in EntityLivingBase)
        fid_hurtTime = Java::Env()->GetFieldID(livingCls, OBFUSCATE_STRING("hurtTime").c_str(), OBFUSCATE_STRING("I").c_str());

        // Jump (in EntityLivingBase)
        mid_jump = Java::Env()->GetMethodID(livingCls, OBFUSCATE_STRING("jump").c_str(), OBFUSCATE_STRING("()V").c_str());

        // JumpTicks (in EntityLivingBase) - controls jump delay
        fid_jumpTicks = Java::Env()->GetFieldID(livingCls, OBFUSCATE_STRING("jumpTicks").c_str(), OBFUSCATE_STRING("I").c_str());

    }

    // Item-use state belongs to EntityPlayer, not EntityLivingBase. Looking it
    // up on the living class leaves a pending NoSuchMethodError and disables
    // every module that relies on this state.
    if (JniCache::cls_EntityPlayer) {
        mid_isUsingItem = Java::Env()->GetMethodID(
            JniCache::cls_EntityPlayer, OBFUSCATE_STRING("isUsingItem").c_str(),
            OBFUSCATE_STRING("()Z").c_str());
        if (!mid_isUsingItem) {
            Java::Env()->ExceptionClear();
            mid_isUsingItem = Java::Env()->GetMethodID(
                JniCache::cls_EntityPlayer, OBFUSCATE_STRING("func_71039_bw").c_str(),
                OBFUSCATE_STRING("()Z").c_str());
        }
        if (!mid_isUsingItem && Java::Env()->ExceptionCheck())
            Java::Env()->ExceptionClear();
    }

    // Need to get EntityPlayerSP class for these
    jclass playerSPCls = nullptr;
    if (Java::AssignClass(OBFUSCATE_STRING("net.minecraft.client.entity.EntityPlayerSP"), playerSPCls)) {
        fid_lastReportedPosX = Java::Env()->GetFieldID(playerSPCls, OBFUSCATE_STRING("lastReportedPosX").c_str(), OBFUSCATE_STRING("D").c_str());
        fid_lastReportedPosY = Java::Env()->GetFieldID(playerSPCls, OBFUSCATE_STRING("lastReportedPosY").c_str(), OBFUSCATE_STRING("D").c_str());
        fid_lastReportedPosZ = Java::Env()->GetFieldID(playerSPCls, OBFUSCATE_STRING("lastReportedPosZ").c_str(), OBFUSCATE_STRING("D").c_str());
        fid_lastReportedYaw = Java::Env()->GetFieldID(playerSPCls, OBFUSCATE_STRING("lastReportedYaw").c_str(), OBFUSCATE_STRING("F").c_str());
        fid_lastReportedPitch = Java::Env()->GetFieldID(playerSPCls, OBFUSCATE_STRING("lastReportedPitch").c_str(), OBFUSCATE_STRING("F").c_str());

        if (fid_lastReportedPosX && fid_lastReportedPosY && fid_lastReportedPosZ) {
            log_ok("LocalPlayer: lastReportedPos fields initialized (Blink ready)");
        } else {
            // Clear exceptions if any
            if (Java::Env()->ExceptionCheck())
                Java::Env()->ExceptionClear();
            log_warn("LocalPlayer: lastReportedPos fields not available");
        }

        Java::Env()->DeleteLocalRef(playerSPCls);
    }

    // Cache inventory-related fields/methods (prevents JNI leak in getHeldItemId)
    // These are resolved once here instead of every tick
    jclass playerCls = nullptr;
    jclass inventoryCls = nullptr;
    jclass itemStackCls = nullptr;
    jclass itemCls = nullptr;

    if (Java::AssignClass(OBFUSCATE_STRING("net.minecraft.entity.player.EntityPlayer"), playerCls) &&
        Java::AssignClass(OBFUSCATE_STRING("net.minecraft.entity.player.InventoryPlayer"), inventoryCls) &&
        Java::AssignClass(OBFUSCATE_STRING("net.minecraft.item.ItemStack"), itemStackCls) &&
        Java::AssignClass(OBFUSCATE_STRING("net.minecraft.item.Item"), itemCls)) {

        fid_playerInventory = Java::Env()->GetFieldID(playerCls, OBFUSCATE_STRING("inventory").c_str(), OBFUSCATE_STRING("Lnet/minecraft/entity/player/InventoryPlayer;").c_str());
        mid_getCurrentItem = Java::Env()->GetMethodID(inventoryCls, OBFUSCATE_STRING("getCurrentItem").c_str(), OBFUSCATE_STRING("()Lnet/minecraft/item/ItemStack;").c_str());
        mid_getItem = Java::Env()->GetMethodID(itemStackCls, OBFUSCATE_STRING("getItem").c_str(), OBFUSCATE_STRING("()Lnet/minecraft/item/Item;").c_str());
        mid_getIdFromItem = Java::Env()->GetStaticMethodID(itemCls, OBFUSCATE_STRING("getIdFromItem").c_str(), OBFUSCATE_STRING("(Lnet/minecraft/item/Item;)I").c_str());
        mid_getMetadata = Java::Env()->GetMethodID(itemStackCls, OBFUSCATE_STRING("getMetadata").c_str(), OBFUSCATE_STRING("()I").c_str());

        // Keep Item class as GlobalRef for static method calls
        cls_Item = (jclass)Java::Env()->NewGlobalRef(itemCls);

        if (fid_playerInventory && mid_getCurrentItem && mid_getItem && mid_getIdFromItem) {
            log_ok("LocalPlayer: inventory fields cached (NoHitDelay ready)");
        }

        Java::Env()->DeleteLocalRef(playerCls);
        Java::Env()->DeleteLocalRef(inventoryCls);
        Java::Env()->DeleteLocalRef(itemStackCls);
        Java::Env()->DeleteLocalRef(itemCls);
    }

    if (Java::Env()->ExceptionCheck())
        Java::Env()->ExceptionClear();

    fieldsInitialized = true;
}

void LocalPlayer::refresh() {
    // Safety check - if no valid JNI environment, don't try to refresh
    if (!Java::Env() || !Java::Initialized) {
        if (instance) {
            instance = nullptr;  // Invalidate cached instance
        }
        return;
    }

    // ALWAYS get a fresh reference - the old approach cached too aggressively
    // and caused stale position data when changing worlds/servers
    if (instance) {
        Java::Env()->DeleteGlobalRef(instance);
        instance = nullptr;
    }

    jobject localPlayer = mc.getPlayer();
    if (localPlayer) {
        instance = Java::Env()->NewGlobalRef(localPlayer);
        Java::Env()->DeleteLocalRef(localPlayer);
    }

    // Check if we got a valid instance
    if (instance) {
        // Verify the object is still valid (not garbage collected)
        if (Java::Env()->ExceptionCheck()) {
            Java::Env()->ExceptionClear();
            Java::Env()->DeleteGlobalRef(instance);
            instance = nullptr;
        } else if (!fieldsInitialized) {
            initFields();
        }
    }
}

// Position getters
double LocalPlayer::getPosX() {
    if (!isValid() || !fid_posX)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_posX);
}

double LocalPlayer::getPosY() {
    if (!isValid() || !fid_posY)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_posY);
}

double LocalPlayer::getPosZ() {
    if (!isValid() || !fid_posZ)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_posZ);
}

// Last tick position getters (for interpolation)
double LocalPlayer::getLastPosX() {
    if (!isValid() || !fid_lastPosX)
        return getPosX();
    return Java::Env()->GetDoubleField(instance, fid_lastPosX);
}

double LocalPlayer::getLastPosY() {
    if (!isValid() || !fid_lastPosY)
        return getPosY();
    return Java::Env()->GetDoubleField(instance, fid_lastPosY);
}

double LocalPlayer::getLastPosZ() {
    if (!isValid() || !fid_lastPosZ)
        return getPosZ();
    return Java::Env()->GetDoubleField(instance, fid_lastPosZ);
}

// Motion getters/setters
double LocalPlayer::getMotionX() {
    if (!isValid() || !fid_motionX)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_motionX);
}

double LocalPlayer::getMotionY() {
    if (!isValid() || !fid_motionY)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_motionY);
}

double LocalPlayer::getMotionZ() {
    if (!isValid() || !fid_motionZ)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_motionZ);
}

void LocalPlayer::setMotionX(double value) {
    if (!isValid() || !fid_motionX)
        return;
    Java::Env()->SetDoubleField(instance, fid_motionX, value);
}

void LocalPlayer::setMotionY(double value) {
    if (!isValid() || !fid_motionY)
        return;
    Java::Env()->SetDoubleField(instance, fid_motionY, value);
}

void LocalPlayer::setMotionZ(double value) {
    if (!isValid() || !fid_motionZ)
        return;
    Java::Env()->SetDoubleField(instance, fid_motionZ, value);
}

// Rotation getters/setters
float LocalPlayer::getYaw() {
    if (!isValid() || !fid_rotationYaw)
        return 0.0f;
    return Java::Env()->GetFloatField(instance, fid_rotationYaw);
}

float LocalPlayer::getPitch() {
    if (!isValid() || !fid_rotationPitch)
        return 0.0f;
    return Java::Env()->GetFloatField(instance, fid_rotationPitch);
}

float LocalPlayer::getPrevYaw() {
    if (!isValid() || !fid_prevRotationYaw)
        return getYaw();
    return Java::Env()->GetFloatField(instance, fid_prevRotationYaw);
}

float LocalPlayer::getPrevPitch() {
    if (!isValid() || !fid_prevRotationPitch)
        return getPitch();
    return Java::Env()->GetFloatField(instance, fid_prevRotationPitch);
}

void LocalPlayer::setYaw(float value) {
    if (!isValid() || !fid_rotationYaw)
        return;
    Java::Env()->SetFloatField(instance, fid_rotationYaw, value);
}

void LocalPlayer::setPitch(float value) {
    if (!isValid() || !fid_rotationPitch)
        return;
    Java::Env()->SetFloatField(instance, fid_rotationPitch, value);
}

void LocalPlayer::setPrevYaw(float value) {
    if (!isValid() || !fid_prevRotationYaw)
        return;
    Java::Env()->SetFloatField(instance, fid_prevRotationYaw, value);
}

void LocalPlayer::setPrevPitch(float value) {
    if (!isValid() || !fid_prevRotationPitch)
        return;
    Java::Env()->SetFloatField(instance, fid_prevRotationPitch, value);
}

// State
bool LocalPlayer::isOnGround() {
    if (!isValid() || !fid_onGround)
        return false;
    return Java::Env()->GetBooleanField(instance, fid_onGround);
}

void LocalPlayer::setOnGround(bool value) {
    if (!isValid() || !fid_onGround)
        return;
    Java::Env()->SetBooleanField(instance, fid_onGround, value);
}

bool LocalPlayer::isSprinting() {
    if (!isValid() || !mid_isSprinting)
        return false;
    return Java::Env()->CallBooleanMethod(instance, mid_isSprinting);
}

void LocalPlayer::setSprinting(bool value) {
    if (!isValid() || !mid_setSprinting)
        return;
    Java::Env()->CallVoidMethod(instance, mid_setSprinting, value);
}

bool LocalPlayer::isSneaking() {
    if (!isValid() || !mid_isSneaking)
        return false;
    return Java::Env()->CallBooleanMethod(instance, mid_isSneaking);
}

bool LocalPlayer::isUsingItem() {
    if (!isValid() || !mid_isUsingItem)
        return false;
    return Java::Env()->CallBooleanMethod(instance, mid_isUsingItem);
}

// Combat
float LocalPlayer::getHealth() {
    if (!isValid() || !mid_getHealth)
        return 0.0f;
    return Java::Env()->CallFloatMethod(instance, mid_getHealth);
}

int LocalPlayer::getHurtTime() {
    if (!isValid() || !fid_hurtTime)
        return 0;
    return Java::Env()->GetIntField(instance, fid_hurtTime);
}

void LocalPlayer::jump() {
    if (!isValid() || !mid_jump)
        return;
    Java::Env()->CallVoidMethod(instance, mid_jump);
}

void LocalPlayer::setJumpTicks(int value) {
    if (!isValid() || !fid_jumpTicks || !Java::Env())
        return;
    Java::Env()->SetIntField(instance, fid_jumpTicks, value);
    if (Java::Env()->ExceptionCheck()) {
        Java::Env()->ExceptionClear();
    }
}

int LocalPlayer::getHeldItemId() {
    if (!isValid())
        return -1;

    // Uses cached field/method IDs from initFields() to avoid JNI leaks
    // Previously this created 4 jclass locals per call, causing crashes after ~1 hour
    if (!fid_playerInventory || !mid_getCurrentItem || !mid_getItem || !mid_getIdFromItem || !cls_Item)
        return -1;

    jobject inventory = Java::Env()->GetObjectField(instance, fid_playerInventory);
    if (!inventory)
        return -1;

    jobject itemStack = Java::Env()->CallObjectMethod(inventory, mid_getCurrentItem);
    Java::Env()->DeleteLocalRef(inventory);

    if (!itemStack)
        return -1;  // Empty hand

    jobject item = Java::Env()->CallObjectMethod(itemStack, mid_getItem);
    Java::Env()->DeleteLocalRef(itemStack);

    if (!item)
        return -1;

    int result = Java::Env()->CallStaticIntMethod(cls_Item, mid_getIdFromItem, item);
    Java::Env()->DeleteLocalRef(item);

    if (Java::Env()->ExceptionCheck())
        Java::Env()->ExceptionClear();

    return result;
}


int LocalPlayer::getHeldItemDamage() {
    if (!isValid())
        return -1;

    // Chain: player.inventory.getCurrentItem().getMetadata()
    if (!fid_playerInventory || !mid_getCurrentItem || !mid_getMetadata)
        return -1;

    JNIEnv* env = Java::Env();
    if (!env)
        return -1;

    if (env->PushLocalFrame(16) < 0)
        return -1;

    jobject inventory = env->GetObjectField(instance, fid_playerInventory);
    if (!inventory) {
        env->PopLocalFrame(nullptr);
        return -1;
    }

    jobject itemStack = env->CallObjectMethod(inventory, mid_getCurrentItem);
    if (!itemStack) {
        env->PopLocalFrame(nullptr);
        return -1; // Empty hand
    }

    int result = env->CallIntMethod(itemStack, mid_getMetadata);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        result = -1;
    }

    env->PopLocalFrame(nullptr);
    return result;
}

int LocalPlayer::getItemInSlot(int slot) {
    if (!isValid())
        return -1;
    if (slot < 0 || slot > 35)
        return -1;  // Valid slots: 0-35 (hotbar + inventory)

    JNIEnv* env = Java::Env();
    if (!env)
        return -1;

    if (env->PushLocalFrame(16) < 0)
        return -1;

    if (!JniCache::cls_InventoryPlayer || !JniCache::fid_inventory || !JniCache::fid_mainInventory) {
        env->PopLocalFrame(nullptr);
        return -1;
    }

    jobject inventory = env->GetObjectField(instance, JniCache::fid_inventory);
    if (!inventory || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return -1;
    }

    jobjectArray mainInventory = (jobjectArray)env->GetObjectField(inventory, JniCache::fid_mainInventory);
    if (!mainInventory || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return -1;
    }

    jsize arrayLength = env->GetArrayLength(mainInventory);
    if (slot >= arrayLength) {
        env->PopLocalFrame(nullptr);
        return -1;
    }

    jobject itemStack = env->GetObjectArrayElement(mainInventory, slot);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return -1;
    }

    if (!itemStack) {
        env->PopLocalFrame(nullptr);
        return -1; // Empty slot
    }

    // Get itemStack.getItem() -> Item
    if (!JniCache::mid_getItem || !JniCache::cls_Item || !JniCache::mid_getIdFromItem) {
        env->PopLocalFrame(nullptr);
        return -1;
    }

    jobject item = env->CallObjectMethod(itemStack, JniCache::mid_getItem);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return -1;
    }

    if (!item) {
        env->PopLocalFrame(nullptr);
        return -1;
    }

    int itemId = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem, item);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        itemId = -1;
    }

    env->PopLocalFrame(nullptr);
    return itemId;
}

// ============================================================================
// Hotbar Slot Control - For AutoTool module
// Gets/sets the currently selected hotbar slot (0-8)
// ============================================================================

int LocalPlayer::getCurrentSlot() {
    if (!isValid())
        return 0;

    JNIEnv* env = Java::Env();
    if (!env)
        return 0;

    if (env->PushLocalFrame(16) < 0)
        return 0;

    if (!JniCache::fid_inventory || !JniCache::fid_currentItem) {
        env->PopLocalFrame(nullptr);
        return 0;
    }

    jobject inventory = env->GetObjectField(instance, JniCache::fid_inventory);
    if (!inventory || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return 0;
    }

    int currentSlot = env->GetIntField(inventory, JniCache::fid_currentItem);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        currentSlot = 0;
    }

    env->PopLocalFrame(nullptr);
    return currentSlot;
}

void LocalPlayer::setCurrentSlot(int slot) {
    if (!isValid())
        return;
    if (slot < 0 || slot > 8)
        return;  // Valid hotbar slots: 0-8

    JNIEnv* env = Java::Env();
    if (!env)
        return;

    if (env->PushLocalFrame(16) < 0)
        return;

    if (!JniCache::fid_inventory || !JniCache::fid_currentItem) {
        env->PopLocalFrame(nullptr);
        return;
    }

    jobject inventory = env->GetObjectField(instance, JniCache::fid_inventory);
    if (!inventory || env->ExceptionCheck()) {
        env->ExceptionClear();
        env->PopLocalFrame(nullptr);
        return;
    }

    env->SetIntField(inventory, JniCache::fid_currentItem, slot);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    env->PopLocalFrame(nullptr);
}

// ============================================================================
// lastReportedPos manipulation
// These fields control when Minecraft sends C03PacketPlayer
// By keeping lastReportedPos = currentPos, no movement packets are sent
// ============================================================================

double LocalPlayer::getLastReportedPosX() {
    if (!isValid() || !fid_lastReportedPosX)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_lastReportedPosX);
}

double LocalPlayer::getLastReportedPosY() {
    if (!isValid() || !fid_lastReportedPosY)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_lastReportedPosY);
}

double LocalPlayer::getLastReportedPosZ() {
    if (!isValid() || !fid_lastReportedPosZ)
        return 0.0;
    return Java::Env()->GetDoubleField(instance, fid_lastReportedPosZ);
}

void LocalPlayer::setLastReportedPosX(double value) {
    if (!isValid() || !fid_lastReportedPosX)
        return;
    Java::Env()->SetDoubleField(instance, fid_lastReportedPosX, value);
}

void LocalPlayer::setLastReportedPosY(double value) {
    if (!isValid() || !fid_lastReportedPosY)
        return;
    Java::Env()->SetDoubleField(instance, fid_lastReportedPosY, value);
}

void LocalPlayer::setLastReportedPosZ(double value) {
    if (!isValid() || !fid_lastReportedPosZ)
        return;
    Java::Env()->SetDoubleField(instance, fid_lastReportedPosZ, value);
}

float LocalPlayer::getLastReportedYaw() {
    if (!isValid() || !fid_lastReportedYaw)
        return 0.0f;
    return Java::Env()->GetFloatField(instance, fid_lastReportedYaw);
}

float LocalPlayer::getLastReportedPitch() {
    if (!isValid() || !fid_lastReportedPitch)
        return 0.0f;
    return Java::Env()->GetFloatField(instance, fid_lastReportedPitch);
}

void LocalPlayer::setLastReportedYaw(float value) {
    if (!isValid() || !fid_lastReportedYaw)
        return;
    Java::Env()->SetFloatField(instance, fid_lastReportedYaw, value);
}

void LocalPlayer::setLastReportedPitch(float value) {
    if (!isValid() || !fid_lastReportedPitch)
        return;
    Java::Env()->SetFloatField(instance, fid_lastReportedPitch, value);
}
