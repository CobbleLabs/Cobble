// Render-related functions for Minecraft SDK
// Handles render position, partial ticks, mouse sensitivity, and targeting info

#include "../../util/logger.hpp"
#include "../../util/aes_string.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../jni/jstring_util.h"

#include <gl/GL.h>

#pragma comment(lib, "opengl32.lib")

// Lazily-loaded glBlendColor (GL 1.4) for constant-alpha translucency of the
// backtrack ghost. Not in the base opengl32 import surface, so resolved via wgl.
typedef void(APIENTRY* PFN_glBlendColor_t)(GLfloat, GLfloat, GLfloat, GLfloat);
static PFN_glBlendColor_t g_glBlendColor = nullptr;
static bool g_glBlendColorTried = false;
#ifndef GL_CONSTANT_ALPHA
#define GL_CONSTANT_ALPHA 0x8003
#endif
#ifndef GL_ONE_MINUS_CONSTANT_ALPHA
#define GL_ONE_MINUS_CONSTANT_ALPHA 0x8004
#endif

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
    if (!mouseOver)
        return false;

    if (!JniCache::fid_typeOfHit) {
        env->DeleteLocalRef(mouseOver);
        return false;
    }

    jobject typeOfHit = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
    env->DeleteLocalRef(mouseOver);

    if (!typeOfHit)
        return false;

    // Compare with BLOCK enum constant
    bool result = false;
    if (JniCache::fid_BLOCK) {
        jclass typeClass = env->GetObjectClass(typeOfHit);
        if (typeClass) {
            jobject blockEnum = env->GetStaticObjectField(typeClass, JniCache::fid_BLOCK);
            result = env->IsSameObject(typeOfHit, blockEnum);
            if (blockEnum)
                env->DeleteLocalRef(blockEnum);
            env->DeleteLocalRef(typeClass);
        }
    }

    env->DeleteLocalRef(typeOfHit);
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
    if (!mouseOver)
        return result;

    bool gotPosition = false;

    // Method 1: 1.8.x - getBlockPos() -> BlockPos
    if (JniCache::mid_getBlockPos && JniCache::mid_getX && JniCache::mid_getY && JniCache::mid_getZ) {
        jobject blockPosObj = env->CallObjectMethod(mouseOver, JniCache::mid_getBlockPos);
        if (blockPosObj) {
            result.x = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getX);
            result.y = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getY);
            result.z = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getZ);
            env->DeleteLocalRef(blockPosObj);
            gotPosition = true;
        }
        env->ExceptionClear();
    }

    // Method 2: 1.7.10 - direct blockX/blockY/blockZ fields
    if (!gotPosition && JniCache::fid_blockX && JniCache::fid_blockY && JniCache::fid_blockZ) {
        result.x = (float)env->GetIntField(mouseOver, JniCache::fid_blockX);
        result.y = (float)env->GetIntField(mouseOver, JniCache::fid_blockY);
        result.z = (float)env->GetIntField(mouseOver, JniCache::fid_blockZ);
        gotPosition = true;
    }

    env->DeleteLocalRef(mouseOver);
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
    if (!mouseOver)
        return false;

    if (!JniCache::fid_typeOfHit) {
        env->DeleteLocalRef(mouseOver);
        return false;
    }

    jobject typeOfHit = env->GetObjectField(mouseOver, JniCache::fid_typeOfHit);
    env->DeleteLocalRef(mouseOver);

    if (!typeOfHit)
        return false;

    // Compare with ENTITY enum constant
    bool result = false;
    if (JniCache::fid_ENTITY) {
        jclass typeClass = env->GetObjectClass(typeOfHit);
        if (typeClass) {
            jobject entityEnum = env->GetStaticObjectField(typeClass, JniCache::fid_ENTITY);
            result = env->IsSameObject(typeOfHit, entityEnum);
            if (entityEnum)
                env->DeleteLocalRef(entityEnum);
            env->DeleteLocalRef(typeClass);
        }
    }

    env->DeleteLocalRef(typeOfHit);
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
    if (!mouseOver)
        return 0;

    if (!JniCache::fid_entityHit) {
        env->DeleteLocalRef(mouseOver);
        return 0;
    }

    jobject entityHit = env->GetObjectField(mouseOver, JniCache::fid_entityHit);
    env->DeleteLocalRef(mouseOver);

    if (!entityHit)
        return 0;

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

// =============================================================================
// renderGhostEntity - Re-render a live entity at an arbitrary world position.
// =============================================================================

