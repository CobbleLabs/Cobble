#include "nametags.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/visual_cache.h"
#include "../../sdk/player/render_data.h"
#include "../../util/health_predictor.hpp"
#include "../../ui/widgets/widgets.hpp"
#include "../../ui/widgets/icons.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/http.hpp"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"
#include "../../jni/apollo/apollo_packet_observer.hpp"
#include "../../jni/packet/bytecode_patcher.hpp"

#include <cmath>
#include <cstdio>
#include <thread>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../util/textures/item_textures.h"

// Helper for potion names
// Helper for potion names
static const char* getPotionName(int id) {
    switch (id) {
        case 1:
            return "Speed";
        case 2:
            return "Slowness";
        case 3:
            return "Haste";
        case 4:
            return "Mining Fatigue";
        case 5:
            return "Strength";
        case 6:
            return "Instant Health";
        case 7:
            return "Instant Damage";
        case 8:
            return "Jump Boost";
        case 9:
            return "Nausea";
        case 10:
            return "Regeneration";
        case 11:
            return "Resistance";
        case 12:
            return "Fire Res";
        case 13:
            return "Water Breathing";
        case 14:
            return "Invisibility";
        case 15:
            return "Blindness";
        case 16:
            return "Night Vision";
        case 17:
            return "Hunger";
        case 18:
            return "Weakness";
        case 19:
            return "Poison";
        case 20:
            return "Wither";
        case 21:
            return "Health Boost";
        case 22:
            return "Absorption";
        case 23:
            return "Saturation";
        default:
            return "Unknown";
    }
}

static ImU32 GetPotionColor(int id) {
    switch (id) {
        case 1:
            return IM_COL32(124, 175, 198, 255);  // Speed - Light Blue
        case 2:
            return IM_COL32(90, 108, 129, 255);  // Slowness - Gray Blue
        case 3:
            return IM_COL32(217, 192, 67, 255);  // Haste - Gold
        case 4:
            return IM_COL32(74, 66, 23, 255);  // Fatigue - Brown
        case 5:
            return IM_COL32(147, 36, 35, 255);  // Strength - Red
        case 6:
            return IM_COL32(67, 10, 9, 255);  // Health - Dark Red
        case 7:
            return IM_COL32(67, 10, 9, 255);  // Damage - Dark Red
        case 8:
            return IM_COL32(34, 255, 76, 255);  // Jump - Green
        case 9:
            return IM_COL32(85, 29, 74, 255);  // Nausea - Purple
        case 10:
            return IM_COL32(205, 92, 171, 255);  // Regen - Pink
        case 11:
            return IM_COL32(153, 69, 58, 255);  // Resistance - Red/Brown
        case 12:
            return IM_COL32(228, 154, 58, 255);  // Fire Res - Orange
        case 13:
            return IM_COL32(46, 82, 153, 255);  // Water Breathing - Blue
        case 14:
            return IM_COL32(127, 131, 146, 255);  // Invis - Gray
        case 15:
            return IM_COL32(31, 31, 35, 255);  // Blindness - Black
        case 16:
            return IM_COL32(31, 31, 161, 255);  // Night Vision - Dark Blue
        case 17:
            return IM_COL32(88, 118, 83, 255);  // Hunger - Green/Brown
        case 18:
            return IM_COL32(72, 77, 72, 255);  // Weakness - Dark Gray
        case 19:
            return IM_COL32(78, 147, 49, 255);  // Poison - Green
        case 20:
            return IM_COL32(53, 42, 39, 255);  // Wither - Dark Brown
        case 21:
            return IM_COL32(248, 125, 35, 255);  // Health Boost - Orange
        case 22:
            return IM_COL32(37, 82, 165, 255);  // Absorption - Blue
        case 23:
            return IM_COL32(248, 36, 35, 255);  // Saturation - Red
        default:
            return IM_COL32(255, 255, 255, 255);
    }
}

static std::string formatDuration(int ticks) {
    int seconds = ticks / 20;
    int minutes = seconds / 60;
    seconds = seconds % 60;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d:%02d", minutes, seconds);
    return std::string(buf);
}

static ImU32 GetDurabilityColor(float percent) {
    if (percent > 0.5f)
        return IM_COL32(0, 255, 0, 255);
    if (percent > 0.25f)
        return IM_COL32(255, 255, 0, 255);
    return IM_COL32(255, 0, 0, 255);
}

