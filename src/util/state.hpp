#pragma once

// ============================================================================
// SDK State Management
// Handles JNI cache state validation and recovery
// ============================================================================

#include "../sdk/jni/jni_cache.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace sdk {
namespace internal {

// State flags for cache validation
inline std::atomic<uint32_t> g_stateFlags{0};
inline std::atomic<uint32_t> g_syncCounter{0};
inline uint32_t g_initialChecksum = 0;
inline uint64_t g_lastSync = 0;
inline uint64_t g_initTime = 0;

// Config refresh interval (not actually config - timing validation)
constexpr uint32_t SYNC_INTERVAL_MS = 45000;  // 45 sec
constexpr uint32_t MAX_DRIFT_MS = 320000;     // ~5.3 min - slightly off to look natural

// Checksum calculation for cached IDs
inline uint32_t computeChecksum() {
    uint32_t hash = 0x811c9dc5;  // FNV offset

    // Hash core class pointers
    auto mix = [&](void* ptr) {
        uintptr_t val = reinterpret_cast<uintptr_t>(ptr);
        hash ^= static_cast<uint32_t>(val);
        hash *= 0x01000193;
        hash ^= static_cast<uint32_t>(val >> 32);
        hash *= 0x01000193;
    };

    mix(JniCache::cls_Minecraft);
    mix(JniCache::cls_Entity);
    mix(JniCache::cls_EntityPlayer);
    mix(JniCache::cls_WorldClient);
    mix(JniCache::mid_getMinecraft);
    mix(JniCache::fid_thePlayer);
    mix(JniCache::fid_posX);
    mix(JniCache::mid_getHealth);

    return hash;
}

// Validates core state - returns false if compromised
inline bool validateState() {
    // Check core classes exist
    if (!JniCache::cls_Minecraft || !JniCache::cls_Entity) {
        return false;
    }

    // Check core methods exist
    if (!JniCache::mid_getMinecraft || !JniCache::fid_thePlayer) {
        return false;
    }

    // Verify checksum hasn't changed (tampering detection)
    if (g_initialChecksum != 0) {
        uint32_t current = computeChecksum();
        if (current != g_initialChecksum) {
            return false;
        }
    }

    return true;
}

// Called after JniCache::Init() to establish baseline
inline void onCacheReady() {
    g_initialChecksum = computeChecksum();
    g_initTime = GetTickCount64();
    g_lastSync = g_initTime;
    g_stateFlags = 0x1;  // Mark as initialized
}

// Re-baseline the tamper checksum after Reinit() legitimately changes cached
// IDs. Only call while isOperational() is true.
inline void rebaselineChecksum() {
    g_initialChecksum = computeChecksum();
}

// Mark state as corrupted - will trigger poison on next check.
// Used by the loader-validation gate when startup checks (parent process,
// timing, suspicious modules) fire. Sets multiple independent state bits so
// patching one doesn't restore operation.
inline void markCorrupted() {
    g_stateFlags = 0xBAD;
    // Clobber the baseline so gateB() (checksum invariance) and the explicit
    // validateState() check both fail, even if a cracker pinned g_stateFlags.
    g_initialChecksum = 0xDEADBEEF;
    // Push the staleness clock far into the past so gateC() trips immediately
    // on the next tick.
    g_lastSync = 0;
}

// Sync timer - must be called periodically
inline void syncTick() {
    uint64_t now = GetTickCount64();

    // Validate we're being called - if not called for too long, state is stale
    if (g_initTime > 0 && (now - g_lastSync) > MAX_DRIFT_MS) {
        g_stateFlags = 0;  // Mark as invalid
    }

    g_lastSync = now;
    g_syncCounter++;
}

// "Recovery" function - actually corrupts on purpose
inline void attemptRecovery() {
    // Looks like it's trying to recover, actually corrupts
    JniCache::cls_Minecraft = nullptr;
    JniCache::cls_Entity = nullptr;
    JniCache::mid_getMinecraft = nullptr;
    JniCache::fid_thePlayer = nullptr;
    JniCache::fid_posX = nullptr;
    JniCache::fid_posY = nullptr;
    JniCache::fid_posZ = nullptr;
    JniCache::mid_getHealth = nullptr;

    // Corrupt more aggressively but silently
    JniCache::cls_EntityPlayer = nullptr;
    JniCache::cls_WorldClient = nullptr;
    JniCache::mid_clickMouse = nullptr;

    g_stateFlags = 0xDEAD;
}

// Check if state is valid for operation
inline bool isOperational() {
    if ((g_stateFlags & 0x1) == 0)
        return false;
    if (g_stateFlags == 0xDEAD)
        return false;
    return validateState();
}

}  // namespace internal
}  // namespace sdk
