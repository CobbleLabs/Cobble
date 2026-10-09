// World-related functions for Minecraft SDK
// Handles block queries and raytrace operations

#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"

// =============================================================================
// getBlockId - Get block ID at world coordinates
// =============================================================================

int Minecraft::getBlockId(int x, int y, int z) {
    if (!isValid())
        return 0;

    if (y < 0 || y > 255)
        return 0;

    JNIEnv* env = Java::Env();
    if (!env)
        return 0;

    if (env->PushLocalFrame(16) < 0)
        return 0;

    int blockId = 0;

    // 1.7.10: World.getBlock(int, int, int) -> Block
    if (Java::Version == MinecraftVersion::MC_1_7_10) {
        jobject world = getWorld();
        if (world) {
            // World.getBlock(int, int, int) -> Block
            if (JniCache::mid_getBlockWorld) {
                jobject block = env->CallObjectMethod(world, JniCache::mid_getBlockWorld, x, y, z);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    block = nullptr;
                }
                if (block) {
                    // Block.getIdFromBlock(Block) -> int
                    if (JniCache::cls_Block && JniCache::mid_getIdFromBlock) {
                        blockId = env->CallStaticIntMethod(JniCache::cls_Block, JniCache::mid_getIdFromBlock, block);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            blockId = 0;
                        }
                    }
                }
            }
        }
    } else {
        jobject world = getWorld();
        if (world) {
            // Create BlockPos object
            if (JniCache::cls_BlockPos && JniCache::mid_blockPosInit) {
                jobject blockPos = env->NewObject(JniCache::cls_BlockPos, JniCache::mid_blockPosInit, x, y, z);
                if (env->ExceptionCheck()) {
                    env->ExceptionClear();
                    blockPos = nullptr;
                }
                if (blockPos && JniCache::mid_getBlockState) {
                    // world.getBlockState(BlockPos) -> IBlockState
                    jobject blockState = env->CallObjectMethod(world, JniCache::mid_getBlockState, blockPos);
                    if (env->ExceptionCheck()) {
                        env->ExceptionClear();
                        blockState = nullptr;
                    }
                    if (blockState && JniCache::mid_getBlock) {
                        // IBlockState.getBlock() -> Block
                        jobject block = env->CallObjectMethod(blockState, JniCache::mid_getBlock);
                        if (env->ExceptionCheck()) {
                            env->ExceptionClear();
                            block = nullptr;
                        }
                        if (block && JniCache::cls_Block && JniCache::mid_getIdFromBlock) {
                            // Block.getIdFromBlock(Block) -> int
                            blockId =
                                env->CallStaticIntMethod(JniCache::cls_Block, JniCache::mid_getIdFromBlock, block);
                            if (env->ExceptionCheck()) {
                                env->ExceptionClear();
                                blockId = 0;
                            }
                        }
                    }
                }
            }
        }
    }

    env->PopLocalFrame(nullptr);
    return blockId;
}