static std::string potion_key_sig(const std::vector<PlayerInfo::PotionEffectInfo>& potions) {
    if (potions.empty())
        return "<none>";

    std::vector<PlayerInfo::PotionEffectInfo> tmp = potions;
    std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
        if (a.id != b.id)
            return a.id < b.id;
        return a.amplifier < b.amplifier;
    });

    std::ostringstream ss;
    for (size_t i = 0; i < tmp.size(); i++) {
        if (i)
            ss << ",";
        ss << tmp[i].id << ":" << tmp[i].amplifier;
    }
    return ss.str();
}

static std::string potion_full_sig(const std::vector<PlayerInfo::PotionEffectInfo>& potions) {
    if (potions.empty())
        return "<none>";

    std::vector<PlayerInfo::PotionEffectInfo> tmp = potions;
    std::sort(tmp.begin(), tmp.end(), [](const auto& a, const auto& b) {
        if (a.id != b.id)
            return a.id < b.id;
        if (a.amplifier != b.amplifier)
            return a.amplifier < b.amplifier;
        return a.duration < b.duration;
    });

    std::ostringstream ss;
    for (size_t i = 0; i < tmp.size(); i++) {
        if (i)
            ss << ",";
        ss << tmp[i].id << ":" << tmp[i].amplifier << ":" << tmp[i].duration;
    }
    return ss.str();
}

NameTags* nameTags = nullptr;

NameTags::NameTags() : Module("NameTags") {
    keybind = 0;  // No default keybind
    renders = true;
    worldVisual = true;
    usesPlayerCache = true;
}

void NameTags::applyVanillaHide() {
    // Hide vanilla names only while the module is on AND the option is checked.
    BytecodePatcher::SetHideVanillaNames(enabled && settings.hideVanilla);
}

void NameTags::onEnable() {
    log_ok("NameTags enabled");
    lastWorldRef = nullptr;  // Force world change detection on first update
    ApolloPacketObserver::SetDebugEntityEffects(settings.debugEffects);
    applyVanillaHide();

    // Reset RenderData to force fresh matrices
    RenderData::initialized = false;
    RenderData::initFailed = false;
    RenderData::initRetryCount = 0;
    RenderData::Reset();

    // Clear any stale cached players
    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedPlayers.clear();
    lastPotionSig.clear();
}

void NameTags::onDisable() {
    log_ok("NameTags disabled");
    ApolloPacketObserver::SetDebugEntityEffects(false);
    applyVanillaHide();  // module off -> restore vanilla names

    // Clean up world ref
    /*if (lastWorldRef && Java::Env) {
        Java::Env()->DeleteGlobalRef(lastWorldRef);
    }*/
    lastWorldRef = nullptr;

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedPlayers.clear();
    mojangCache.clear();
    lastPotionSig.clear();
}

// Detect world change (server join/leave, dimension change)
// Reinitializes RenderData when world reference changes
void NameTags::checkWorldChange() {
    if (!Java::Env() || !Java::Initialized)
        return;

    jobject currentWorld = mc.getWorld();

    // First time or world changed
    bool worldChanged = false;

    if (lastWorldRef == nullptr && currentWorld != nullptr) {
        // First world load
        worldChanged = true;
    } else if (lastWorldRef != nullptr && currentWorld == nullptr) {
        // Left server
        worldChanged = true;
    } else if (lastWorldRef != nullptr && currentWorld != nullptr) {
        // Check if same object
        if (!Java::Env()->IsSameObject(lastWorldRef, currentWorld)) {
            worldChanged = true;
        }
    }

    if (worldChanged) {
        // Clear cache
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            cachedPlayers.clear();
            lastPotionSig.clear();
        }

        // Force RenderData to reinitialize
        RenderData::initialized = false;
        RenderData::initFailed = false;
        RenderData::initRetryCount = 0;
        RenderData::Reset();

        // Clean up old global ref
        if (lastWorldRef) {
            Java::Env()->DeleteGlobalRef(lastWorldRef);
            lastWorldRef = nullptr;
        }

        // Store new global ref
        if (currentWorld) {
            lastWorldRef = Java::Env()->NewGlobalRef(currentWorld);
        }
    }

    // Clean up local ref
    if (currentWorld) {
        Java::Env()->DeleteLocalRef(currentWorld);
    }
}

