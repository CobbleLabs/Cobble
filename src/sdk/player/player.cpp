#include "player.h"
#include "../../util/logger.hpp"
#include "../../util/aes_string.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../jni/member_resolver.h"

#include <initializer_list>

namespace {

jfieldID resolveField(jclass cls, std::initializer_list<const char*> names, const char* signature) {
    if (!cls || !signature)
        return nullptr;

    JNIEnv* env = Java::Env();
    const auto signatures = jni_resolver::signatureVariants(signature);
    for (const char* name : names) {
        if (!name)
            continue;
        for (const auto& candidate : signatures) {
            jfieldID field = env->GetFieldID(cls, name, candidate.c_str());
            if (field)
                return field;
            env->ExceptionClear();
        }
    }
    return jni_resolver::findUniqueFieldBySignature(cls, signature, false);
}

jmethodID resolveMethod(jclass cls, std::initializer_list<const char*> names, const char* signature,
                        bool isStatic = false) {
    if (!cls || !signature)
        return nullptr;

    JNIEnv* env = Java::Env();
    const auto signatures = jni_resolver::signatureVariants(signature);
    for (const char* name : names) {
        if (!name)
            continue;
        for (const auto& candidate : signatures) {
            jmethodID method = isStatic ? env->GetStaticMethodID(cls, name, candidate.c_str())
                                        : env->GetMethodID(cls, name, candidate.c_str());
            if (method)
                return method;
            env->ExceptionClear();
        }
    }
    return jni_resolver::findUniqueMethodBySignature(cls, signature, isStatic);
}

}  // namespace

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
    fid_posX = resolveField(entityCls, {"posX", "field_70165_t", "s"}, "D");
    fid_posY = resolveField(entityCls, {"posY", "field_70163_u", "t"}, "D");
    fid_posZ = resolveField(entityCls, {"posZ", "field_70161_v", "u"}, "D");

    // Last tick position fields (for interpolation)
    fid_lastPosX = resolveField(entityCls, {"lastTickPosX", "field_70142_S", "m"}, "D");
    fid_lastPosY = resolveField(entityCls, {"lastTickPosY", "field_70137_T", "n"}, "D");
    fid_lastPosZ = resolveField(entityCls, {"lastTickPosZ", "field_70136_U", "o"}, "D");

    // Motion fields (in Entity)
    fid_motionX = resolveField(entityCls, {"motionX", "field_70159_w", "v"}, "D");
    fid_motionY = resolveField(entityCls, {"motionY", "field_70181_x", "w"}, "D");
    fid_motionZ = resolveField(entityCls, {"motionZ", "field_70179_y", "x"}, "D");

    // Rotation fields (in Entity)
    fid_rotationYaw = resolveField(entityCls, {"rotationYaw", "field_70177_z", "y"}, "F");
    fid_rotationPitch = resolveField(entityCls, {"rotationPitch", "field_70125_A", "z"}, "F");
    fid_prevRotationYaw = resolveField(entityCls, {"prevRotationYaw", "field_70126_B", "A"}, "F");
    fid_prevRotationPitch = resolveField(entityCls, {"prevRotationPitch", "field_70127_C", "B"}, "F");

    // On ground (in Entity)
    fid_onGround = resolveField(entityCls, {"onGround", "field_70122_E", "C"}, "Z");

    // Sprint/sneak methods (in Entity)
    mid_isSprinting = resolveMethod(entityCls, {"isSprinting", "func_70051_ag", "aw"}, "()Z");
    mid_setSprinting = resolveMethod(entityCls, {"setSprinting", "func_70031_b", "g"}, "(Z)V");
    mid_isSneaking = resolveMethod(entityCls, {"isSneaking", "func_70093_af", "av"}, "()Z");

    if (livingCls) {
        // Health (in EntityLivingBase)
        mid_getHealth = resolveMethod(livingCls, {"getHealth", "func_110143_aJ", "bn"}, "()F");

        // HurtTime (in EntityLivingBase)
        fid_hurtTime = resolveField(livingCls, {"hurtTime", "field_70737_aN", "ax"}, "I");

        // Jump (in EntityLivingBase)
        mid_jump = resolveMethod(livingCls, {"jump", "func_70664_aZ", "bF"}, "()V");

        // JumpTicks (in EntityLivingBase) - controls jump delay
        fid_jumpTicks = resolveField(livingCls, {"jumpTicks", "field_70773_bE"}, "I");

    }

    // Item-use state belongs to EntityPlayer, not EntityLivingBase. Looking it
    // up on the living class leaves a pending NoSuchMethodError and disables
    // every module that relies on this state.
    if (JniCache::cls_EntityPlayer) {
        mid_isUsingItem = resolveMethod(
            JniCache::cls_EntityPlayer, {"isUsingItem", "func_71039_bw", "bS"}, "()Z");
        if (!mid_isUsingItem && Java::Env()->ExceptionCheck())
            Java::Env()->ExceptionClear();

        // Bow draw progress. Resolved by exact name only, never by signature:
        // EntityPlayer carries a dozen int fields, so resolveField()'s
        // find-the-unique-one fallback would hand back an unrelated counter.
        JNIEnv* env = Java::Env();
        fid_itemInUseCount = env->GetFieldID(JniCache::cls_EntityPlayer, "itemInUseCount", "I");
        if (!fid_itemInUseCount) {
            env->ExceptionClear();
            fid_itemInUseCount = env->GetFieldID(JniCache::cls_EntityPlayer, "field_71072_f", "I");
        }
        if (!fid_itemInUseCount)
            env->ExceptionClear();
    }

    // Need to get the client-player class for these. Badlion 1.8.9 uses the
    // Forge/SRG member names even when the class itself is MCP-named.
    jclass playerSPCls = nullptr;
    const bool is_1_7_10 = Java::Version == MinecraftVersion::MC_1_7_10;
    const bool foundPlayerClass =
        is_1_7_10
            ? Java::AssignClass(OBFUSCATE_STRING("net.minecraft.client.entity.EntityClientPlayerMP"), playerSPCls)
            : Java::AssignClass(OBFUSCATE_STRING("net.minecraft.client.entity.EntityPlayerSP"), playerSPCls);
    if (foundPlayerClass) {
        const char* posXName = is_1_7_10 ? "oldPosX" : "lastReportedPosX";
        const char* posYName = is_1_7_10 ? "oldPosY" : "lastReportedPosY";
        const char* posZName = is_1_7_10 ? "oldPosZ" : "lastReportedPosZ";
        const char* yawName = is_1_7_10 ? "oldRotationYaw" : "lastReportedYaw";
        const char* pitchName = is_1_7_10 ? "oldRotationPitch" : "lastReportedPitch";
        fid_lastReportedPosX = resolveField(playerSPCls, {posXName, is_1_7_10 ? nullptr : "field_175172_bI",
                                                        is_1_7_10 ? nullptr : "d"}, "D");
        fid_lastReportedPosY = resolveField(playerSPCls, {posYName, is_1_7_10 ? nullptr : "field_175166_bJ",
                                                        is_1_7_10 ? nullptr : "e"}, "D");
        fid_lastReportedPosZ = resolveField(playerSPCls, {posZName, is_1_7_10 ? nullptr : "field_175167_bK",
                                                        is_1_7_10 ? nullptr : "f"}, "D");
        fid_lastReportedYaw = resolveField(playerSPCls, {yawName, is_1_7_10 ? nullptr : "field_175164_bL",
                                                       is_1_7_10 ? nullptr : "g"}, "F");
        fid_lastReportedPitch = resolveField(playerSPCls, {pitchName, is_1_7_10 ? nullptr : "field_175165_bM",
                                                         is_1_7_10 ? nullptr : "h"}, "F");

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

        fid_playerInventory = JniCache::fid_inventory
                                  ? JniCache::fid_inventory
                                  : resolveField(playerCls, {"inventory", "field_71071_by", "bi"},
                                                 "Lnet/minecraft/entity/player/InventoryPlayer;");
        mid_getCurrentItem = resolveMethod(inventoryCls, {"getCurrentItem", "func_70448_g", "h"},
                                           "()Lnet/minecraft/item/ItemStack;");
        mid_getItem = JniCache::mid_getItem
                          ? JniCache::mid_getItem
                          : resolveMethod(itemStackCls, {"getItem", "func_77973_b", "b"},
                                          "()Lnet/minecraft/item/Item;");
        mid_getIdFromItem = JniCache::mid_getIdFromItem
                                ? JniCache::mid_getIdFromItem
                                : resolveMethod(itemCls, {"getIdFromItem", "func_150891_b", "a"},
                                                "(Lnet/minecraft/item/Item;)I", true);
        mid_getMetadata = JniCache::mid_getItemDamage
                              ? JniCache::mid_getItemDamage
                              : resolveMethod(itemStackCls, {"getMetadata", "func_77960_j", "j"}, "()I");

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

void LocalPlayer::cleanup() {
    std::lock_guard<std::recursive_mutex> lock(instanceMutex);
    if (cls_Item && Java::Initialized) {
        JNIEnv* env = Java::Env();
        if (env) {
            env->DeleteGlobalRef(cls_Item);
            cls_Item = nullptr;
        }
    }
    fieldsInitialized = false;
}

void LocalPlayer::refresh() {
    std::lock_guard<std::recursive_mutex> lock(instanceMutex);

    // Safety check - if no valid JNI environment, don't try to refresh
    if (!Java::Env() || !Java::Initialized) {
        if (instance) {
            Java::Env()->DeleteGlobalRef(instance);
            instance = nullptr;  // Invalidate cached instance
        }
        return;
    }

    jobject localPlayer = mc.getPlayer();
    if (localPlayer) {
        // Check if the player instance actually changed
        if (instance && Java::Env()->IsSameObject(instance, localPlayer) == JNI_TRUE) {
            Java::Env()->DeleteLocalRef(localPlayer);
            return; // Same player instance, do nothing and keep it stable!
        }

        // Object changed: delete old global reference
        if (instance) {
            Java::Env()->DeleteGlobalRef(instance);
            instance = nullptr;
        }

        instance = Java::Env()->NewGlobalRef(localPlayer);
        Java::Env()->DeleteLocalRef(localPlayer);
    } else {
        // No local player
        if (instance) {
            Java::Env()->DeleteGlobalRef(instance);
            instance = nullptr;
        }
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

int LocalPlayer::getItemInUseTicks() {
    if (!isValid() || !fid_itemInUseCount)
        return 0;
    // setItemInUse() seeds the counter with the stack's max use duration and
    // updateItemUse() decrements it once per tick, so the elapsed draw is the
    // difference. 72000 is the bow's duration (ItemBow.getMaxItemUseDuration);
    // callers holding something else must not read this as a tick count.
    const int count = Java::Env()->GetIntField(instance, fid_itemInUseCount);
    if (count <= 0)
        return 0;
    const int drawn = 72000 - count;
    return drawn < 0 ? 0 : drawn;
}

// Combat
float LocalPlayer::getHealth() {
    if (!isValid() || !mid_getHealth)
        return 0.0f;
    return Java::Env()->CallFloatMethod(instance, mid_getHealth);
}

float LocalPlayer::getMaxHealth() {
    if (!isValid() || !JniCache::mid_getMaxHealth)
        return 20.0f;
    JNIEnv* env = Java::Env();
    if (!env)
        return 20.0f;
    const float result = env->CallFloatMethod(instance, JniCache::mid_getMaxHealth);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 20.0f;
    }
    return result > 0.0f ? result : 20.0f;
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