bool Minecraft::damageBlock(int x, int y, int z) {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env || !JniCache::fid_playerController)
        return false;

    if (env->PushLocalFrame(16) < 0)
        return false;

    jobject controller = env->GetObjectField(instance, JniCache::fid_playerController);
    if (!controller) {
        env->PopLocalFrame(nullptr);
        return false;
    }

    jclass controllerClass = env->GetObjectClass(controller);
    if (!controllerClass) {
        env->PopLocalFrame(nullptr);
        return false;
    }

    bool damaged = false;
    if (Java::Version == MinecraftVersion::MC_1_7_10) {
        jmethodID damageMethod = Java::GetMethodID(env, controllerClass, OBFUSCATE_STRING("onPlayerDamageBlock").c_str(),
                                                  OBFUSCATE_STRING("(IIII)Z").c_str());
        if (!damageMethod) {
            env->ExceptionClear();
            damageMethod = Java::GetMethodID(env, controllerClass, OBFUSCATE_STRING("func_78759_c").c_str(),
                                            OBFUSCATE_STRING("(IIII)Z").c_str());
        }
        if (!damageMethod && env->ExceptionCheck())
            env->ExceptionClear();
        if (damageMethod)
            damaged = env->CallBooleanMethod(controller, damageMethod, x, y, z, 1) == JNI_TRUE;
    } else if (JniCache::cls_BlockPos && JniCache::mid_blockPosInit) {
        jmethodID damageMethod = Java::GetMethodID(env, 
            controllerClass, OBFUSCATE_STRING("onPlayerDamageBlock").c_str(),
            OBFUSCATE_STRING("(Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;)Z").c_str());
        if (!damageMethod) {
            env->ExceptionClear();
            damageMethod = Java::GetMethodID(env, 
                controllerClass, OBFUSCATE_STRING("func_180512_c").c_str(),
                OBFUSCATE_STRING("(Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;)Z").c_str());
        }
        if (!damageMethod && env->ExceptionCheck())
            env->ExceptionClear();

        jclass facingClass = nullptr;
        if (damageMethod && Java::AssignClass(OBFUSCATE_STRING("net.minecraft.util.EnumFacing"), facingClass)) {
            jmethodID valuesMethod =
                Java::GetStaticMethodID(env, facingClass, OBFUSCATE_STRING("values").c_str(),
                                       OBFUSCATE_STRING("()[Lnet/minecraft/util/EnumFacing;").c_str());
            if (!valuesMethod && env->ExceptionCheck())
                env->ExceptionClear();
            jobjectArray values =
                valuesMethod ? static_cast<jobjectArray>(env->CallStaticObjectMethod(facingClass, valuesMethod))
                             : nullptr;
            jobject facingUp =
                values && env->GetArrayLength(values) > 1 ? env->GetObjectArrayElement(values, 1) : nullptr;
            jobject blockPos = env->NewObject(JniCache::cls_BlockPos, JniCache::mid_blockPosInit, x, y, z);
            if (blockPos && facingUp)
                damaged = env->CallBooleanMethod(controller, damageMethod, blockPos, facingUp) == JNI_TRUE;
        }
    }

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        damaged = false;
    }

    env->PopLocalFrame(nullptr);
    return damaged;
}

// =============================================================================
// damageTargetedBlock - Damage the crosshair block with vanilla's exact target
// =============================================================================

bool Minecraft::damageTargetedBlock() {
    JNIEnv* env = Java::Env();
    if (!isValid() || !env)
        return false;
    if (!JniCache::fid_playerController || !JniCache::fid_objectMouseOver || !JniCache::mid_getBlockPos ||
        !JniCache::cls_MovingObjectPosition)
        return false;

    if (env->PushLocalFrame(16) < 0)
        return false;

    bool damaged = false;
    do {
        jobject controller = env->GetObjectField(instance, JniCache::fid_playerController);
        if (!controller)
            break;

        jobject mouseOver = env->GetObjectField(instance, JniCache::fid_objectMouseOver);
        if (!mouseOver)
            break;

        // Block position exactly as the game sees it under the crosshair.
        jobject blockPos = env->CallObjectMethod(mouseOver, JniCache::mid_getBlockPos);
        if (!blockPos || env->ExceptionCheck()) {
            env->ExceptionClear();
            break;
        }

        // sideHit : EnumFacing — the exact face vanilla reports for this hit.
        // Using the real side keeps our STOP_DESTROY_BLOCK consistent with the
        // START the game already sent. SRG name fallback for non-MCP mappings.
        jfieldID fidSideHit = Java::GetFieldID(env, JniCache::cls_MovingObjectPosition, OBFUSCATE_STRING("sideHit").c_str(),
                                              OBFUSCATE_STRING("Lnet/minecraft/util/EnumFacing;").c_str());
        if (!fidSideHit) {
            env->ExceptionClear();
            fidSideHit = Java::GetFieldID(env, JniCache::cls_MovingObjectPosition, OBFUSCATE_STRING("field_178784_b").c_str(),
                                         OBFUSCATE_STRING("Lnet/minecraft/util/EnumFacing;").c_str());
        }
        if (!fidSideHit) {
            env->ExceptionClear();
            break;
        }
        jobject sideHit = env->GetObjectField(mouseOver, fidSideHit);
        if (!sideHit)
            break;

        jclass controllerClass = env->GetObjectClass(controller);
        if (!controllerClass)
            break;

        jmethodID damageMethod = Java::GetMethodID(env, 
            controllerClass, OBFUSCATE_STRING("onPlayerDamageBlock").c_str(),
            OBFUSCATE_STRING("(Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;)Z").c_str());
        if (!damageMethod) {
            env->ExceptionClear();
            damageMethod = Java::GetMethodID(env, 
                controllerClass, OBFUSCATE_STRING("func_180512_c").c_str(),
                OBFUSCATE_STRING("(Lnet/minecraft/util/BlockPos;Lnet/minecraft/util/EnumFacing;)Z").c_str());
        }
        if (!damageMethod) {
            env->ExceptionClear();
            break;
        }

        damaged = env->CallBooleanMethod(controller, damageMethod, blockPos, sideHit) == JNI_TRUE;
    } while (false);

    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        damaged = false;
    }

    env->PopLocalFrame(nullptr);
    return damaged;
}