// Called from MAIN thread - cache player positions and render info
void NameTags::onUpdate() {
    // Keep packet-level effect tracking debug in sync with this module's toggle.
    // This also enables the S1D/S1E fallback path for remote player effects.
    static bool lastDebugEffects = false;
    if (settings.debugEffects != lastDebugEffects) {
        ApolloPacketObserver::SetDebugEntityEffects(settings.debugEffects);
        if (settings.debugEffects) {
            std::lock_guard<std::mutex> lock(cacheMutex);
            lastPotionSig.clear();
        }
        lastDebugEffects = settings.debugEffects;
    }

    // Check for world changes first
    // checkWorldChange();

    // If player is not valid (in menu, left server, etc), clear cache
    if (!player.isValid()) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedPlayers.clear();
        return;
    }

    auto snap = VisualCache::GetSnapshot();
    if (!snap.valid) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedPlayers.clear();
        return;
    }

    // Get render position and partial ticks
    RenderPos renderPos = snap.renderPos;
    float partialTicks = snap.partialTicks;

    // Get local player position for distance calculation
    double localX = snap.localX;
    double localY = snap.localY;
    double localZ = snap.localZ;

    // Get player list from cache
    auto& players = snap.players;

    // Build new cache
    std::vector<CachedPlayer> newCache;

    for (auto& p : players) {
        double dx = p.posX - localX;
        double dy = p.posY - localY;
        double dz = p.posZ - localZ;
        float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

        // Skip if out of range or too close (self)
        if (dist > settings.range || dist < 0.5f)
            continue;

        CachedPlayer cached;
        cached.name = p.name;
        if (!p.profileName.empty()) {
            cached.mojangName = p.profileName;
        }
        cached.uuid = p.uuid;
        cached.entityId = p.entityId;
        cached.posX = p.posX;
        cached.posY = p.posY;
        cached.posZ = p.posZ;
        cached.lastPosX = p.lastPosX;
        cached.lastPosY = p.lastPosY;
        cached.lastPosZ = p.lastPosZ;
        cached.distance = dist;
        cached.health = p.health;
        cached.maxHealth = p.maxHealth;
        cached.armor = p.armor;      // Copy armor
        cached.potions = p.potions;  // Copy potions
        cached.weapon = p.weapon;    // Copy held weapon
        cached.valid = true;

        if (settings.debugEffects) {
            std::string key = !cached.uuid.empty() ? cached.uuid : ("#" + std::to_string(cached.entityId));
            std::string sig = potion_key_sig(cached.potions);
            auto it = lastPotionSig.find(key);
            if (it == lastPotionSig.end() || it->second != sig) {
                lastPotionSig[key] = sig;
                std::string full = potion_full_sig(cached.potions);
                log_debug("[NameTags][Effects] %s uuid=%s entityId=%d potions=%zu sig=%s", cached.name.c_str(),
                          cached.uuid.empty() ? "<none>" : cached.uuid.c_str(), cached.entityId, cached.potions.size(),
                          full.c_str());
            }
        }

        // Mojang name resolution for obfuscated GameProfiles
        // Always lookup, but only override display name if different from server name
        // NOTE: mojangCache is also written by the detached lookup threads below,
        // which lock cacheMutex. EVERY access here must hold the same lock or the
        // unordered_map rehash races the threads -> heap corruption. Decide under
        // the lock, then spawn the thread AFTER releasing it.
        if (!p.uuid.empty()) {
            bool needLookup = false;
            std::string uuidCopy = p.uuid;
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                auto& entry = mojangCache[p.uuid];

                if (entry.lookupDone) {
                    // Compare with server name - only use Mojang if different
                    if (!entry.realName.empty() && entry.realName != p.name) {
                        cached.mojangName = entry.realName;
                    }
                } else if (!entry.lookupQueued) {
                    // Queue async lookup
                    entry.lookupQueued = true;
                    needLookup = true;
                }
            }

            if (needLookup) {
                std::thread([this, uuidCopy]() {
                    std::string realName = http::lookupMojangName(uuidCopy);
                    if (!realName.empty()) {
                        std::lock_guard<std::mutex> lock(cacheMutex);
                        auto& e = mojangCache[uuidCopy];
                        e.realName = realName;
                        e.lookupDone = true;
                    } else {
                        std::lock_guard<std::mutex> lock(cacheMutex);
                        auto& e = mojangCache[uuidCopy];
                        e.lookupDone = true;  // Mark done to avoid retry spam
                    }
                }).detach();
            }
        }

        newCache.push_back(cached);
    }

    if (settings.debugEffects) {
        std::unordered_set<std::string> seen;
        seen.reserve(newCache.size());
        for (const auto& cp : newCache) {
            std::string key = !cp.uuid.empty() ? cp.uuid : ("#" + std::to_string(cp.entityId));
            seen.insert(key);
        }
        for (auto it = lastPotionSig.begin(); it != lastPotionSig.end();) {
            if (seen.find(it->first) == seen.end()) {
                it = lastPotionSig.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Thread-safe swap
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedPlayers = std::move(newCache);
        cachedRenderX = renderPos.x;
        cachedRenderY = renderPos.y;
        cachedRenderZ = renderPos.z;
        cachedPartialTicks = partialTicks;

        // Cache local player position for fallback (when renderPos is unavailable)
        cachedLocalX = localX;
        cachedLocalY = localY;
        cachedLocalZ = localZ;
        cachedLocalLastX = snap.localLastX;
        cachedLocalLastY = snap.localLastY;
        cachedLocalLastZ = snap.localLastZ;

        // Apply resolved Mojang names to cached players
        for (auto& cp : cachedPlayers) {
            if (!cp.uuid.empty()) {
                auto it = mojangCache.find(cp.uuid);
                if (it != mojangCache.end() && it->second.lookupDone && !it->second.realName.empty()) {
                    cp.mojangName = it->second.realName;
                }
            }
        }
    }
}

// Called from RENDER thread
void NameTags::onRender() {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw)
        return;

    if (!RenderData::updated)
        return;

    // Copy cache safely
    std::vector<CachedPlayer> localCache;
    double renderX, renderY, renderZ;
    float partialTicks;
    double localX, localY, localZ;
    double localLastX, localLastY, localLastZ;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        localCache = cachedPlayers;
        renderX = cachedRenderX;
        renderY = cachedRenderY;
        renderZ = cachedRenderZ;
        partialTicks = cachedPartialTicks;
        localX = cachedLocalX;
        localY = cachedLocalY;
        localZ = cachedLocalZ;
        localLastX = cachedLocalLastX;
        localLastY = cachedLocalLastY;
        localLastZ = cachedLocalLastZ;
    }

    // FALLBACK: If renderPos is (0,0,0) (failed init), use interpolated local player position
    // The camera position is essentially the player's eye position
    bool useFallback = (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0);

    if (useFallback) {
        // Interpolate local player position + eye height for camera
        renderX = localLastX + (localX - localLastX) * partialTicks;
        renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;  // Eye height
        renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
    }

    if (localCache.empty())
        return;

    for (auto& p : localCache) {
        if (!p.valid)
            continue;

        // origin = renderPos - entityLastPos + (entityLastPos - entityPos) * renderPartialTicks
        // This interpolates the entity position between ticks for smooth movement

        // Entity height offset above player feet
        // 1.7.10: posY is at feet, need +2.0 for above head
        // 1.8.9: posY is at feet, need +2.0 for above head
        // But in 1.7.10 with fallback camera, we need to account for eye height difference
        float entityHeight = 2.0f;  // Above head

        // Point = renderPos - (entityLastPos interpolated) + offset
        float interpX = (float)(p.lastPosX + (p.posX - p.lastPosX) * partialTicks);
        float interpY = (float)(p.lastPosY + (p.posY - p.lastPosY) * partialTicks) + entityHeight;
        float interpZ = (float)(p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks);

        // Position relative to render origin (camera)
        float relX = interpX - (float)renderX;
        float relY = interpY - (float)renderY;
        float relZ = interpZ - (float)renderZ;

        Vector3 worldPos{relX, relY, relZ};
        Vector2 screenPos;

        bool onScreen = CWorldToScreen::WorldToScreen(worldPos, RenderData::modelView, RenderData::projection,
                                                      RenderData::screenWidth, RenderData::screenHeight, screenPos);

        if (onScreen) {
            bool isManualFriend = friendsManager && ((!p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid)) ||
                                                     friendsManager->isFriend(FriendsManager::stripColorCodes(p.name)));
            const float* color = isManualFriend ? settings.friendColor : settings.normalColor;
            ImU32 nameColor = ImGui::ColorConvertFloat4ToU32(ImVec4(color[0], color[1], color[2], color[3]));
            renderPlayerTag(draw, screenPos.x, screenPos.y, p.getDisplayName().c_str(), nameColor, p.distance, p.health,
                            p.maxHealth, p.armor, p.potions, p.weapon);
        }
    }
}

