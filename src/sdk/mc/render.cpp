// Render-related functions for Minecraft SDK
// Handles render position, partial ticks, mouse sensitivity, and targeting info

#include "../../util/logger.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../jni/jstring_util.h"

// =============================================================================
// getMouseSensitivity - Get player's mouse sensitivity setting
// =============================================================================

float Minecraft::getMouseSensitivity() {
    if (!isValid())
        return 0.5f;  // Default

    JNIEnv* env = Java::Env();
    if (!env)
        return 0.5f;

    if (!JniCache::fid_gameSettings)
        return 0.5f;

    jobject gameSettings = env->GetObjectField(instance, JniCache::fid_gameSettings);
    if (!gameSettings)
        return 0.5f;

    float sensitivity = 0.5f;
    if (JniCache::fid_mouseSensitivity) {
        sensitivity = env->GetFloatField(gameSettings, JniCache::fid_mouseSensitivity);
    }

    env->DeleteLocalRef(gameSettings);
    return sensitivity;
}

// =============================================================================
// isTargetingBlock - Check if crosshair is aimed at a block
// =============================================================================

bool Minecraft::isTargetingBlock() {
    if (!isValid())
        return false;

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    if (!JniCache::fid_objectMouseOver)
        return false;

    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    if (!mouseOver)
        return false;

    if (!JniCache::fid_typeOfHit) {
        env->DeleteLocalRef(mouseOver);
        return false;
    }

    jobject typeOfHit = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(mouseOver); return false; }
    env->DeleteLocalRef(mouseOver);

    if (!typeOfHit)
        return false;

    // Compare with BLOCK enum constant
    bool result = false;
    if (JniCache::fid_BLOCK && JniCache::cls_MovingObjectType) {
        jobject blockEnum = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_BLOCK);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else {
            result = env->IsSameObject(typeOfHit, blockEnum);
            if (blockEnum)
                env->DeleteLocalRef(blockEnum);
        }
    }

    env->DeleteLocalRef(typeOfHit);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return result;
}

// =============================================================================
// getTargetedBlockPos - Get block position from game's objectMouseOver
// =============================================================================

Minecraft::Vec3 Minecraft::getTargetedBlockPos() {
    Vec3 result(-1.0f, -1.0f, -1.0f);  // Invalid position marker

    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    if (!JniCache::fid_objectMouseOver)
        return result;

    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return result; }
    if (!mouseOver)
        return result;

    bool gotPosition = false;

    // Method 1: 1.8.x - getBlockPos() -> BlockPos
    if (JniCache::mid_getBlockPos && JniCache::mid_getX && JniCache::mid_getY && JniCache::mid_getZ) {
        jobject blockPosObj = env->CallObjectMethod(mouseOver, JniCache::mid_getBlockPos);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else if (blockPosObj) {
            result.x = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getX);
            result.y = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getY);
            result.z = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getZ);
            env->DeleteLocalRef(blockPosObj);
            gotPosition = true;
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
    }

    // Method 2: 1.7.10 - direct blockX/blockY/blockZ fields
    if (!gotPosition && JniCache::fid_blockX && JniCache::fid_blockY && JniCache::fid_blockZ) {
        result.x = (float)env->GetIntField(mouseOver, JniCache::fid_blockX);
        result.y = (float)env->GetIntField(mouseOver, JniCache::fid_blockY);
        result.z = (float)env->GetIntField(mouseOver, JniCache::fid_blockZ);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else {
            gotPosition = true;
        }
    }

    env->DeleteLocalRef(mouseOver);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return result;
}

// =============================================================================
// isTargetingEntity - Check if crosshair is aimed at an entity
// =============================================================================

bool Minecraft::isTargetingEntity() {
    if (!isValid())
        return false;

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    if (!JniCache::fid_objectMouseOver)
        return false;

    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    if (!mouseOver)
        return false;

    if (!JniCache::fid_typeOfHit) {
        env->DeleteLocalRef(mouseOver);
        return false;
    }

    jobject typeOfHit = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(mouseOver); return false; }
    env->DeleteLocalRef(mouseOver);

    if (!typeOfHit)
        return false;

    // Compare with ENTITY enum constant
    bool result = false;
    if (JniCache::fid_ENTITY && JniCache::cls_MovingObjectType) {
        jobject entityEnum = env->GetStaticObjectField(JniCache::cls_MovingObjectType, JniCache::fid_ENTITY);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
        } else {
            result = env->IsSameObject(typeOfHit, entityEnum);
            if (entityEnum)
                env->DeleteLocalRef(entityEnum);
        }
    }

    env->DeleteLocalRef(typeOfHit);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return result;
}

// =============================================================================
// getTargetedEntityHurtTime - Get hurtTime of entity being looked at
// =============================================================================

int Minecraft::getTargetedEntityHurtTime() {
    if (!isValid())
        return 0;

    JNIEnv* env = Java::Env();
    if (!env)
        return 0;

    if (!JniCache::fid_objectMouseOver)
        return 0;

    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 0; }
    if (!mouseOver)
        return 0;

    if (!JniCache::fid_entityHit) {
        env->DeleteLocalRef(mouseOver);
        return 0;
    }

    jobject entityHit = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
    if (env->ExceptionCheck()) { env->ExceptionClear(); env->DeleteLocalRef(mouseOver); return 0; }
    env->DeleteLocalRef(mouseOver);

    if (!entityHit)
        return 0;

    // Check if it's a living entity
    if (!JniCache::cls_EntityLivingBase || !env->IsInstanceOf(entityHit, JniCache::cls_EntityLivingBase)) {
        env->DeleteLocalRef(entityHit);
        if (env->ExceptionCheck()) env->ExceptionClear();
        return 0;
    }

    int hurtTime = 0;
    if (JniCache::fid_hurtTime) {
        hurtTime = env->GetIntField(entityHit, JniCache::fid_hurtTime);
        if (env->ExceptionCheck()) { env->ExceptionClear(); hurtTime = 0; }
    }

    env->DeleteLocalRef(entityHit);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return hurtTime;
}

