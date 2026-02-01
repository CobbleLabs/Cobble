#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "../../jni/java.h"
#include "../../sdk/core/minecraft.h"
#include "../module.h"

// Forward declare
struct ImDrawList;

// Cached player data - stores world positions for interpolation
struct CachedPlayer {
    std::string name;                                 // Display name (may be obfuscated)
    std::string uuid;                                 // Player UUID for Mojang lookup
    std::string mojangName;                           // Real name from Mojang API (empty if not resolved)
    double posX = 0, posY = 0, posZ = 0;              // Current position
    double lastPosX = 0, lastPosY = 0, lastPosZ = 0;  // Last tick position
    float distance = 0;
    float health = 20.0f;
    float maxHealth = 20.0f;
    bool valid = false;
    std::vector<PlayerInfo::ArmorItem> armor;
    std::vector<PlayerInfo::PotionEffectInfo> potions;


    // Get display name (prefers Mojang real name if available)
    const std::string& getDisplayName() const { return !mojangName.empty() ? mojangName : name; }
};

class NameTags : public Module {
public:
    NameTags();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;

    struct Settings {
        float range = 64.0f;
        float scale = 1.0f;
        bool background = true;
        bool showHealth = true;
        bool showDistance = true;
        bool showArmor = true;
        bool showEffects = true;
    } settings;

private:
    int debugCounter = 0;
    bool matrixDebugDone = false;

    // Thread-safe player cache
    std::mutex cacheMutex;
    std::vector<CachedPlayer> cachedPlayers;
    double cachedRenderX = 0, cachedRenderY = 0, cachedRenderZ = 0;
    float cachedPartialTicks = 0.0f;

    // Local player position for fallback (when renderPos unavailable in 1.7.10)
    double cachedLocalX = 0, cachedLocalY = 0, cachedLocalZ = 0;
    double cachedLocalLastX = 0, cachedLocalLastY = 0, cachedLocalLastZ = 0;

    // World change detection
    jobject lastWorldRef = nullptr;

    // Mojang API name resolution cache (persists across cache updates)
    // Maps UUID -> real Mojang name
    struct MojangEntry {
        std::string realName;
        bool lookupDone = false;
        bool lookupQueued = false;
    };
    std::map<std::string, MojangEntry> mojangCache;
    void checkWorldChange();

    void renderPlayerTag(ImDrawList* draw, float screenX, float screenY, const char* name, float distance, float health,
                         float maxHealth, const std::vector<PlayerInfo::ArmorItem>& armor,
                         const std::vector<PlayerInfo::PotionEffectInfo>& potions);
};

extern NameTags* nameTags;