static ImU32 GetHealthColor(float health, float maxHealth) {
    if (maxHealth <= 0.0f)
        maxHealth = 1.0f;
    float percent = health / maxHealth;
    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 1.0f)
        percent = 1.0f;

    // Red-Yellow-Green Gradient
    int r, g;
    if (percent > 0.5f) {
        // 50-100%: Yellow to Green (255,255,0 -> 0,255,0)
        // R goes 255 -> 0
        r = (int)((1.0f - percent) * 2.0f * 255.0f);
        g = 255;
    } else {
        // 0-50%: Red to Yellow (255,0,0 -> 255,255,0)
        // G goes 0 -> 255
        r = 255;
        g = (int)(percent * 2.0f * 255.0f);
    }

    return IM_COL32(r, g, 0, 255);
}

void NameTags::renderPlayerTag(ImDrawList* draw, float screenX, float screenY, const char* name, unsigned int nameColor,
                               float distance, float health, float maxHealth,
                               const std::vector<PlayerInfo::ArmorItem>& armor,
                               const std::vector<PlayerInfo::PotionEffectInfo>& potions,
                               const PlayerInfo::HeldItem& weapon) {
    if (!draw)
        return;

    float displayScale = settings.scale;
    ImGui::SetWindowFontScale(displayScale);

    char nameText[256];
    char healthText[64];
    char distText[64];

    // Format strings
    snprintf(nameText, sizeof(nameText), "%s", name);

    if (settings.showHealth) {
        snprintf(healthText, sizeof(healthText), " [%.0f]", health);
    } else {
        healthText[0] = '\0';
    }

    if (settings.showDistance) {
        snprintf(distText, sizeof(distText), " [%.0fm]", distance);
    } else {
        distText[0] = '\0';
    }

    // --- Pre-calculate Weapon + Armor Cells ---
    struct IconCell {
        int id;
        int damage;
        int maxDamage;
        struct EnchDraw {
            std::string text;
            ImU32 color;
        };
        std::vector<EnchDraw> enchants;
    };
    std::vector<IconCell> cells;

    if (settings.showWeapon && weapon.id > 0) {
        IconCell cell;
        cell.id = weapon.id;
        cell.damage = weapon.damage;
        cell.maxDamage = weapon.maxDamage;

        if (settings.showEnchants) {
            // Priority 1 (closest to icon): Sharpness or Power
            if (weapon.sharpness > 0) {
                cell.enchants.push_back({"S" + std::to_string(weapon.sharpness),
                                         IM_COL32(85, 255, 255, 255)});  // Cyan Sharpness/Sword level (Exodar style)
            } else if (weapon.power > 0) {
                cell.enchants.push_back(
                    {"P" + std::to_string(weapon.power), IM_COL32(85, 255, 255, 255)});  // Cyan Power
            }

            // Priority 2: Unbreaking
            if (weapon.unbreaking > 0) {
                cell.enchants.push_back({"U" + std::to_string(weapon.unbreaking),
                                         IM_COL32(85, 255, 255, 255)});  // Cyan Unbreaking (Exodar style)
            }
        }
        cells.push_back(cell);
    }
    if (settings.showArmor) {
        for (auto it = armor.rbegin(); it != armor.rend(); ++it) {
            if (it->id > 0) {
                IconCell cell;
                cell.id = it->id;
                cell.damage = it->damage;
                cell.maxDamage = it->maxDamage;

                if (settings.showEnchants) {
                    // Priority 1 (closest to icon): Protection
                    if (it->protection > 0) {
                        cell.enchants.push_back(
                            {"P" + std::to_string(it->protection), IM_COL32(85, 255, 255, 255)});  // Cyan Protection
                    }

                    // Priority 2: Unbreaking
                    if (it->unbreaking > 0) {
                        cell.enchants.push_back(
                            {"U" + std::to_string(it->unbreaking), IM_COL32(85, 255, 255, 255)});  // Cyan Unbreaking
                    }
                }
                cells.push_back(cell);
            }
        }
    }

    float iconSize = 16.0f * displayScale;
    float pad = 6.0f * displayScale;
    float rowWidth = cells.empty() ? 0.0f : (cells.size() * iconSize + (cells.size() - 1) * pad);

    // Calculate sizes
    ImVec2 nameSize = ImGui::CalcTextSize(nameText);

    char healthDisplay[64];
    if (settings.showHealth) {
        snprintf(healthDisplay, sizeof(healthDisplay), " %.0f", health / 2.0f);
    } else {
        healthDisplay[0] = '\0';
    }
    ImVec2 healthSize = ImGui::CalcTextSize(healthDisplay);
    ImVec2 heartSize = settings.showHealth ? ImGui::CalcTextSize(" " ICON_FA_HEART) : ImVec2(0, 0);
    ImVec2 distSize = ImGui::CalcTextSize(distText);

    float textWidth = nameSize.x + healthSize.x + heartSize.x + distSize.x;
    float totalWidth = textWidth;
    float sideArmorPadding = 6.0f * displayScale;

    // If armor is drawn on side, include it in total width of background
    if (settings.armorOnSide && !cells.empty()) {
        totalWidth += rowWidth + sideArmorPadding;
    }

    float totalHeight = nameSize.y;  // Assumes all same font size

    float posX = screenX - totalWidth / 2.0f;
    float posY = screenY - totalHeight;
    float currentY = posY;  // Top of the name tag

    // Background for Name
    if (settings.background) {
        float paddingX = 6.0f * displayScale;
        float paddingY = 4.0f * displayScale;
        float extraTopPadding = 0.0f;

        // If armor is on side and has active enchant texts drawn above icons, expand background upwards
        if (settings.armorOnSide && settings.showEnchants && !cells.empty()) {
            size_t maxEnchCount = 0;
            for (const auto& c : cells) {
                if (c.enchants.size() > maxEnchCount) {
                    maxEnchCount = c.enchants.size();
                }
            }
            if (maxEnchCount > 0) {
                ImGui::SetWindowFontScale(displayScale * 0.75f);
                float singleEnchantLineHeight = ImGui::CalcTextSize("P4").y + 0.5f * displayScale;
                extraTopPadding = singleEnchantLineHeight * maxEnchCount;
                ImGui::SetWindowFontScale(displayScale);
            }
        }

        // Beautiful glassmorphism styled card with clean 1px border
        draw->AddRectFilled(ImVec2(posX - paddingX, posY - paddingY - extraTopPadding),
                            ImVec2(posX + totalWidth + paddingX, posY + totalHeight + paddingY),
                            IM_COL32(12, 12, 12, 180),
                            4.0f);  // Soft rounding
        draw->AddRect(ImVec2(posX - paddingX, posY - paddingY - extraTopPadding),
                      ImVec2(posX + totalWidth + paddingX, posY + totalHeight + paddingY), IM_COL32(255, 255, 255, 20),
                      4.0f, 0, 1.0f);
    }

    // Draw Name and Info
    draw->AddText(ImVec2(posX + 1, posY + 1), IM_COL32(0, 0, 0, 255), nameText);
    draw->AddText(ImVec2(posX, posY), nameColor, nameText);

    float currentX = posX + nameSize.x;

    if (settings.showHealth) {
        ImU32 hpColor = GetHealthColor(health, maxHealth);
        draw->AddText(ImVec2(currentX + 1, posY + 1), IM_COL32(0, 0, 0, 255), healthDisplay);
        draw->AddText(ImVec2(currentX, posY), hpColor, healthDisplay);
        currentX += healthSize.x;

        std::string heartStr = " " ICON_FA_HEART;
        draw->AddText(ImVec2(currentX + 1, posY + 1), IM_COL32(0, 0, 0, 255), heartStr.c_str());
        draw->AddText(ImVec2(currentX, posY), hpColor, heartStr.c_str());
        currentX += heartSize.x;
    }
    if (settings.showDistance) {
        draw->AddText(ImVec2(currentX + 1, posY + 1), IM_COL32(0, 0, 0, 255), distText);
        draw->AddText(ImVec2(currentX, posY), IM_COL32(200, 200, 200, 255), distText);
        currentX += distSize.x;
    }

    // --- Render Weapon + Armor (with enchant levels) ---
    if (!cells.empty()) {
        float startX, rowY;
        if (settings.armorOnSide) {
            // Beside the name row, to the right of name/health/distance
            startX = posX + textWidth + sideArmorPadding;
            rowY = posY + (totalHeight - iconSize) * 0.5f;
        } else {
            // Centered above the name (legacy layout); effects stack above it
            startX = screenX - rowWidth / 2.0f;

            float enchantHeight = 0.0f;
            if (settings.showEnchants) {
                size_t maxEnchCount = 0;
                for (const auto& c : cells) {
                    if (c.enchants.size() > maxEnchCount) {
                        maxEnchCount = c.enchants.size();
                    }
                }
                if (maxEnchCount > 0) {
                    ImGui::SetWindowFontScale(displayScale * 0.75f);
                    float singleEnchantLineHeight = ImGui::CalcTextSize("P4").y + 0.5f * displayScale;
                    enchantHeight = singleEnchantLineHeight * maxEnchCount;
                    ImGui::SetWindowFontScale(displayScale);
                }
            }

            rowY = currentY - iconSize - 4.0f * displayScale;
            currentY = rowY - enchantHeight;
        }

        float ix = startX;
        for (const auto& c : cells) {
            unsigned int texId = ItemTextureManager::get().getTexture(c.id, c.damage);
            if (texId > 0) {
                draw->AddImage((ImTextureID)(intptr_t)texId, ImVec2(ix, rowY), ImVec2(ix + iconSize, rowY + iconSize));
            }

            // Durability bar
            if (c.maxDamage > 0) {
                float pct = 1.0f - (float)c.damage / (float)c.maxDamage;
                if (pct < 0.0f)
                    pct = 0.0f;
                ImU32 barColor = GetDurabilityColor(pct);
                float barH = 2.0f * displayScale;
                float barY = rowY + iconSize - barH;
                draw->AddRectFilled(ImVec2(ix, barY), ImVec2(ix + iconSize, barY + barH), IM_COL32(0, 0, 0, 255));
                draw->AddRectFilled(ImVec2(ix, barY), ImVec2(ix + iconSize * pct, barY + barH), barColor);
            }

            // Enchant level texts stacked vertically above the icon (closest to icon is bottom element)
            if (settings.showEnchants && !c.enchants.empty()) {
                ImGui::SetWindowFontScale(displayScale * 0.75f);
                float currentEnchY = rowY - 1.0f * displayScale;
                for (const auto& ench : c.enchants) {
                    ImVec2 ns = ImGui::CalcTextSize(ench.text.c_str());
                    float nx = ix + (iconSize - ns.x) * 0.5f;
                    float ny = currentEnchY - ns.y;

                    // Draw text shadow / outline
                    draw->AddText(ImVec2(nx + 1.0f, ny + 1.0f), IM_COL32(0, 0, 0, 255), ench.text.c_str());

                    // Draw colored text
                    draw->AddText(ImVec2(nx, ny), ench.color, ench.text.c_str());

                    currentEnchY = ny - 0.5f * displayScale;
                }
                ImGui::SetWindowFontScale(displayScale);
            }

            ix += iconSize + pad;
        }
    }

    // --- Render Effects ---

    if (settings.showEffects && !potions.empty()) {
        // Render as list of text or icons. User asked for "Show Effects".
        // Stack them vertically upwards from currentY

        float effectLineHeight = 10.0f * displayScale;  // Small text
        float effectY = currentY - effectLineHeight;

        ImGui::SetWindowFontScale(displayScale * 0.7f);  // Smaller font for effects

        for (const auto& pot : potions) {
            // Filter expired or very short effects (less than 1s)
            if (pot.duration < 20)
                continue;

            std::string name = getPotionName(pot.id);
            std::string dur = formatDuration(pot.duration);
            std::string amp = "";
            if (pot.amplifier > 0) {
                char ampBuf[8];
                snprintf(ampBuf, sizeof(ampBuf), " %d", pot.amplifier + 1);
                amp = ampBuf;
            }

            std::string textFull = name + amp + " (" + dur + ")";
            ImVec2 textSize = ImGui::CalcTextSize(textFull.c_str());

            // Draw formatted text: Color(Name + Amp) + Gray(Duration)
            // We need to calculate size of name+amp separately to offset duration

            std::string nameAmp = name + amp;
            std::string durText = " (" + dur + ")";

            ImVec2 nameSize = ImGui::CalcTextSize(nameAmp.c_str());
            ImVec2 durSize = ImGui::CalcTextSize(durText.c_str());

            float fullWidth = nameSize.x + durSize.x;
            float effectX = screenX - fullWidth / 2.0f;

            // Draw Name + Amp with Color
            ImU32 color = GetPotionColor(pot.id);
            draw->AddText(ImVec2(effectX + 1, effectY + 1), IM_COL32(0, 0, 0, 255), nameAmp.c_str());
            draw->AddText(ImVec2(effectX, effectY), color, nameAmp.c_str());

            // Draw Duration in Gray/White
            draw->AddText(ImVec2(effectX + nameSize.x + 1, effectY + 1), IM_COL32(0, 0, 0, 255), durText.c_str());
            draw->AddText(ImVec2(effectX + nameSize.x, effectY), IM_COL32(200, 200, 200, 255), durText.c_str());

            effectY -= effectLineHeight;
        }

        ImGui::SetWindowFontScale(displayScale);  // Restore scale
    }

    // END OF RENDER
    return;
}

