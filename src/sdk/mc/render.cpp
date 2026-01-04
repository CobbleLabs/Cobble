// Render-related functions for Minecraft SDK
// Handles render position, partial ticks, mouse sensitivity, and targeting info

#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../../util/logger.hpp"

// =============================================================================
// getMouseSensitivity - Get player's mouse sensitivity setting
// =============================================================================

float Minecraft::getMouseSensitivity()
{
    if (!isValid()) return 0.5f; // Default

    JNIEnv* env = Java::GetEnv();
    if (!env) return 0.5f;

    if (!JniCache::fid_gameSettings) return 0.5f;
    
    jobject gameSettings = env->GetObjectField(instance, JniCache::fid_gameSettings);
    if (!gameSettings) return 0.5f;

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

bool Minecraft::isTargetingBlock()
{
    if (!isValid()) return false;

    JNIEnv* env = Java::GetEnv();
    if (!env) return false;

    if (!JniCache::fid_objectMouseOver) return false;
    
    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) return false;

    if (!JniCache::fid_typeOfHit) {
        env->DeleteLocalRef(mouseOver);
        return false;
    }

    jobject typeOfHit = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
    env->DeleteLocalRef(mouseOver);

    if (!typeOfHit) return false;

    // Compare with BLOCK enum constant
    bool result = false;
    if (JniCache::fid_BLOCK) {
        jclass typeClass = env->GetObjectClass(typeOfHit);
        if (typeClass) {
            jobject blockEnum = env->GetStaticObjectField(typeClass, JniCache::fid_BLOCK);
            result = env->IsSameObject(typeOfHit, blockEnum);
            if (blockEnum) env->DeleteLocalRef(blockEnum);
            env->DeleteLocalRef(typeClass);
        }
    }

    env->DeleteLocalRef(typeOfHit);
    return result;
}

// =============================================================================
// isTargetingEntity - Check if crosshair is aimed at an entity
// =============================================================================

bool Minecraft::isTargetingEntity()
{
    if (!isValid()) return false;

    JNIEnv* env = Java::GetEnv();
    if (!env) return false;

    if (!JniCache::fid_objectMouseOver) return false;
    
    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) return false;

    if (!JniCache::fid_typeOfHit) {
        env->DeleteLocalRef(mouseOver);
        return false;
    }

    jobject typeOfHit = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
    env->DeleteLocalRef(mouseOver);

    if (!typeOfHit) return false;

    // Compare with ENTITY enum constant
    bool result = false;
    if (JniCache::fid_ENTITY) {
        jclass typeClass = env->GetObjectClass(typeOfHit);
        if (typeClass) {
            jobject entityEnum = env->GetStaticObjectField(typeClass, JniCache::fid_ENTITY);
            result = env->IsSameObject(typeOfHit, entityEnum);
            if (entityEnum) env->DeleteLocalRef(entityEnum);
            env->DeleteLocalRef(typeClass);
        }
    }

    env->DeleteLocalRef(typeOfHit);
    return result;
}

// =============================================================================
// getTargetedEntityHurtTime - Get hurtTime of entity being looked at
// =============================================================================

int Minecraft::getTargetedEntityHurtTime()
{
    if (!isValid()) return 0;

    JNIEnv* env = Java::GetEnv();
    if (!env) return 0;

    if (!JniCache::fid_objectMouseOver) return 0;
    
    jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
    if (!mouseOver) return 0;

    if (!JniCache::fid_entityHit) {
        env->DeleteLocalRef(mouseOver);
        return 0;
    }

    jobject entityHit = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
    env->DeleteLocalRef(mouseOver);

    if (!entityHit) return 0;

    // Check if it's a living entity
    if (!JniCache::cls_EntityLivingBase || !env->IsInstanceOf(entityHit, JniCache::cls_EntityLivingBase)) {
        env->DeleteLocalRef(entityHit);
        return 0;
    }

    int hurtTime = 0;
    if (JniCache::fid_hurtTime) {
        hurtTime = env->GetIntField(entityHit, JniCache::fid_hurtTime);
    }

    env->DeleteLocalRef(entityHit);
    return hurtTime;
}

// =============================================================================
// getRenderPos - Get camera render position for WorldToScreen
// =============================================================================

RenderPos Minecraft::getRenderPos()
{
    RenderPos result;
    if (!isValid()) return result;

    JNIEnv* env = Java::GetEnv();
    if (!env) return result;

    bool is_1_7_10 = (Java::Version == MinecraftVersion::MC_1_7_10);

    if (is_1_7_10) {
        // 1.7.10: RenderManager uses STATIC fields
        if (JniCache::cls_RenderManager && 
            JniCache::fid_renderPosX_static && 
            JniCache::fid_renderPosY_static && 
            JniCache::fid_renderPosZ_static) {
            result.x = env->GetStaticDoubleField(JniCache::cls_RenderManager, JniCache::fid_renderPosX_static);
            result.y = env->GetStaticDoubleField(JniCache::cls_RenderManager, JniCache::fid_renderPosY_static);
            result.z = env->GetStaticDoubleField(JniCache::cls_RenderManager, JniCache::fid_renderPosZ_static);
        }
    } else {
        // 1.8.x: RenderManager uses INSTANCE fields
        if (!JniCache::fid_renderManager) return result;
        
        jobject renderManager = env->GetObjectField(instance, JniCache::fid_renderManager);
        if (!renderManager) return result;

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

float Minecraft::getPartialTicks()
{
    if (!isValid()) return 0.0f;

    JNIEnv* env = Java::GetEnv();
    if (!env) return 0.0f;

    if (!JniCache::fid_timer) return 0.0f;
    
    jobject timer = env->GetObjectField(instance, JniCache::fid_timer);
    if (!timer) return 0.0f;

    float result = 0.0f;
    if (JniCache::fid_renderPartialTicks) {
        result = env->GetFloatField(timer, JniCache::fid_renderPartialTicks);
    }

    env->DeleteLocalRef(timer);
    return result;
}
