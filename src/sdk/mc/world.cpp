// World-related functions for Minecraft SDK
// Handles block queries and raytrace operations

#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../../util/logger.hpp"

// =============================================================================
// getBlockId - Get block ID at world coordinates
// =============================================================================

int Minecraft::getBlockId(int x, int y, int z)
{
    if (!isValid()) return 0;
    
    // 1.7.10: World.getBlock(int, int, int) -> Block
    if (Java::Version == MinecraftVersion::MC_1_7_10) {
        JNIEnv* env = Java::GetEnv();
        if (!env) return 0;

        jobject world = getWorld();
        if (!world) return 0;

        int blockId = 0;

        // World.getBlock(int, int, int) -> Block
        if (JniCache::mid_getBlockWorld) {
            jobject block = env->CallObjectMethod(world, JniCache::mid_getBlockWorld, x, y, z);
            if (block) {
                // Block.getIdFromBlock(Block) -> int
                if (JniCache::cls_Block && JniCache::mid_getIdFromBlock) {
                    blockId = env->CallStaticIntMethod(JniCache::cls_Block, JniCache::mid_getIdFromBlock, block);
                }
                env->DeleteLocalRef(block);
            }
        }

        env->DeleteLocalRef(world);
        return blockId;
    }

    JNIEnv* env = Java::GetEnv();
    if (!env) return 0;

    jobject world = getWorld();
    if (!world) return 0;

    int blockId = 0;

    // Create BlockPos object
    if (!JniCache::cls_BlockPos || !JniCache::mid_blockPosInit) {
        env->DeleteLocalRef(world);
        return 0;
    }

    jobject blockPos = env->NewObject(JniCache::cls_BlockPos, JniCache::mid_blockPosInit, x, y, z);
    if (!blockPos) {
        env->DeleteLocalRef(world);
        return 0;
    }

    // world.getBlockState(BlockPos) -> IBlockState
    if (!JniCache::mid_getBlockState) {
        env->DeleteLocalRef(blockPos);
        env->DeleteLocalRef(world);
        return 0;
    }

    jobject blockState = env->CallObjectMethod(world, JniCache::mid_getBlockState, blockPos);
    if (!blockState) {
        env->DeleteLocalRef(blockPos);
        env->DeleteLocalRef(world);
        return 0;
    }

    // IBlockState.getBlock() -> Block
    if (!JniCache::mid_getBlock) {
        env->DeleteLocalRef(blockState);
        env->DeleteLocalRef(blockPos);
        env->DeleteLocalRef(world);
        return 0;
    }

    jobject block = env->CallObjectMethod(blockState, JniCache::mid_getBlock);
    if (!block) {
        env->DeleteLocalRef(blockState);
        env->DeleteLocalRef(blockPos);
        env->DeleteLocalRef(world);
        return 0;
    }

    // Block.getIdFromBlock(Block) -> int
    if (JniCache::cls_Block && JniCache::mid_getIdFromBlock) {
        blockId = env->CallStaticIntMethod(JniCache::cls_Block, JniCache::mid_getIdFromBlock, block);
    }

    // Cleanup
    env->DeleteLocalRef(block);
    env->DeleteLocalRef(blockState);
    env->DeleteLocalRef(blockPos);
    env->DeleteLocalRef(world);

    return blockId;
}

// =============================================================================
// rayTraceBlocks - Cast a ray and find block hit position
// =============================================================================

Minecraft::RAYTRACE_BLOCKS_RESULT Minecraft::rayTraceBlocks(Vec3 from, Vec3 direction, Vec3& result, bool stopOnLiquid)
{
    result = { -1.5f, -1.5f, -1.5f }; // Default error value
    
    if (!isValid()) return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

    JNIEnv* env = Java::GetEnv();
    if (!env) return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

    jobject world = getWorld();
    if (!world) return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;

    // Create Vec3 objects for from and direction
    if (!JniCache::cls_Vec3 || !JniCache::mid_vec3Init) {
        env->DeleteLocalRef(world);
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
    }

    jobject fromObj = env->NewObject(JniCache::cls_Vec3, JniCache::mid_vec3Init, 
        (double)from.x, (double)from.y, (double)from.z);
    if (!fromObj) {
        env->DeleteLocalRef(world);
        return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
    }

    jobject directionObj = env->NewObject(JniCache::cls_Vec3, JniCache::mid_vec3Init, 
        (double)direction.x, (double)direction.y, (double)direction.z);
    if (!directionObj) {
        env->DeleteLocalRef(fromObj);
        env->DeleteLocalRef(world);
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

    env->DeleteLocalRef(directionObj);
    env->DeleteLocalRef(fromObj);
    env->DeleteLocalRef(world);

    // No hit
    if (!mop) {
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
            env->DeleteLocalRef(blockPosObj);
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

    env->DeleteLocalRef(mop);

    if (gotPosition) {
        return RAYTRACE_BLOCKS_RESULT::HIT;
    }
    
    return RAYTRACE_BLOCKS_RESULT::RAYTRACE_ERROR;
}
