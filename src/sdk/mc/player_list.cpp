// Player list and entity-related functions for Minecraft SDK
// Handles getPlayerList, canEntityBeSeen, getLocalPlayerTeamColor

#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../jni/jstring_util.h"
#include "../../util/logger.hpp"

#include <cctype>
#include <algorithm>

// =============================================================================
// Helper: rate-limited logging
// =============================================================================

static bool every_ms(int ms)
{
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last >= (ULONGLONG)ms) { last = now; return true; }
    return false;
}

static void jni_log_if_exception(const char* where)
{
    JNIEnv* env = Java::GetEnv();
    if (!env) return;
    
    if (env->ExceptionCheck()) {
        if (every_ms(1000)) {
            log_warn("[JNI] Exception in %s", where);
        }
        env->ExceptionClear();
    }
}

// =============================================================================
// Helper: Parse team color from display name
// =============================================================================

static int parseTeamColor(const std::string& displayName)
{
    for (size_t j = 0; j < displayName.size(); j++) {
        unsigned char c = displayName[j];
        
        // Check for UTF-8 Ã‚Â§ (0xC2 0xA7)
        if (c == 0xC2 && j + 2 < displayName.size() && 
            (unsigned char)displayName[j + 1] == 0xA7) {
            char code = std::tolower(displayName[j + 2]);
            if (code >= '0' && code <= '9') {
                return code - '0';
            } else if (code >= 'a' && code <= 'f') {
                return 10 + (code - 'a');
            }
            // Format code (k-o, r), skip and continue
            j += 2;
            continue;
        }
        
        // Check for raw Ã‚Â§ byte (0xA7)
        if (c == 0xA7 && j + 1 < displayName.size()) {
            char code = std::tolower(displayName[j + 1]);
            if (code >= '0' && code <= '9') {
                return code - '0';
            } else if (code >= 'a' && code <= 'f') {
                return 10 + (code - 'a');
            }
            j += 1;
            continue;
        }
    }
    return -1;
}

// =============================================================================
// getPlayerList - Get all players in the world (excluding local player)
// =============================================================================

std::vector<PlayerInfo> Minecraft::getPlayerList()
{
    std::vector<PlayerInfo> result;
    if (!isValid()) return result;

    JNIEnv* env = Java::GetEnv();
    if (!env) return result;

    jobject world = getWorld();
    if (!world) return result;

    // Get playerEntities list from world
    if (!JniCache::fid_playerEntities) {
        env->DeleteLocalRef(world);
        return result;
    }

    jobject playerList = env->GetObjectField(world, JniCache::fid_playerEntities);
    env->DeleteLocalRef(world);

    if (!playerList) return result;

    // Get List methods
    if (!JniCache::cls_List || !JniCache::mid_listSize || !JniCache::mid_listGet) {
        env->DeleteLocalRef(playerList);
        return result;
    }

    int size = env->CallIntMethod(playerList, JniCache::mid_listSize);
    if (size <= 0) {
        env->DeleteLocalRef(playerList);
        return result;
    }

    result.reserve(size);

    jobject localPlayer = getPlayer();

    // Static cache for display name methods (one-time init)
    static jmethodID mid_getDisplayName_cached = nullptr;
    static jmethodID mid_getUnformattedText_cached = nullptr;
    static jmethodID mid_getFormattedText_cached = nullptr;
    static bool displayNameInit = false;

    if (!displayNameInit && JniCache::cls_EntityPlayer) {
        mid_getDisplayName_cached = JniCache::mid_getDisplayName;
        mid_getUnformattedText_cached = JniCache::mid_getUnformattedText;
        mid_getFormattedText_cached = JniCache::mid_getFormattedText;
        displayNameInit = true;
    }

    for (int i = 0; i < size; i++) {
        jobject entity = env->CallObjectMethod(playerList, JniCache::mid_listGet, i);
        if (!entity) continue;

        // Skip local player
        if (localPlayer && env->IsSameObject(entity, localPlayer)) {
            env->DeleteLocalRef(entity);
            continue;
        }

        PlayerInfo info;
        info.instance = entity; // Caller must delete later

        // Position data
        if (JniCache::fid_posX) info.posX = env->GetDoubleField(entity, JniCache::fid_posX);
        if (JniCache::fid_posY) info.posY = env->GetDoubleField(entity, JniCache::fid_posY);
        if (JniCache::fid_posZ) info.posZ = env->GetDoubleField(entity, JniCache::fid_posZ);
        if (JniCache::fid_lastTickPosX) info.lastPosX = env->GetDoubleField(entity, JniCache::fid_lastTickPosX);
        if (JniCache::fid_lastTickPosY) info.lastPosY = env->GetDoubleField(entity, JniCache::fid_lastTickPosY);
        if (JniCache::fid_lastTickPosZ) info.lastPosZ = env->GetDoubleField(entity, JniCache::fid_lastTickPosZ);

        // Visibility
        if (JniCache::mid_isInvisible) {
            info.isInvisible = env->CallBooleanMethod(entity, JniCache::mid_isInvisible);
        }

        // Name
        if (JniCache::mid_getName) {
            jstring nameStr = (jstring)env->CallObjectMethod(entity, JniCache::mid_getName);
            if (nameStr) {
                info.name = jstringToString(nameStr);
                env->DeleteLocalRef(nameStr);
            }
        }
        if (info.name.empty()) info.name = "Unknown";

        // Display name for team color detection
        std::string realName;
        if (mid_getDisplayName_cached) {
            jobject displayResult = env->CallObjectMethod(entity, mid_getDisplayName_cached);
            if (displayResult) {
                // Check type and extract name
                jclass strCls = env->FindClass("java/lang/String");
                if (strCls && env->IsInstanceOf(displayResult, strCls)) {
                    realName = jstringToString((jstring)displayResult);
                    info.displayName = realName;
                } else if (JniCache::cls_IChatComponent && 
                           env->IsInstanceOf(displayResult, JniCache::cls_IChatComponent)) {
                    // Get unformatted text for real name
                    if (mid_getUnformattedText_cached) {
                        jstring unformatted = (jstring)env->CallObjectMethod(displayResult, mid_getUnformattedText_cached);
                        if (unformatted) {
                            realName = jstringToString(unformatted);
                            env->DeleteLocalRef(unformatted);
                        }
                    }
                    // Get formatted text for team color
                    if (mid_getFormattedText_cached) {
                        jstring formatted = (jstring)env->CallObjectMethod(displayResult, mid_getFormattedText_cached);
                        if (formatted) {
                            info.displayName = jstringToString(formatted);
                            env->DeleteLocalRef(formatted);
                        }
                    }
                }
                if (strCls) env->DeleteLocalRef(strCls);
                env->DeleteLocalRef(displayResult);
            }
            env->ExceptionClear();
        }

        // Use real name if available
        if (!realName.empty()) {
            info.name = realName;
        }

        // Parse team color
        if (!info.displayName.empty()) {
            info.teamColor = parseTeamColor(info.displayName);
        }

        // Health data
        if (JniCache::mid_getHealth) info.health = env->CallFloatMethod(entity, JniCache::mid_getHealth);
        if (JniCache::mid_getMaxHealth) info.maxHealth = env->CallFloatMethod(entity, JniCache::mid_getMaxHealth);
        if (JniCache::fid_hurtTime) info.hurtTime = env->GetIntField(entity, JniCache::fid_hurtTime);

        result.push_back(std::move(info));
    }

    if (localPlayer) env->DeleteLocalRef(localPlayer);
    env->DeleteLocalRef(playerList);

    return result;
}

