// Player list and entity-related functions for Minecraft SDK
// Handles getPlayerList, canEntityBeSeen, getLocalPlayerTeamColor

#include "../../util/aes_string.hpp"
#include "../../util/logger.hpp"
#include "../../util/team/friends_manager.h"
#include "../core/minecraft.h"
#include "../jni/jni_cache.h"
#include "../jni/jstring_util.h"
#include "../player/effect_tracker.h"

#include <algorithm>
#include <cctype>

// =============================================================================
// Helper: read an enchantment level from an ItemStack (0 if absent).
// Reused for Protection (id 0) on armor and Sharpness (id 16) on weapons.
// Method/field IDs are resolved once and cached -- this runs on the hot
// per-player path (every ~4ms), so per-call GetMethodID would be wasteful.
// =============================================================================

static int readEnchantLevel(JNIEnv* env, jobject itemStack, int wantEnchantId) {
    if (!env || !itemStack)
        return 0;

    static bool s_init = false;
    static jclass s_nbtCompoundCls = nullptr;
    static jmethodID s_getShort = nullptr;
    static jmethodID s_getInteger = nullptr;
    static jmethodID s_getEnchantTagList = nullptr;
    static jmethodID s_tagCount = nullptr;
    static jmethodID s_getCompoundAt = nullptr;
    static jstring s_strId = nullptr;
    static jstring s_strLvl = nullptr;

    if (!s_init) {
        s_init = true;
        jclass cmp = nullptr;
        Java::AssignClass(OBFUSCATE_STRING("net.minecraft.nbt.NBTTagCompound"), cmp);
        if (cmp) {
            s_nbtCompoundCls = (jclass)env->NewGlobalRef(cmp);
            s_getShort = env->GetMethodID(cmp, OBFUSCATE_STRING("getShort").c_str(),
                                          OBFUSCATE_STRING("(Ljava/lang/String;)S").c_str());
            if (!s_getShort) {
                env->ExceptionClear();
                s_getShort = env->GetMethodID(cmp, OBFUSCATE_STRING("func_74765_d").c_str(),
                                              OBFUSCATE_STRING("(Ljava/lang/String;)S").c_str());
                if (!s_getShort) {
                    env->ExceptionClear();
                }
            }
            s_getInteger = env->GetMethodID(cmp, OBFUSCATE_STRING("getInteger").c_str(),
                                            OBFUSCATE_STRING("(Ljava/lang/String;)I").c_str());
            if (!s_getInteger) {
                env->ExceptionClear();
                s_getInteger = env->GetMethodID(cmp, OBFUSCATE_STRING("func_74762_e").c_str(),
                                                OBFUSCATE_STRING("(Ljava/lang/String;)I").c_str());
                if (!s_getInteger) {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(cmp);
        }
        jclass stackCls = env->GetObjectClass(itemStack);
        if (stackCls) {
            s_getEnchantTagList =
                env->GetMethodID(stackCls, OBFUSCATE_STRING("getEnchantmentTagList").c_str(),
                                 OBFUSCATE_STRING("()Lnet/minecraft/nbt/NBTTagList;").c_str());
            if (!s_getEnchantTagList) {
                env->ExceptionClear();
                s_getEnchantTagList =
                    env->GetMethodID(stackCls, OBFUSCATE_STRING("func_77986_q").c_str(),
                                     OBFUSCATE_STRING("()Lnet/minecraft/nbt/NBTTagList;").c_str());
                if (!s_getEnchantTagList) {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(stackCls);
        }
        jclass tlc = nullptr;
        Java::AssignClass(OBFUSCATE_STRING("net.minecraft.nbt.NBTTagList"), tlc);
        if (tlc) {
            s_tagCount = env->GetMethodID(tlc, OBFUSCATE_STRING("tagCount").c_str(), OBFUSCATE_STRING("()I").c_str());
            if (!s_tagCount) {
                env->ExceptionClear();
                s_tagCount = env->GetMethodID(tlc, OBFUSCATE_STRING("func_74745_c").c_str(), OBFUSCATE_STRING("()I").c_str());
                if (!s_tagCount) {
                    env->ExceptionClear();
                }
            }
            s_getCompoundAt = env->GetMethodID(tlc, OBFUSCATE_STRING("getCompoundTagAt").c_str(),
                                               OBFUSCATE_STRING("(I)Lnet/minecraft/nbt/NBTTagCompound;").c_str());
            if (!s_getCompoundAt) {
                env->ExceptionClear();
                s_getCompoundAt = env->GetMethodID(tlc, OBFUSCATE_STRING("func_150305_b").c_str(),
                                                   OBFUSCATE_STRING("(I)Lnet/minecraft/nbt/NBTTagCompound;").c_str());
                if (!s_getCompoundAt) {
                    env->ExceptionClear();
                }
            }
            env->DeleteLocalRef(tlc);
        }
        jstring lid = env->NewStringUTF(OBFUSCATE_STRING("id").c_str());
        jstring llvl = env->NewStringUTF(OBFUSCATE_STRING("lvl").c_str());
        s_strId = (jstring)env->NewGlobalRef(lid);
        s_strLvl = (jstring)env->NewGlobalRef(llvl);
        env->DeleteLocalRef(lid);
        env->DeleteLocalRef(llvl);
    }

    if (!s_getEnchantTagList || !s_getShort || !s_tagCount || !s_getCompoundAt || !s_strId || !s_strLvl)
        return 0;

    int level = 0;
    jobject tagList = env->CallObjectMethod(itemStack, s_getEnchantTagList);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        return 0;
    }
    if (tagList) {
        int count = env->CallIntMethod(tagList, s_tagCount);
        bool found = false;
        for (int e = 0; e < count; e++) {
            jobject compound = env->CallObjectMethod(tagList, s_getCompoundAt, e);
            if (compound) {
                int eid = 0;
                if (s_getShort) {
                    eid = env->CallShortMethod(compound, s_getShort, s_strId);
                }
                if (eid == 0 && s_getInteger) {
                    eid = env->CallIntMethod(compound, s_getInteger, s_strId);
                }

                if (eid == wantEnchantId) {
                    if (s_getShort) {
                        level = env->CallShortMethod(compound, s_getShort, s_strLvl);
                    }
                    if (level == 0 && s_getInteger) {
                        level = env->CallIntMethod(compound, s_getInteger, s_strLvl);
                    }
                    found = true;
                    env->DeleteLocalRef(compound);
                    break;
                }
                env->DeleteLocalRef(compound);
            }
        }

        // Smart estimation: if the item is enchanted (has tagList) but the server obfuscates the levels,
        // fall back to estimating Protection 2 / Sharpness 2 (standard for HCF/competitive).
        if (!found) {
            level = 2;
        }

        env->DeleteLocalRef(tagList);
    }
    return level;
}

// Vanilla 1.8 sword/axe item IDs -- only these are treated as "weapons" for the
// held-item readout (so blocks/tools in hand don't clutter the nametag).
static bool isWeaponId(int id) {
    switch (id) {
        case 268: case 272: case 267: case 276: case 283:  // swords: wood/stone/iron/diamond/gold
        case 271: case 275: case 258: case 279: case 286:  // axes:   wood/stone/iron/diamond/gold
            return true;
        default:
            return false;
    }
}

// =============================================================================
// Helper: rate-limited logging
// =============================================================================

static bool every_ms(int ms) {
    static ULONGLONG last = 0;
    ULONGLONG now = GetTickCount64();
    if (now - last >= (ULONGLONG)ms) {
        last = now;
        return true;
    }
    return false;
}

static void jni_log_if_exception(const char* where) {
    JNIEnv* env = Java::Env();
    if (!env)
        return;

    if (env->ExceptionCheck()) {
        if (every_ms(1000)) {
            log_warn("[JNI] Exception in %s", where);
        }
        env->ExceptionClear();
    }
}

// =============================================================================
// Helper: Reflection fallback for EntityLivingBase active potion effects map
// (for environments where we can't resolve activePotionsMap/getActivePotionEffects by name)
// =============================================================================

struct ReflectPotions {
    bool inited = false;
    jclass cls_Class = nullptr;
    jclass cls_Field = nullptr;
    jclass cls_AccessibleObject = nullptr;
    jmethodID mid_getDeclaredFields = nullptr;   // Class.getDeclaredFields() -> Field[]
    jmethodID mid_fieldGet = nullptr;            // Field.get(Object) -> Object
    jmethodID mid_setAccessible = nullptr;       // AccessibleObject.setAccessible(boolean)
    jobject cachedField = nullptr;               // GlobalRef(Field)
    ULONGLONG lastTryMs = 0;
};

static ReflectPotions g_refPotions;

static bool init_reflect_potions(JNIEnv* env) {
    if (g_refPotions.inited)
        return g_refPotions.mid_getDeclaredFields && g_refPotions.mid_fieldGet && g_refPotions.mid_setAccessible;

    g_refPotions.inited = true;

    jclass localClass = env->FindClass("java/lang/Class");
    jclass localField = env->FindClass("java/lang/reflect/Field");
    jclass localAccessible = env->FindClass("java/lang/reflect/AccessibleObject");
    if (!localClass || !localField || !localAccessible) {
        env->ExceptionClear();
        return false;
    }

    g_refPotions.cls_Class = (jclass)env->NewGlobalRef(localClass);
    g_refPotions.cls_Field = (jclass)env->NewGlobalRef(localField);
    g_refPotions.cls_AccessibleObject = (jclass)env->NewGlobalRef(localAccessible);
    env->DeleteLocalRef(localClass);
    env->DeleteLocalRef(localField);
    env->DeleteLocalRef(localAccessible);

    g_refPotions.mid_getDeclaredFields = env->GetMethodID(g_refPotions.cls_Class, "getDeclaredFields", "()[Ljava/lang/reflect/Field;");
    g_refPotions.mid_fieldGet = env->GetMethodID(g_refPotions.cls_Field, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
    g_refPotions.mid_setAccessible = env->GetMethodID(g_refPotions.cls_AccessibleObject, "setAccessible", "(Z)V");
    env->ExceptionClear();

    return g_refPotions.mid_getDeclaredFields && g_refPotions.mid_fieldGet && g_refPotions.mid_setAccessible;
}

static jobject try_get_active_potions_map_reflect(JNIEnv* env, jobject entity) {
    if (!env || !entity)
        return nullptr;
    if (!JniCache::mid_mapValues || !JniCache::mid_collectionToArray || !JniCache::cls_PotionEffect)
        return nullptr;
    if (!init_reflect_potions(env))
        return nullptr;

    if (g_refPotions.cachedField) {
        jobject mapObj = env->CallObjectMethod(g_refPotions.cachedField, g_refPotions.mid_fieldGet, entity);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return nullptr;
        }
        return mapObj;  // local ref (or nullptr)
    }

    ULONGLONG nowMs = GetTickCount64();
    if (nowMs - g_refPotions.lastTryMs < 5000)
        return nullptr;
    g_refPotions.lastTryMs = nowMs;

    jclass entityCls = env->GetObjectClass(entity);
    if (!entityCls) {
        env->ExceptionClear();
        return nullptr;
    }

    jobjectArray fields = (jobjectArray)env->CallObjectMethod(entityCls, g_refPotions.mid_getDeclaredFields);
    env->DeleteLocalRef(entityCls);
    if (!fields || env->ExceptionCheck()) {
        env->ExceptionClear();
        return nullptr;
    }

    int len = env->GetArrayLength(fields);
    for (int i = 0; i < len; i++) {
        jobject f = env->GetObjectArrayElement(fields, i);
        if (!f)
            continue;

        env->CallVoidMethod(f, g_refPotions.mid_setAccessible, JNI_TRUE);
        env->ExceptionClear();

        jobject mapObj = env->CallObjectMethod(f, g_refPotions.mid_fieldGet, entity);
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(f);
            continue;
        }
        if (!mapObj) {
            env->DeleteLocalRef(f);
            continue;
        }

        // Heuristic: treat it as the active potion map if its values() contains PotionEffect instances.
        jobject values = env->CallObjectMethod(mapObj, JniCache::mid_mapValues);
        if (!values || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(mapObj);
            env->DeleteLocalRef(f);
            continue;
        }
        jobjectArray arr = (jobjectArray)env->CallObjectMethod(values, JniCache::mid_collectionToArray);
        env->DeleteLocalRef(values);
        if (!arr || env->ExceptionCheck()) {
            env->ExceptionClear();
            env->DeleteLocalRef(mapObj);
            env->DeleteLocalRef(f);
            continue;
        }

        int alen = env->GetArrayLength(arr);
        jobject first = (alen > 0) ? env->GetObjectArrayElement(arr, 0) : nullptr;
        bool ok = (first && env->IsInstanceOf(first, JniCache::cls_PotionEffect));
        if (first) env->DeleteLocalRef(first);
        env->DeleteLocalRef(arr);

        if (ok) {
            g_refPotions.cachedField = env->NewGlobalRef(f);
            env->DeleteLocalRef(f);
            // Keep mapObj for caller
            env->DeleteLocalRef(fields);
            return mapObj;
        }

        env->DeleteLocalRef(mapObj);
        env->DeleteLocalRef(f);
    }

    env->DeleteLocalRef(fields);
    return nullptr;
}

// =============================================================================
// Helper: Parse team color from display name
// =============================================================================

static int parseTeamColor(const std::string& displayName) {
    for (size_t j = 0; j < displayName.size(); j++) {
        unsigned char c = displayName[j];

        // Check for UTF-8 Ã‚Â§ (0xC2 0xA7)
        if (c == 0xC2 && j + 2 < displayName.size() && (unsigned char)displayName[j + 1] == 0xA7) {
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

static bool detectIsNPC(const std::string& name, const std::string& displayName, bool isInvisible) {
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
        bool endsWithReset = (name.size() >= 2 && ((name.substr(name.size() - 2) == "\xA7r") ||
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

std::vector<PlayerInfo> Minecraft::getPlayerList() {
    std::vector<PlayerInfo> result;
    if (!isValid())
        return result;

    JNIEnv* env = Java::Env();
    if (!env)
        return result;

    // CRITICAL: Wrap entire function in a local frame to prevent JNI ref table overflow.
    // getPlayerList() allocates dozens of local refs per player per call (every 4ms).
    if (env->PushLocalFrame(32) < 0)
        return result;

    jobject world = getWorld();
    if (!world) {
        env->PopLocalFrame(nullptr);
        return result;
    }

    // Get playerEntities list from world
    if (!JniCache::fid_playerEntities) {
        env->PopLocalFrame(nullptr);
        return result;
    }

    jobject playerList = env->GetObjectField(world, JniCache::fid_playerEntities);

    if (!playerList) {
        env->PopLocalFrame(nullptr);
        return result;
    }

    // Get List methods
    if (!JniCache::cls_List || !JniCache::mid_listSize || !JniCache::mid_listGet) {
        env->PopLocalFrame(nullptr);
        return result;
    }

    int size = env->CallIntMethod(playerList, JniCache::mid_listSize);
    if (size <= 0) {
        env->PopLocalFrame(nullptr);
        return result;
    }

    result.reserve(size);

    jobject localPlayer = getPlayer();

    // Static cache for display name methods (one-time init)
    static jmethodID mid_getDisplayName_cached = nullptr;
    static jmethodID mid_getUnformattedText_cached = nullptr;
    static jmethodID mid_getFormattedText_cached = nullptr;
    static jmethodID mid_isOnSameTeam_cached = nullptr;
    static bool displayNameInit = false;
    static bool sameTeamInit = false;

    if (!displayNameInit && JniCache::cls_EntityPlayer) {
        mid_getDisplayName_cached = JniCache::mid_getDisplayName;
        mid_getUnformattedText_cached = JniCache::mid_getUnformattedText;
        mid_getFormattedText_cached = JniCache::mid_getFormattedText;
        displayNameInit = true;
    }
    if (!sameTeamInit && localPlayer) {
        jclass localPlayerClass = env->GetObjectClass(localPlayer);
        if (localPlayerClass) {
            mid_isOnSameTeam_cached =
                env->GetMethodID(localPlayerClass, "isOnSameTeam", "(Lnet/minecraft/entity/EntityLivingBase;)Z");
            if (env->ExceptionCheck())
                env->ExceptionClear();
            env->DeleteLocalRef(localPlayerClass);
        }
        sameTeamInit = true;
    }

    for (int i = 0; i < size; i++) {
        // Each player iteration gets its own sub-frame: UUID, displayName, armor, potions, etc.
        // = ~60-100 local refs per player; this ensures they're all freed after each iteration.
        if (env->PushLocalFrame(64) < 0)
            continue;

        jobject entity = env->CallObjectMethod(playerList, JniCache::mid_listGet, i);
        if (!entity) {
            env->PopLocalFrame(nullptr);
            continue;
        }

        // Only track real players; drop fakes/armor stands early
        bool isPlayerClass = JniCache::cls_EntityPlayer && env->IsInstanceOf(entity, JniCache::cls_EntityPlayer);
        bool isArmorStand = JniCache::cls_EntityArmorStand && env->IsInstanceOf(entity, JniCache::cls_EntityArmorStand);
        if (!isPlayerClass || isArmorStand) {
            env->PopLocalFrame(nullptr);
            continue;
        }

        // Skip local player
        if (localPlayer && env->IsSameObject(entity, localPlayer)) {
            env->PopLocalFrame(nullptr);
            continue;
        }

        PlayerInfo info;
        // Promote entity to GlobalRef so it survives the PopLocalFrame at end of this iteration.
        // Callers are responsible for calling env->DeleteGlobalRef(p.instance) when done.
        info.instance = env->NewGlobalRef(entity);
        if (localPlayer && mid_isOnSameTeam_cached) {
            info.isOnLocalTeam = env->CallBooleanMethod(localPlayer, mid_isOnSameTeam_cached, entity);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                info.isOnLocalTeam = false;
            }
        }

        // Entity ID (used to match incoming effect packets)
        if (JniCache::mid_getEntityId) {
            info.entityId = env->CallIntMethod(entity, JniCache::mid_getEntityId);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                info.entityId = 0;
            }
        }

        // Position data
        if (JniCache::fid_posX)
            info.posX = env->GetDoubleField(entity, JniCache::fid_posX);
        if (JniCache::fid_posY)
            info.posY = env->GetDoubleField(entity, JniCache::fid_posY);
        if (JniCache::fid_posZ)
            info.posZ = env->GetDoubleField(entity, JniCache::fid_posZ);
        if (JniCache::fid_lastTickPosX)
            info.lastPosX = env->GetDoubleField(entity, JniCache::fid_lastTickPosX);
        if (JniCache::fid_lastTickPosY)
            info.lastPosY = env->GetDoubleField(entity, JniCache::fid_lastTickPosY);
        if (JniCache::fid_lastTickPosZ)
            info.lastPosZ = env->GetDoubleField(entity, JniCache::fid_lastTickPosZ);

        // Visibility
        if (JniCache::mid_isInvisible) {
            info.isInvisible = env->CallBooleanMethod(entity, JniCache::mid_isInvisible);
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
                info.isInvisible = false;  // Safe default
            }
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

        // GameProfile name (real, not spoofable by plugins)
        if (JniCache::mid_getGameProfile && JniCache::mid_getProfileName) {
            jobject gp = env->CallObjectMethod(entity, JniCache::mid_getGameProfile);
            if (gp) {
                jstring profileName = (jstring)env->CallObjectMethod(gp, JniCache::mid_getProfileName);
                if (profileName) {
                    info.profileName = jstringToString(profileName);
                    env->DeleteLocalRef(profileName);
                }
                env->DeleteLocalRef(gp);
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
        if (info.name.empty())
            info.name = "Unknown";

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
                if (strCls)
                    env->DeleteLocalRef(strCls);
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
        if (JniCache::mid_getHealth)
            info.health = env->CallFloatMethod(entity, JniCache::mid_getHealth);
        if (JniCache::mid_getMaxHealth)
            info.maxHealth = env->CallFloatMethod(entity, JniCache::mid_getMaxHealth);
        if (JniCache::fid_hurtTime)
            info.hurtTime = env->GetIntField(entity, JniCache::fid_hurtTime);

        // Sneaking state
        if (JniCache::mid_isSneaking) {
            info.isSneaking = env->CallBooleanMethod(entity, JniCache::mid_isSneaking);
        }

        // Hitbox (bounding box)
        if (JniCache::fid_boundingBox && JniCache::cls_AxisAlignedBB) {
            jobject aabb = env->GetObjectField(entity, JniCache::fid_boundingBox);
            if (aabb) {
                if (JniCache::fid_aabb_minX)
                    info.hitboxMinX = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minX);
                if (JniCache::fid_aabb_minY)
                    info.hitboxMinY = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minY);
                if (JniCache::fid_aabb_minZ)
                    info.hitboxMinZ = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_minZ);
                if (JniCache::fid_aabb_maxX)
                    info.hitboxMaxX = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxX);
                if (JniCache::fid_aabb_maxY)
                    info.hitboxMaxY = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxY);
                if (JniCache::fid_aabb_maxZ)
                    info.hitboxMaxZ = (float)env->GetDoubleField(aabb, JniCache::fid_aabb_maxZ);
                env->DeleteLocalRef(aabb);
            }
        }

        // Armor
        if (JniCache::fid_inventory && JniCache::fid_armorInventory) {
            jobject inventory = env->GetObjectField(entity, JniCache::fid_inventory);
            if (inventory) {
                jobjectArray armorList = (jobjectArray)env->GetObjectField(inventory, JniCache::fid_armorInventory);
                if (armorList) {
                    int len = env->GetArrayLength(armorList);
                    for (int k = 0; k < len; k++) {
                        jobject stack = env->GetObjectArrayElement(armorList, k);
                        PlayerInfo::ArmorItem itemInfo;
                        if (stack) {
                            if (JniCache::mid_getItem && JniCache::mid_getIdFromItem) {
                                jobject item = env->CallObjectMethod(stack, JniCache::mid_getItem);
                                if (item) {
                                    itemInfo.id = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem, item);
                                    if (JniCache::mid_getMaxDamage)
                                        itemInfo.maxDamage = env->CallIntMethod(item, JniCache::mid_getMaxDamage);
                                    env->DeleteLocalRef(item);
                                }
                            }
                            if (JniCache::mid_getItemDamage) {
                                itemInfo.damage = env->CallIntMethod(stack, JniCache::mid_getItemDamage);
                            }
                            if (itemInfo.id > 0) {
                                itemInfo.protection = readEnchantLevel(env, stack, 0);  // Protection = id 0
                            }
                            env->DeleteLocalRef(stack);
                        }
                        info.armor.push_back(itemInfo);
                    }
                    env->DeleteLocalRef(armorList);
                }
                
                // Main Inventory (Potions)
                // NOTE: On many servers, remote player inventory is NOT synced to the client!
                // This usually only works if the server sends the data (e.g. some 1.7 servers or plugins).
                
                static bool loggedMissingField = false;
                if (!JniCache::fid_mainInventory) {
                    if (!loggedMissingField) {
                         log_debug("[SDK] Missing fid_mainInventory - cannot count potions");
                         loggedMissingField = true;
                    }
                } else {
                    jobjectArray mainList = (jobjectArray)env->GetObjectField(inventory, JniCache::fid_mainInventory);
                    if (mainList) {
                        int len = env->GetArrayLength(mainList);
                        for (int k = 0; k < len; k++) {
                             jobject stack = env->GetObjectArrayElement(mainList, k);
                             if (stack) {
                                 int id = 0;
                                 int damage = 0;
                                 int amount = 1;
                                 
                                 // Get ID
                                 if (JniCache::mid_getItem && JniCache::mid_getIdFromItem) {
                                      jobject item = env->CallObjectMethod(stack, JniCache::mid_getItem);
                                      if (item) {
                                           id = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem, item);
                                           env->DeleteLocalRef(item);
                                      }
                                 }
                                 
                                 if (id == 373) { // Potion
                                      // Get Damage/Metadata
                                      if (JniCache::mid_getItemDamage) {
                                           damage = env->CallIntMethod(stack, JniCache::mid_getItemDamage);
                                      }
                                      
                                      // get Stack Size
                                      if (JniCache::fid_stackSize) {
                                           amount = env->GetIntField(stack, JniCache::fid_stackSize);
                                      }
                                      
                                      // Check for various Health potion IDs (Splash II, Drink II, Splash I, etc)
                                      // 16421: Splash Health II
                                      // 16453: Splash Health II (Alt)
                                      // 8229: Drink Health II
                                      // 16389: Splash Health I
                                      // 8261: Drink Health I
                                      if (damage == 16421 || damage == 8229 || damage == 16453 || damage == 16389 || damage == 8261) {
                                           info.totalHealthPotions += amount;
                                      }
                                      
                                      // DEBUG logging (rate limited)
                                      // if (every_ms(5000)) {
                                      //      log_debug("[SDK] Found Potion ID: %d Metadata: %d Amount: %d", id, damage, amount);
                                      // }
                                 }
                                 
                                 env->DeleteLocalRef(stack);
                             }
                        }
                        env->DeleteLocalRef(mainList);
                    }
                }

                // Held weapon (sword/axe) + Sharpness, from mainInventory[currentItem].
                // NOTE: many servers don't sync remote players' held items -> stays empty.
                if (JniCache::fid_currentItem && JniCache::fid_mainInventory) {
                    int curSlot = env->GetIntField(inventory, JniCache::fid_currentItem);
                    jobjectArray mainArr = (jobjectArray)env->GetObjectField(inventory, JniCache::fid_mainInventory);
                    if (mainArr) {
                        int mlen = env->GetArrayLength(mainArr);
                        if (curSlot >= 0 && curSlot < mlen) {
                            jobject hstack = env->GetObjectArrayElement(mainArr, curSlot);
                            if (hstack) {
                                int hid = 0;
                                if (JniCache::mid_getItem && JniCache::mid_getIdFromItem) {
                                    jobject item = env->CallObjectMethod(hstack, JniCache::mid_getItem);
                                    if (item) {
                                        hid = env->CallStaticIntMethod(JniCache::cls_Item, JniCache::mid_getIdFromItem,
                                                                       item);
                                        if (isWeaponId(hid)) {
                                            info.weapon.id = hid;
                                            if (JniCache::mid_getMaxDamage)
                                                info.weapon.maxDamage = env->CallIntMethod(item, JniCache::mid_getMaxDamage);
                                        }
                                        env->DeleteLocalRef(item);
                                    }
                                }
                                if (isWeaponId(hid)) {
                                    if (JniCache::mid_getItemDamage)
                                        info.weapon.damage = env->CallIntMethod(hstack, JniCache::mid_getItemDamage);
                                    info.weapon.sharpness = readEnchantLevel(env, hstack, 16);  // Sharpness = id 16
                                }
                                env->DeleteLocalRef(hstack);
                            }
                        }
                        env->DeleteLocalRef(mainArr);
                    }
                }

                env->DeleteLocalRef(inventory);
            }
        }

        // Potions
        if (JniCache::fid_activePotionsMap && JniCache::mid_mapValues && JniCache::mid_collectionToArray) {
            jobject potionMap = env->GetObjectField(entity, JniCache::fid_activePotionsMap);
            if (potionMap) {
                jobject values = env->CallObjectMethod(potionMap, JniCache::mid_mapValues);
                if (values) {
                    jobjectArray arr = (jobjectArray)env->CallObjectMethod(values, JniCache::mid_collectionToArray);
                    if (arr) {
                        int len = env->GetArrayLength(arr);
                        for (int k = 0; k < len; k++) {
                            jobject effect = env->GetObjectArrayElement(arr, k);
                            if (effect) {
                                PlayerInfo::PotionEffectInfo effInfo;
                                if (JniCache::mid_getPotionID)
                                    effInfo.id = env->CallIntMethod(effect, JniCache::mid_getPotionID);
                                if (JniCache::mid_getDuration)
                                    effInfo.duration = env->CallIntMethod(effect, JniCache::mid_getDuration);
                                if (JniCache::mid_getAmplifier)
                                    effInfo.amplifier = env->CallIntMethod(effect, JniCache::mid_getAmplifier);
                                info.potions.push_back(effInfo);
                                env->DeleteLocalRef(effect);
                            }
                        }
                        env->DeleteLocalRef(arr);
                    }
                    env->DeleteLocalRef(values);
                }
                env->DeleteLocalRef(potionMap);
            }
        }

        // Fallback: some builds/servers may not expose activePotionsMap reliably. Try getActivePotionEffects().
        if (info.potions.empty() && JniCache::mid_getActivePotionEffects && JniCache::mid_collectionToArray) {
            jobject coll = env->CallObjectMethod(entity, JniCache::mid_getActivePotionEffects);
            if (coll) {
                jobjectArray arr = (jobjectArray)env->CallObjectMethod(coll, JniCache::mid_collectionToArray);
                if (arr) {
                    int len = env->GetArrayLength(arr);
                    for (int k = 0; k < len; k++) {
                        jobject effect = env->GetObjectArrayElement(arr, k);
                        if (effect) {
                            PlayerInfo::PotionEffectInfo effInfo;
                            if (JniCache::mid_getPotionID)
                                effInfo.id = env->CallIntMethod(effect, JniCache::mid_getPotionID);
                            if (JniCache::mid_getDuration)
                                effInfo.duration = env->CallIntMethod(effect, JniCache::mid_getDuration);
                            if (JniCache::mid_getAmplifier)
                                effInfo.amplifier = env->CallIntMethod(effect, JniCache::mid_getAmplifier);
                            if (effInfo.id > 0 && effInfo.duration > 0) {
                                info.potions.push_back(effInfo);
                            }
                            env->DeleteLocalRef(effect);
                        }
                    }
                    env->DeleteLocalRef(arr);
                }
                env->DeleteLocalRef(coll);
            }
            env->ExceptionClear();
        }

        // Reflection fallback for obfuscated environments (Notch-mapped, Lunar, etc.)
        // Try to locate EntityLivingBase's active potion map by scanning fields and looking for Map values that
        // contain PotionEffect instances.
        if (info.potions.empty() && JniCache::mid_mapValues && JniCache::mid_collectionToArray && JniCache::cls_PotionEffect) {
            jobject potionMap = try_get_active_potions_map_reflect(env, entity);
            if (potionMap) {
                jobject values = env->CallObjectMethod(potionMap, JniCache::mid_mapValues);
                if (values) {
                    jobjectArray arr = (jobjectArray)env->CallObjectMethod(values, JniCache::mid_collectionToArray);
                    if (arr) {
                        int len = env->GetArrayLength(arr);
                        for (int k = 0; k < len; k++) {
                            jobject effect = env->GetObjectArrayElement(arr, k);
                            if (effect) {
                                PlayerInfo::PotionEffectInfo effInfo;
                                if (JniCache::mid_getPotionID)
                                    effInfo.id = env->CallIntMethod(effect, JniCache::mid_getPotionID);
                                if (JniCache::mid_getDuration)
                                    effInfo.duration = env->CallIntMethod(effect, JniCache::mid_getDuration);
                                if (JniCache::mid_getAmplifier)
                                    effInfo.amplifier = env->CallIntMethod(effect, JniCache::mid_getAmplifier);
                                if (effInfo.id > 0 && effInfo.duration > 0) {
                                    info.potions.push_back(effInfo);
                                }
                                env->DeleteLocalRef(effect);
                            }
                        }
                        env->DeleteLocalRef(arr);
                    }
                    env->DeleteLocalRef(values);
                }
                env->DeleteLocalRef(potionMap);
            }
            env->ExceptionClear();
        }

        // If the client isn't updating EntityLivingBase.activePotionsMap for remote players (server-dependent),
        // fall back to tracking S1D/S1E packets (EffectTracker).
        if (info.entityId > 0) {
            if (!info.uuid.empty()) {
                EffectTracker::AssociateEntity(info.entityId, info.uuid);
            }
            auto tracked = EffectTracker::GetEffects(info.entityId);
            if (!tracked.empty()) {
                if (info.potions.empty()) {
                    info.potions = std::move(tracked);
                } else {
                    // Merge (prefer longer duration / stronger amp for same effect id)
                    for (const auto& te : tracked) {
                        bool found = false;
                        for (auto& pe : info.potions) {
                            if (pe.id == te.id) {
                                found = true;
                                if (te.amplifier > pe.amplifier ||
                                    (te.amplifier == pe.amplifier && te.duration > pe.duration)) {
                                    pe = te;
                                }
                                break;
                            }
                        }
                        if (!found) {
                            info.potions.push_back(te);
                        }
                    }
                }
            }
        }

        result.push_back(std::move(info));
        // Free all local refs in this player iteration (entity, uuid, displayName, armor, potions, etc.).
        // info.instance is a GlobalRef and survives this pop.
        env->PopLocalFrame(nullptr);
    }

    // Pop the outer function frame (releases world, playerList, localPlayer, etc.)
    env->PopLocalFrame(nullptr);

    return result;
}

// =============================================================================
// canEntityBeSeen - Check if local player can see target entity
// =============================================================================

bool Minecraft::canEntityBeSeen(jobject target) {
    if (!isValid() || !target)
        return false;

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    jobject localPlayer = getPlayer();
    if (!localPlayer)
        return false;

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

int Minecraft::getLocalPlayerTeamColor() {
    if (!isValid())
        return -1;

    JNIEnv* env = Java::Env();
    if (!env)
        return -1;

    if (env->PushLocalFrame(16) < 0)
        return -1;

    jobject localPlayer = getPlayer();
    if (!localPlayer) {
        env->PopLocalFrame(nullptr);
        return -1;
    }

    int teamColor = -1;
    std::string displayName;

    if (JniCache::mid_getDisplayName) {
        jobject displayResult = env->CallObjectMethod(localPlayer, JniCache::mid_getDisplayName);
        if (displayResult) {
            jclass strCls = env->FindClass("java/lang/String");
            if (strCls && env->IsInstanceOf(displayResult, strCls)) {
                displayName = jstringToString((jstring)displayResult);
            } else if (JniCache::cls_IChatComponent && env->IsInstanceOf(displayResult, JniCache::cls_IChatComponent)) {
                if (JniCache::mid_getFormattedText) {
                    jstring formatted = (jstring)env->CallObjectMethod(displayResult, JniCache::mid_getFormattedText);
                    if (formatted) {
                        displayName = jstringToString(formatted);
                        env->DeleteLocalRef(formatted);
                    }
                }
            }
            if (strCls)
                env->DeleteLocalRef(strCls);
            env->DeleteLocalRef(displayResult);
        }
        env->ExceptionClear();
    }

    if (!displayName.empty()) {
        teamColor = parseTeamColor(displayName);
    }

    env->PopLocalFrame(nullptr);
    return teamColor;
}

std::string Minecraft::getLocalPlayerName() {
    if (!isValid() || !Java::Env())
        return "";

    JNIEnv* env = Java::Env();
    if (env->PushLocalFrame(16) < 0)
        return "";

    jobject localPlayer = getPlayer();
    if (!localPlayer) {
        env->PopLocalFrame(nullptr);
        return "";
    }

    std::string name;
    if (JniCache::mid_getGameProfile && JniCache::mid_getProfileName) {
        jobject profile = env->CallObjectMethod(localPlayer, JniCache::mid_getGameProfile);
        if (profile) {
            jstring nameStr = (jstring)env->CallObjectMethod(profile, JniCache::mid_getProfileName);
            if (nameStr) {
                name = jstringToString(nameStr);
                env->DeleteLocalRef(nameStr);
            }
            env->DeleteLocalRef(profile);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }
    if (name.empty() && JniCache::mid_getName) {
        jstring nameStr = (jstring)env->CallObjectMethod(localPlayer, JniCache::mid_getName);
        if (nameStr) {
            name = jstringToString(nameStr);
            env->DeleteLocalRef(nameStr);
        }
        if (env->ExceptionCheck())
            env->ExceptionClear();
    }

    env->PopLocalFrame(nullptr);
    return name;
}

// =============================================================================
// findPlayerByName - Find player in current world by name and get their UUID
// =============================================================================

PlayerInfo Minecraft::findPlayerByName(const std::string& searchName) {
    if (searchName.empty())
        return PlayerInfo();

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

        // Check real GameProfile name
        std::string profileLower = p.profileName;
        std::transform(profileLower.begin(), profileLower.end(), profileLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        if (!profileLower.empty() && profileLower == searchLower) {
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