// =============================================================================
// rayTraceBlocks - Cast a ray and find block hit position
// =============================================================================

Minecraft::RAYTRACE_BLOCKS_RESULT Minecraft::rayTraceBlocks(Vec3 from, Vec3 direction, Vec3& result,
                                                            bool stopOnLiquid) {
    result = {-1.5f, -1.5f, -1.5f};  // Default error value

    if (!isValid())
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

    JNIEnv* env = Java::Env();
    if (!env)
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

    if (env->PushLocalFrame(16) < 0)
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

    jobject world = getWorld();
    if (!world) {
        env->PopLocalFrame(nullptr);
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
    }

    // Create Vec3 objects for from and direction
    if (!JniCache::cls_Vec3 || !JniCache::mid_vec3Init) {
        env->PopLocalFrame(nullptr);
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
    }

    jobject fromObj =
        env->NewObject(JniCache::cls_Vec3, JniCache::mid_vec3Init, (double)from.x, (double)from.y, (double)from.z);
    if (!fromObj) {
        env->PopLocalFrame(nullptr);
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
    }

    jobject directionObj = env->NewObject(JniCache::cls_Vec3, JniCache::mid_vec3Init, (double)direction.x,
                                          (double)direction.y, (double)direction.z);
    if (!directionObj) {
        env->PopLocalFrame(nullptr);
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
    }

    // Call world.rayTraceBlocks
    jobject mop = nullptr;
    if (JniCache::mid_rayTraceBlocks) {
        mop = env->CallObjectMethod(world, JniCache::mid_rayTraceBlocks, fromObj, directionObj, stopOnLiquid);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            // rayTraceBlocks might be 2-arg version, try again
            mop = env->CallObjectMethod(world, JniCache::mid_rayTraceBlocks, fromObj, directionObj);
        }
    }

    // No hit
    if (!mop) {
        env->PopLocalFrame(nullptr);
        return RAYTRACE_BLOCKS_RESULT::NO_HIT;
    }

    // Extract block position from MovingObjectPosition
    bool gotPosition = false;

    // Method 1: 1.8.x - getBlockPos() -> BlockPos
    if (JniCache::mid_getBlockPos && JniCache::mid_getX && JniCache::mid_getY && JniCache::mid_getZ) {
        jobject blockPosObj = env->CallObjectMethod(mop, JniCache::mid_getBlockPos);
        if (blockPosObj) {
            result.x = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getX);
            result.y = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getY);
            result.z = (float)env->CallIntMethod(blockPosObj, JniCache::mid_getZ);
            gotPosition = true;
        }
    }

    // Method 2: 1.7.10 - direct blockX/blockY/blockZ fields
    if (!gotPosition && JniCache::fid_blockX && JniCache::fid_blockY && JniCache::fid_blockZ) {
        result.x = (float)env->GetIntField(mop, JniCache::fid_blockX);
        result.y = (float)env->GetIntField(mop, JniCache::fid_blockY);
        result.z = (float)env->GetIntField(mop, JniCache::fid_blockZ);
        gotPosition = true;
    }

    env->PopLocalFrame(nullptr);

    if (gotPosition) {
        return RAYTRACE_BLOCKS_RESULT::HIT;
    }

    return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
}