// =============================================================================
// canEntityBeSeen - Check if local player can see target entity
// =============================================================================

bool Minecraft::canEntityBeSeen(jobject target)
{
    if (!isValid() || !target) return false;

    JNIEnv* env = Java::GetEnv();
    if (!env) return false;

    jobject localPlayer = getPlayer();
    if (!localPlayer) return false;

    bool result = false;
    if (JniCache::mid_canEntityBeSeen) {
        result = env->CallBooleanMethod(localPlayer, JniCache::mid_canEntityBeSeen, target);
    }

    env->DeleteLocalRef(localPlayer);
    return result;
}

// =============================================================================
// getLocalPlayerTeamColor - Get team color from local player's display name
// =============================================================================

int Minecraft::getLocalPlayerTeamColor()
{
    if (!isValid()) return -1;

    JNIEnv* env = Java::GetEnv();
    if (!env) return -1;

    jobject localPlayer = getPlayer();
    if (!localPlayer) return -1;

    int teamColor = -1;
    std::string displayName;

    if (JniCache::mid_getDisplayName) {
        jobject displayResult = env->CallObjectMethod(localPlayer, JniCache::mid_getDisplayName);
        if (displayResult) {
            jclass strCls = env->FindClass("java/lang/String");
            if (strCls && env->IsInstanceOf(displayResult, strCls)) {
                displayName = jstringToString((jstring)displayResult);
            } else if (JniCache::cls_IChatComponent && 
                       env->IsInstanceOf(displayResult, JniCache::cls_IChatComponent)) {
                if (JniCache::mid_getFormattedText) {
                    jstring formatted = (jstring)env->CallObjectMethod(displayResult, JniCache::mid_getFormattedText);
                    if (formatted) {
                        displayName = jstringToString(formatted);
                        env->DeleteLocalRef(formatted);
                    }
                }
            }
            if (strCls) env->DeleteLocalRef(strCls);
            env->DeleteLocalRef(displayResult);
        }
        env->ExceptionClear();
    }

    if (!displayName.empty()) {
        teamColor = parseTeamColor(displayName);
    }

    env->DeleteLocalRef(localPlayer);
    return teamColor;
}