// =============================================================================
// getTargetedPlayerInfo - Get info (UUID, name) of player being looked at
// =============================================================================

PlayerInfo Minecraft::getTargetedPlayerInfo() {
    PlayerInfo result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    if (!JniCache::fid_objectMouseOver)
        return result;

    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (!mouseOver)
        return result;

    if (!JniCache::fid_entityHit) {
        env->DeleteLocalRef(mouseOver);
        return result;
    }

    jobject entityHit = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
    env->DeleteLocalRef(mouseOver);

    if (!entityHit)
        return result;

    // Check if it's a player entity
    if (!JniCache::cls_EntityPlayer || !env->IsInstanceOf(entityHit, JniCache::cls_EntityPlayer)) {
        env->DeleteLocalRef(entityHit);
        return result;
    }

    // UUID - Stable player identity (MOST RELIABLE)
    if (JniCache::mid_getUniqueID && JniCache::mid_uuidToString) {
        jobject uuidObj = env->CallObjectMethod(entityHit, JniCache::mid_getUniqueID);
        if (uuidObj) {
            jstring uuidStr = (jstring)env->CallObjectMethod(uuidObj, JniCache::mid_uuidToString);
            if (uuidStr) {
                result.uuid = jstringToString(uuidStr);
                env->DeleteLocalRef(uuidStr);
            }
            env->DeleteLocalRef(uuidObj);
        }
        env->ExceptionClear();
    }

    // GameProfile name FIRST (real name, not spoofable by plugins)
    if (JniCache::mid_getGameProfile && JniCache::mid_getProfileName) {
        jobject gp = env->CallObjectMethod(entityHit, JniCache::mid_getGameProfile);
        if (gp) {
            jstring profileName = (jstring)env->CallObjectMethod(gp, JniCache::mid_getProfileName);
            if (profileName) {
                result.profileName = jstringToString(profileName);
                env->DeleteLocalRef(profileName);
            }
            env->DeleteLocalRef(gp);
        }
        env->ExceptionClear();
    }

    // Name (could be obfuscated - fallback only)
    if (JniCache::mid_getName) {
        jstring nameStr = (jstring)env->CallObjectMethod(entityHit, JniCache::mid_getName);
        if (nameStr) {
            result.name = jstringToString(nameStr);
            env->DeleteLocalRef(nameStr);
        }
    }

    log_debug("[getTargetedPlayerInfo] uuid=%s, profileName=%s, name=%s", result.uuid.c_str(),
              result.profileName.c_str(), result.name.c_str());

    env->DeleteLocalRef(entityHit);
    return result;
}

// =============================================================================
// getRenderPos - Get camera render position for WorldToScreen
// =============================================================================

RenderPos Minecraft::getRenderPos() {
    RenderPos result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    bool is_1_7_10 = (Java::Version == MinecraftVersion::MC_1_7_10);

    if (is_1_7_10) {
        // 1.7.10: RenderManager uses STATIC fields
        if (JniCache::cls_RenderManager && JniCache::fid_renderPosX_static && JniCache::fid_renderPosY_static &&
            JniCache::fid_renderPosZ_static) {
            result.x = env->GetStaticDoubleField(JniCache::cls_RenderManager, JniCache::fid_renderPosX_static);
            result.y = env->GetStaticDoubleField(JniCache::cls_RenderManager, JniCache::fid_renderPosY_static);
            result.z = env->GetStaticDoubleField(JniCache::cls_RenderManager, JniCache::fid_renderPosZ_static);
        }
    } else {
        // 1.8.x: RenderManager uses INSTANCE fields
        if (!JniCache::fid_renderManager)
            return result;

        jobject renderManager = env->GetObjectField(instance, JniCache::fid_renderManager);
        if (!renderManager)
            return result;

        if (JniCache::fid_renderPosX && JniCache::fid_renderPosY && JniCache::fid_renderPosZ) {
            result.x = env->GetDoubleField(renderManager, JniCache::fid_renderPosX);
            result.y = env->GetDoubleField(renderManager, JniCache::fid_renderPosY);
            result.z = env->GetDoubleField(renderManager, JniCache::fid_renderPosZ);
        }

        env->DeleteLocalRef(renderManager);
    }

    return result;
}

// =============================================================================
// getPartialTicks - Get render interpolation value
// =============================================================================

float Minecraft::getPartialTicks() {
    if (!isValid())
        return 0.0f;

    JNIEnv* env = Java::Env();
    if (!env)
        return 0.0f;

    if (!JniCache::fid_timer)
        return 0.0f;

    jobject timer = env->GetObjectField(instance, JniCache::fid_timer);
    if (!timer)
        return 0.0f;

    float result = 0.0f;
    if (JniCache::fid_renderPartialTicks) {
        result = env->GetFloatField(timer, JniCache::fid_renderPartialTicks);
    }

    env->DeleteLocalRef(timer);
    return result;
}