void Minecraft::renderGhostEntity(int entityId, double wx, double wy, double wz, float r, float g, float b, float a, bool disableDepth, int renderMode) {
    if (!isValid() || entityId <= 0)
        return;

    JNIEnv* env = Java::Env();
    if (!env)
        return;
    if (env->ExceptionCheck())
        env->ExceptionClear();

    if (!g_glBlendColorTried) {
        g_glBlendColorTried = true;
        g_glBlendColor = reinterpret_cast<PFN_glBlendColor_t>(wglGetProcAddress("glBlendColor"));
    }

    // 1.8.x instance RenderManager only (matches the render-hook environment).
    if (!JniCache::fid_renderManager || !JniCache::fid_theWorld || !JniCache::cls_RenderManager)
        return;
    if (!JniCache::fid_renderPosX || !JniCache::fid_renderPosY || !JniCache::fid_renderPosZ)
        return;

    jobject renderManager = env->GetObjectField(instance, JniCache::fid_renderManager);
    if (!renderManager)
        return;
    jobject world = env->GetObjectField(instance, JniCache::fid_theWorld);
    if (!world) {
        env->DeleteLocalRef(renderManager);
        return;
    }

    // Lazy method/field IDs (render thread is single-threaded, so plain statics are fine).
    static jmethodID mid_getEntityByID = nullptr;
    static jmethodID mid_doRenderEntity = nullptr;
    static bool doRenderReturnsBool = false;
    static jfieldID fid_rotationYaw = nullptr;
    static jfieldID fid_prevRotationYaw = nullptr;
    static bool triedIds = false;
    if (!triedIds) {
        triedIds = true;
        jclass wcls = env->GetObjectClass(world);
        if (wcls) {
            mid_getEntityByID = env->GetMethodID(wcls, OBFUSCATE_STRING("getEntityByID").c_str(),
                                                 OBFUSCATE_STRING("(I)Lnet/minecraft/entity/Entity;").c_str());
            env->DeleteLocalRef(wcls);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
        // 1.8.x doRenderEntity returns boolean; try that first, then a void mapping.
        mid_doRenderEntity =
            env->GetMethodID(JniCache::cls_RenderManager, OBFUSCATE_STRING("doRenderEntity").c_str(),
                             OBFUSCATE_STRING("(Lnet/minecraft/entity/Entity;DDDFFZ)Z").c_str());
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (mid_doRenderEntity) {
            doRenderReturnsBool = true;
        } else {
            mid_doRenderEntity =
                env->GetMethodID(JniCache::cls_RenderManager, OBFUSCATE_STRING("doRenderEntity").c_str(),
                                 OBFUSCATE_STRING("(Lnet/minecraft/entity/Entity;DDDFFZ)V").c_str());
            if (env->ExceptionCheck())
                env->ExceptionClear();
        }
        log_debug("[BacktrackGhost] ids: getEntityByID=%p doRenderEntity=%p (bool=%d)", (void*)mid_getEntityByID,
                  (void*)mid_doRenderEntity, doRenderReturnsBool ? 1 : 0);
        jclass ecls = env->FindClass(OBFUSCATE_STRING("net/minecraft/entity/Entity").c_str());
        if (ecls) {
            fid_rotationYaw = env->GetFieldID(ecls, OBFUSCATE_STRING("rotationYaw").c_str(), "F");
            fid_prevRotationYaw = env->GetFieldID(ecls, OBFUSCATE_STRING("prevRotationYaw").c_str(), "F");
            env->DeleteLocalRef(ecls);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    if (mid_getEntityByID && mid_doRenderEntity) {
        jobject entity = env->CallObjectMethod(world, mid_getEntityByID, static_cast<jint>(entityId));
        if (env->ExceptionCheck())
            env->ExceptionClear();
        if (entity) {
            const double rpx = env->GetDoubleField(renderManager, JniCache::fid_renderPosX);
            const double rpy = env->GetDoubleField(renderManager, JniCache::fid_renderPosY);
            const double rpz = env->GetDoubleField(renderManager, JniCache::fid_renderPosZ);
            const float partial = getPartialTicks();
            float yaw = 0.0f;
            if (fid_rotationYaw) {
                const float ry = env->GetFloatField(entity, fid_rotationYaw);
                const float pry = fid_prevRotationYaw ? env->GetFloatField(entity, fid_prevRotationYaw) : ry;
                yaw = pry + (ry - pry) * partial;
            }

            const double dx = wx - rpx;
            const double dy = wy - rpy;
            const double dz = wz - rpz;

            // Save GL state
            const GLboolean wasBlend = glIsEnabled(GL_BLEND);
            GLint saveSrc = GL_SRC_ALPHA, saveDst = GL_ONE_MINUS_SRC_ALPHA;
            glGetIntegerv(GL_BLEND_SRC, &saveSrc);
            glGetIntegerv(GL_BLEND_DST, &saveDst);
            const GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean wasTex2D = glIsEnabled(GL_TEXTURE_2D);
            const GLboolean wasLighting = glIsEnabled(GL_LIGHTING);
            GLint savePolygonMode[2] = { GL_FILL, GL_FILL };
            glGetIntegerv(GL_POLYGON_MODE, savePolygonMode);
            GLfloat saveLineWidth = 1.0f;
            glGetFloatv(GL_LINE_WIDTH, &saveLineWidth);
            const GLboolean wasLineSmooth = glIsEnabled(GL_LINE_SMOOTH);

            // Helper lambda: call doRenderEntity on the JNI side
            auto callDoRender = [&]() {
                if (doRenderReturnsBool) {
                    env->CallBooleanMethod(renderManager, mid_doRenderEntity, entity,
                                           static_cast<jdouble>(dx), static_cast<jdouble>(dy), static_cast<jdouble>(dz),
                                           static_cast<jfloat>(yaw), static_cast<jfloat>(partial),
                                           static_cast<jboolean>(JNI_FALSE));
                } else {
                    env->CallVoidMethod(renderManager, mid_doRenderEntity, entity,
                                        static_cast<jdouble>(dx), static_cast<jdouble>(dy), static_cast<jdouble>(dz),
                                        static_cast<jfloat>(yaw), static_cast<jfloat>(partial),
                                        static_cast<jboolean>(JNI_FALSE));
                }
                if (env->ExceptionCheck())
                    env->ExceptionClear();
            };

            if (renderMode == 1) {
                // =====================================================
                // CHAMS MODE: Stencil-based silhouette outline / aura
                // =====================================================

                // --- Temporarily strip armor, held items & invisibility so the skin model is outlined ---
                static jmethodID mid_getEquip = nullptr;
                static jmethodID mid_setEquip = nullptr;
                static jmethodID mid_isInvisible = nullptr;
                static jmethodID mid_setInvisible = nullptr;
                static bool triedEquipIds = false;
                if (!triedEquipIds) {
                    triedEquipIds = true;
                    jclass entityCls = env->GetObjectClass(entity);
                    if (entityCls) {
                        mid_getEquip = env->GetMethodID(entityCls,
                            OBFUSCATE_STRING("getEquipmentInSlot").c_str(),
                            OBFUSCATE_STRING("(I)Lnet/minecraft/item/ItemStack;").c_str());
                        if (env->ExceptionCheck()) { env->ExceptionClear(); mid_getEquip = nullptr; }
                        mid_setEquip = env->GetMethodID(entityCls,
                            OBFUSCATE_STRING("setCurrentItemOrArmor").c_str(),
                            OBFUSCATE_STRING("(ILnet/minecraft/item/ItemStack;)V").c_str());
                        if (env->ExceptionCheck()) { env->ExceptionClear(); mid_setEquip = nullptr; }
                        mid_isInvisible = env->GetMethodID(entityCls,
                            OBFUSCATE_STRING("isInvisible").c_str(),
                            OBFUSCATE_STRING("()Z").c_str());
                        if (env->ExceptionCheck()) { env->ExceptionClear(); mid_isInvisible = nullptr; }
                        mid_setInvisible = env->GetMethodID(entityCls,
                            OBFUSCATE_STRING("setInvisible").c_str(),
                            OBFUSCATE_STRING("(Z)V").c_str());
                        if (env->ExceptionCheck()) { env->ExceptionClear(); mid_setInvisible = nullptr; }
                        env->DeleteLocalRef(entityCls);
                    }
                }

                // Temporarily remove invisibility so the model renders
                bool wasInvisible = false;
                if (mid_isInvisible && mid_setInvisible) {
                    wasInvisible = env->CallBooleanMethod(entity, mid_isInvisible);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                    if (wasInvisible) {
                        env->CallVoidMethod(entity, mid_setInvisible, (jboolean)JNI_FALSE);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    }
                }

                // Save and clear equipment (slots 0-4: hand, boots, legs, chest, helmet)
                jobject savedEquip[5] = { nullptr };
                bool stripped = (mid_getEquip && mid_setEquip);
                if (stripped) {
                    for (int s = 0; s < 5; s++) {
                        savedEquip[s] = env->CallObjectMethod(entity, mid_getEquip, s);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        env->CallVoidMethod(entity, mid_setEquip, s, (jobject)nullptr);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                    }
                }

                // Step 1: Render the SKIN-ONLY entity into stencil buffer (no color/depth writes)
                glClear(GL_STENCIL_BUFFER_BIT);
                glEnable(GL_STENCIL_TEST);
                glStencilFunc(GL_ALWAYS, 1, 0xFF);
                glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                glStencilMask(0xFF);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                glDepthMask(GL_FALSE);

                callDoRender();

                // Step 2: Render solid glow outline ONLY where stencil != 1 (silhouetted out)
                glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
                glStencilMask(0x00);
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glDepthMask(GL_TRUE);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_LIGHTING);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                // Use solid fill mode to render a true filled glowing aura
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

                // Determine center of mass height based on sneaking state
                static jmethodID mid_isSneaking = nullptr;
                static bool triedSneakId = false;
                if (!triedSneakId) {
                    triedSneakId = true;
                    jclass entityCls = env->GetObjectClass(entity);
                    if (entityCls) {
                        mid_isSneaking = env->GetMethodID(entityCls, OBFUSCATE_STRING("isSneaking").c_str(), "()Z");
                        if (env->ExceptionCheck()) { env->ExceptionClear(); mid_isSneaking = nullptr; }
                        env->DeleteLocalRef(entityCls);
                    }
                }
                bool isSneaking = false;
                if (mid_isSneaking) {
                    isSneaking = env->CallBooleanMethod(entity, mid_isSneaking);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }
                float centerY = isSneaking ? 0.70f : 0.90f;

                // Scale around the entity's center of mass to prevent shifting offsets
                glPushMatrix();
                glTranslatef(0.0f, centerY, 0.0f);
                glScalef(1.055f, 1.055f, 1.055f);
                glTranslatef(0.0f, -centerY, 0.0f);

                // Draw the solid glow outline
                glColor4f(r, g, b, a * 0.55f);
                callDoRender();

                glPopMatrix();

                // Cleanup stencil
                glDisable(GL_STENCIL_TEST);
                glStencilMask(0xFF);

                // --- Restore equipment & invisibility ---
                if (stripped) {
                    for (int s = 0; s < 5; s++) {
                        env->CallVoidMethod(entity, mid_setEquip, s, savedEquip[s]);
                        if (env->ExceptionCheck()) env->ExceptionClear();
                        if (savedEquip[s]) env->DeleteLocalRef(savedEquip[s]);
                    }
                }

                if (wasInvisible && mid_setInvisible) {
                    env->CallVoidMethod(entity, mid_setInvisible, (jboolean)JNI_TRUE);
                    if (env->ExceptionCheck()) env->ExceptionClear();
                }

            } else if (renderMode == 2) {
                // =====================================================
                // FILL MODE: Solid flat color model rendered through walls
                // =====================================================
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_LIGHTING);
                if (disableDepth)
                    glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(r, g, b, a);

                callDoRender();
            } else {
                // =====================================================
                // DEFAULT GHOST MODE (used by Backtrack): Translucent textured
                // =====================================================
                if (disableDepth)
                    glDisable(GL_DEPTH_TEST);
                glEnable(GL_BLEND);
                if (g_glBlendColor) {
                    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
                    g_glBlendColor(r, g, b, a);
                } else {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                callDoRender();
            }

            // Restore all saved GL state
            if (wasTex2D) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
            if (wasLighting) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
            if (wasDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            glBlendFunc(saveSrc, saveDst);
            if (wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            glPolygonMode(GL_FRONT, savePolygonMode[0]);
            glPolygonMode(GL_BACK, savePolygonMode[1]);
            glLineWidth(saveLineWidth);
            if (wasLineSmooth) glEnable(GL_LINE_SMOOTH); else glDisable(GL_LINE_SMOOTH);
            if (g_glBlendColor)
                g_glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);   // Reset to white

            env->DeleteLocalRef(entity);
        } else {
            static bool warnedNull = false;
            if (!warnedNull) {
                warnedNull = true;
                log_debug("[BacktrackGhost] getEntityByID(%d) returned null", entityId);
            }
        }
    }

    env->DeleteLocalRef(world);
    env->DeleteLocalRef(renderManager);
}
