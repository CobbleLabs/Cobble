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
#include "../../../infra/ext/minhook/minhook.h"

typedef void(APIENTRY* glDepthFunc_t)(GLenum);
typedef void(APIENTRY* glDepthMask_t)(GLboolean);

static glDepthFunc_t o_glDepthFunc = nullptr;
static glDepthMask_t o_glDepthMask = nullptr;

static bool g_DisableDepthOverride = false;

static void APIENTRY hk_glDepthFunc(GLenum func) {
    if (g_DisableDepthOverride) {
        o_glDepthFunc(GL_ALWAYS);
    } else {
        o_glDepthFunc(func);
    }
}

static void APIENTRY hk_glDepthMask(GLboolean flag) {
    if (g_DisableDepthOverride) {
        o_glDepthMask(GL_FALSE);
    } else {
        o_glDepthMask(flag);
    }
}

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

void Minecraft::renderGhostEntity(int entityId, double wx, double wy, double wz, float r, float g, float b, float a,
                                  bool disableDepth, int renderMode, bool forceVisible) {
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

        // Initialize OpenGL hooks lazily
        static bool triedGlHooks = false;
        if (!triedGlHooks) {
            triedGlHooks = true;
            HMODULE hGl = GetModuleHandleA("opengl32.dll");
            if (hGl) {
                LPVOID pDepthFunc = (LPVOID)GetProcAddress(hGl, "glDepthFunc");
                LPVOID pDepthMask = (LPVOID)GetProcAddress(hGl, "glDepthMask");
                if (pDepthFunc && pDepthMask) {
                    MH_CreateHook(pDepthFunc, (LPVOID)hk_glDepthFunc, (LPVOID*)&o_glDepthFunc);
                    MH_CreateHook(pDepthMask, (LPVOID)hk_glDepthMask, (LPVOID*)&o_glDepthMask);
                    MH_EnableHook(pDepthFunc);
                    MH_EnableHook(pDepthMask);
                    log_ok("Chams glDepthFunc/glDepthMask hooks installed successfully!");
                }
            }
        }
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

            // Re-rendering via RenderManager would normally draw a second nametag.
            // Hide the GUI only for this call, then restore the user's setting.
            static jfieldID fid_hideGUI = nullptr;
            static bool triedHideGUI = false;
            if (!triedHideGUI) {
                triedHideGUI = true;
                if (JniCache::cls_GameSettings) {
                    fid_hideGUI = env->GetFieldID(JniCache::cls_GameSettings, "hideGUI", "Z");
                    if (!fid_hideGUI) {
                        if (env->ExceptionCheck())
                            env->ExceptionClear();
                        fid_hideGUI = env->GetFieldID(JniCache::cls_GameSettings, "field_74319_N", "Z");
                    }
                    if (env->ExceptionCheck())
                        env->ExceptionClear();
                }
            }

            jobject gameSettings = nullptr;
            jboolean hideGUIBefore = JNI_FALSE;
            if (fid_hideGUI && JniCache::fid_gameSettings) {
                gameSettings = env->GetObjectField(instance, JniCache::fid_gameSettings);
                if (gameSettings) {
                    hideGUIBefore = env->GetBooleanField(gameSettings, fid_hideGUI);
                    env->SetBooleanField(gameSettings, fid_hideGUI, JNI_TRUE);
                }
                if (env->ExceptionCheck())
                    env->ExceptionClear();
            }

            static jmethodID mid_isInvisible = nullptr;
            static jmethodID mid_setInvisible = nullptr;
            static bool triedInvisibleIds = false;
            if (!triedInvisibleIds) {
                triedInvisibleIds = true;
                jclass entityClass = env->GetObjectClass(entity);
                if (entityClass) {
                    mid_isInvisible = env->GetMethodID(entityClass, "isInvisible", "()Z");
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        mid_isInvisible = nullptr;
                    }
                    mid_setInvisible = env->GetMethodID(entityClass, "setInvisible", "(Z)V");
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        mid_setInvisible = nullptr;
                    }
                    env->DeleteLocalRef(entityClass);
                }
            }

            const bool shouldForceVisible = forceVisible || renderMode == 1;
            jboolean invisibleBefore = JNI_FALSE;
            if (shouldForceVisible && mid_isInvisible && mid_setInvisible) {
                invisibleBefore = env->CallBooleanMethod(entity, mid_isInvisible);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    invisibleBefore = JNI_FALSE;
                } else if (invisibleBefore == JNI_TRUE) {
                    env->CallVoidMethod(entity, mid_setInvisible, JNI_FALSE);
                    if (env->ExceptionCheck())
                        env->ExceptionClear();
                }
            }

            // Save GL state
            const GLboolean wasBlend = glIsEnabled(GL_BLEND);
            GLint saveSrc = GL_SRC_ALPHA, saveDst = GL_ONE_MINUS_SRC_ALPHA;
            glGetIntegerv(GL_BLEND_SRC, &saveSrc);
            glGetIntegerv(GL_BLEND_DST, &saveDst);
            const GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
            GLint saveDepthFunc = GL_LEQUAL;
            GLboolean saveDepthMask = GL_TRUE;
            glGetIntegerv(GL_DEPTH_FUNC, &saveDepthFunc);
            glGetBooleanv(GL_DEPTH_WRITEMASK, &saveDepthMask);
            const GLboolean wasPolygonOffset = glIsEnabled(GL_POLYGON_OFFSET_FILL);
            GLfloat savePolygonOffsetFactor = 0.0f;
            GLfloat savePolygonOffsetUnits = 0.0f;
            glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &savePolygonOffsetFactor);
            glGetFloatv(GL_POLYGON_OFFSET_UNITS, &savePolygonOffsetUnits);
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

            // Keep the pass in front of terrain even if Minecraft's renderer
            // touches GL_DEPTH_TEST internally. GL_ALWAYS is more reliable here
            // than disabling depth alone because GlStateManager caches its own
            // enable/disable state and can otherwise undo a raw OpenGL toggle.
            auto enableThroughWalls = [&]() {
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_ALWAYS);
                glDepthMask(GL_FALSE);
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(1.0f, -1000000.0f);
            };

            if (renderMode == 1) {
                // =====================================================
                // CHAMS MODE: Stencil-based silhouette outline / aura
                // =====================================================

                // --- Temporarily strip armor, held items & invisibility so the skin model is outlined ---
                static jmethodID mid_getEquip = nullptr;
                static jmethodID mid_setEquip = nullptr;
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
                        env->DeleteLocalRef(entityCls);
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

                // Soft, diffused glow outline: instead of one hard scaled shell,
                // stack several progressively larger shells with ADDITIVE blending
                // and a falloff in alpha, all masked to stencil != 1 (outside the
                // real silhouette). The overlapping shells accumulate into a blurred
                // halo that's brightest hugging the model and fades outward — a real
                // glow rather than a solid rim. Scaled around the center of mass so
                // it doesn't drift off the model.
                glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // additive accumulation
                const int GLOW_PASSES = 6;
                for (int gi = 0; gi < GLOW_PASSES; ++gi) {
                    const float gt = (float)gi / (float)(GLOW_PASSES - 1);  // 0 inner .. 1 outer
                    const float gscale = 1.02f + 0.17f * gt;               // hugs the skin, blooms out
                    const float galpha = a * (0.14f * (1.0f - gt) + 0.015f);
                    glPushMatrix();
                    glTranslatef(0.0f, centerY, 0.0f);
                    glScalef(gscale, gscale, gscale);
                    glTranslatef(0.0f, -centerY, 0.0f);
                    glColor4f(r, g, b, galpha);
                    callDoRender();
                    glPopMatrix();
                }

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

            } else if (renderMode == 2) {
                // =====================================================
                // FILL MODE: Solid flat color model rendered through walls
                // =====================================================
                glDisable(GL_TEXTURE_2D);
                glDisable(GL_LIGHTING);
                if (disableDepth) {
                    g_DisableDepthOverride = true;
                    enableThroughWalls();
                }
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glColor4f(r, g, b, a);

                callDoRender();
                g_DisableDepthOverride = false;
            } else {
                // =====================================================
                // DEFAULT GHOST MODE (used by Backtrack): Translucent textured
                // =====================================================
                if (disableDepth) {
                    g_DisableDepthOverride = true;
                    enableThroughWalls();
                }
                glEnable(GL_BLEND);
                if (g_glBlendColor) {
                    glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
                    g_glBlendColor(r, g, b, a);
                } else {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                callDoRender();
                g_DisableDepthOverride = false;
            }

            // Restore all saved GL state
            if (wasTex2D) glEnable(GL_TEXTURE_2D); else glDisable(GL_TEXTURE_2D);
            if (wasLighting) glEnable(GL_LIGHTING); else glDisable(GL_LIGHTING);
            glDepthFunc(saveDepthFunc);
            glDepthMask(saveDepthMask);
            if (wasDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            glPolygonOffset(savePolygonOffsetFactor, savePolygonOffsetUnits);
            if (wasPolygonOffset) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
            glBlendFunc(saveSrc, saveDst);
            if (wasBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            glPolygonMode(GL_FRONT, savePolygonMode[0]);
            glPolygonMode(GL_BACK, savePolygonMode[1]);
            glLineWidth(saveLineWidth);
            if (wasLineSmooth) glEnable(GL_LINE_SMOOTH); else glDisable(GL_LINE_SMOOTH);
            if (g_glBlendColor)
                g_glBlendColor(0.0f, 0.0f, 0.0f, 0.0f);
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);   // Reset to white

            if (invisibleBefore == JNI_TRUE && mid_setInvisible) {
                env->CallVoidMethod(entity, mid_setInvisible, JNI_TRUE);
                if (env->ExceptionCheck())
                    env->ExceptionClear();
            }
            if (gameSettings) {
                env->SetBooleanField(gameSettings, fid_hideGUI, hideGUIBefore);
                if (env->ExceptionCheck())
                    env->ExceptionClear();
                env->DeleteLocalRef(gameSettings);
            }

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