void NameTags::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();
            ui::SliderFloat("Scale", &settings.scale, 0.5f, 2.0f, "%.1f");
            ui::Checkbox("Background", &settings.background);
            ui::color("Normal Color", settings.normalColor);
            ui::color("Friend Color", settings.friendColor);
        },
        "Display",
        [this]() {
            ui::Checkbox("Show Health", &settings.showHealth);
            ui::Checkbox("Show Distance", &settings.showDistance);
            ui::Checkbox("Show Armor", &settings.showArmor);
            ui::Checkbox("Show Weapon", &settings.showWeapon);
            ui::Checkbox("Show Enchants", &settings.showEnchants);
            ui::Checkbox("Armor On Side", &settings.armorOnSide);
            ui::Checkbox("Show Effects", &settings.showEffects);
            if (ui::Checkbox("Hide Vanilla Names", &settings.hideVanilla)) {
                applyVanillaHide();
            }
        });
}

std::string NameTags::getDisplayInfo() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0fm", settings.range);
    return buf;
}

void NameTags::saveSettings(json::Value& node) const {
    node["range"] = settings.range;
    node["scale"] = settings.scale;
    node["background"] = settings.background;
    node["showHealth"] = settings.showHealth;
    node["showDistance"] = settings.showDistance;
    node["showArmor"] = settings.showArmor;
    node["showWeapon"] = settings.showWeapon;
    node["showEnchants"] = settings.showEnchants;
    node["armorOnSide"] = settings.armorOnSide;
    node["showEffects"] = settings.showEffects;
    node["hideVanilla"] = settings.hideVanilla;
    json::writeColor(node, "normalColor", settings.normalColor);
    json::writeColor(node, "friendColor", settings.friendColor);
}

void NameTags::loadSettings(const json::Value& node) {
    settings.range = node["range"].as_float(settings.range);
    settings.scale = node["scale"].as_float(settings.scale);
    settings.background = node["background"].as_bool(settings.background);
    settings.showHealth = node["showHealth"].as_bool(settings.showHealth);
    settings.showDistance = node["showDistance"].as_bool(settings.showDistance);
    settings.showArmor = node["showArmor"].as_bool(settings.showArmor);
    settings.showWeapon = node["showWeapon"].as_bool(settings.showWeapon);
    settings.showEnchants = node["showEnchants"].as_bool(settings.showEnchants);
    settings.armorOnSide = node["armorOnSide"].as_bool(settings.armorOnSide);
    settings.showEffects = node["showEffects"].as_bool(settings.showEffects);
    settings.hideVanilla = node["hideVanilla"].as_bool(settings.hideVanilla);
    json::readColor(node, "normalColor", settings.normalColor);
    json::readColor(node, "friendColor", settings.friendColor);
    applyVanillaHide();  // sync the patcher flag with the loaded setting
}
