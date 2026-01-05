// Player list and entity-related functions for Minecraft SDK
// Handles getPlayerList, canEntityBeSeen, getLocalPlayerTeamColor

#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../jni/jstring_util.h"
#include "../../util/logger.hpp"
#include "../../util/friends_manager.h"

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
// Helper: Detect if player is an NPC (bots, server entities, fake players)
// Based on Dope-Ghost detection patterns
// =============================================================================

static bool detectIsNPC(const std::string& name, const std::string& displayName, bool isInvisible)
{
    // Empty or very short names are suspicious
    if (name.empty() || name.size() <= 1) {
        return true;
    }
    
    // Name starts with § and is very short (just a color code)
    if ((name.size() == 2 || name.size() == 3) && 
        ((unsigned char)name[0] == 0xA7 || 
         (name.size() >= 2 && (unsigned char)name[0] == 0xC2 && (unsigned char)name[1] == 0xA7))) {
        return true;
    }
    
    // Contains [NPC] tag
    if (name.find("[NPC]") != std::string::npos || displayName.find("[NPC]") != std::string::npos) {
        return true;
    }
    
    // Ends with §r but doesn't start with § (weird formatting)
    if (name.size() >= 2) {
        bool startsWithColor = ((unsigned char)name[0] == 0xA7) || 
                               (name.size() >= 2 && (unsigned char)name[0] == 0xC2 && (unsigned char)name[1] == 0xA7);
        bool endsWithReset = (name.size() >= 2 && 
                              ((name.substr(name.size() - 2) == "\xA7r") || 
                               (name.size() >= 3 && name.substr(name.size() - 3) == "\xC2\xA7r")));
        if (!startsWithColor && endsWithReset) {
            return true;
        }
    }
    
    // Contains §c (red color) often used for NPC indicators on some servers
    // Comment out if causing issues: some servers use red for enemies
    // if (name.find("\xA7c") != std::string::npos || name.find("\xC2\xA7c") != std::string::npos) {
    //     return true;
    // }
    
    // Invisible players are often bots/NPCs (anti-bot measure)
    // This is aggressive - some legit players may be invisible (potions)
    // if (isInvisible) {
    //     return true;
    // }
    
    return false;
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

        // UUID - Stable player identity (CANNOT be modified by server plugins)
        // This is the ONLY reliable identifier on protected servers
        if (JniCache::mid_getUniqueID && JniCache::mid_uuidToString) {
            jobject uuidObj = env->CallObjectMethod(entity, JniCache::mid_getUniqueID);
            if (uuidObj) {
                jstring uuidStr = (jstring)env->CallObjectMethod(uuidObj, JniCache::mid_uuidToString);
                if (uuidStr) {
                    info.uuid = jstringToString(uuidStr);
                    env->DeleteLocalRef(uuidStr);
                }
                env->DeleteLocalRef(uuidObj);
            }
            env->ExceptionClear();
        }

        // Name - Use getName() directly from entity
        // NOTE: On protected servers, this may return a server-modified name
        // The UUID is the only reliable identifier for player matching
        if (JniCache::mid_getName) {
            jstring nameStr = (jstring)env->CallObjectMethod(entity, JniCache::mid_getName);
            if (nameStr) {
                info.name = jstringToString(nameStr);
                env->DeleteLocalRef(nameStr);
            }
        }
        if (info.name.empty()) info.name = "Unknown";

        // Display name for team color detection ONLY
        // We use getFormattedText to get the color codes, but NOT to override info.name
        if (mid_getDisplayName_cached) {
            jobject displayResult = env->CallObjectMethod(entity, mid_getDisplayName_cached);
            if (displayResult) {
                // Check type and extract display name
                jclass strCls = env->FindClass("java/lang/String");
                if (strCls && env->IsInstanceOf(displayResult, strCls)) {
                    info.displayName = jstringToString((jstring)displayResult);
                } else if (JniCache::cls_IChatComponent && 
                           env->IsInstanceOf(displayResult, JniCache::cls_IChatComponent)) {
                    // Get formatted text for team color (has color codes)
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

        // Parse team color from displayName (which has color codes)
        if (!info.displayName.empty()) {
            info.teamColor = parseTeamColor(info.displayName);
        }

        // NPC detection (bots, server entities, fake players)
        info.isNPC = detectIsNPC(info.name, info.displayName, info.isInvisible);

        // Health data
        if (JniCache::mid_getHealth) info.health = env->CallFloatMethod(entity, JniCache::mid_getHealth);
        if (JniCache::mid_getMaxHealth) info.maxHealth = env->CallFloatMethod(entity, JniCache::mid_getMaxHealth);
        if (JniCache::fid_hurtTime) info.hurtTime = env->GetIntField(entity, JniCache::fid_hurtTime);
        
        // Sneaking state
        if (JniCache::mid_isSneaking) {
            info.isSneaking = env->CallBooleanMethod(entity, JniCache::mid_isSneaking);
        }
        
        // Hitbox (bounding box)
        if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
            jobject aabb = env->GetObjectField(entity, JniCache::fid_boundingBox);
            if (aabb) {
                if (JniCache::fid_aabb_minX) info.hitboxMinX = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minX);
                if (JniCache::fid_aabb_minY) info.hitboxMinY = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minY);
                if (JniCache::fid_aabb_minZ) info.hitboxMinZ = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minZ);
                if (JniCache::fid_aabb_maxX) info.hitboxMaxX = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxX);
                if (JniCache::fid_aabb_maxY) info.hitboxMaxY = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxY);
                if (JniCache::fid_aabb_maxZ) info.hitboxMaxZ = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxZ);
                env->DeleteLocalRef(aabb);
            }
        }

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

// =============================================================================
// findPlayerByName - Find player in current world by name and get their UUID
// =============================================================================

PlayerInfo Minecraft::findPlayerByName(const std::string& searchName) {
    if (searchName.empty()) return PlayerInfo();
    
    // Get current player list
    auto players = getPlayerList();
    
    // Convert search name to lowercase for case-insensitive matching
    std::string searchLower = searchName;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(),
        [](unsigned char c) { return std::tolower(c); });
    
    // Search for matching player
    for (auto& p : players) {
        // Check name (lowercase comparison)
        std::string nameLower = p.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        
        if (nameLower == searchLower) {
            return p;
        }
        
        // Also check display name (stripped of color codes for comparison)
        std::string displayLower = FriendsManager::stripColorCodes(p.displayName);
        std::transform(displayLower.begin(), displayLower.end(), displayLower.begin(),
            [](unsigned char c) { return std::tolower(c); });
        
        if (displayLower == searchLower) {
            return p;
        }
    }
    
    return PlayerInfo();  // Not found
}
