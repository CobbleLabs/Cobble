#include "nametags.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/visual_cache.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/http.hpp"

#include <cmath>
#include <cstdio>
#include <thread>
#include "../../../infra/ext/imgui/imgui.h"

NameTags* nameTags = nullptr;

NameTags::NameTags() : Module("NameTags") {
    keybind = 0;  // No default keybind
    renders = true;
    usesPlayerCache = true;
}

void NameTags::onEnable() {
    log_ok("NameTags enabled");
    debugCounter = 0;
    matrixDebugDone = false;
    lastWorldRef = nullptr;  // Force world change detection on first update

    // Reset RenderData to force fresh matrices
    RenderData::initialized = false;
    RenderData::initFailed = false;
    RenderData::initRetryCount = 0;
    RenderData::Reset();

    // Clear any stale cached players
    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedPlayers.clear();
}

void NameTags::onDisable() {
    log_ok("NameTags disabled");

    // Clean up world ref
    /*if (lastWorldRef && Java::Env) {
        Java::Env()->DeleteGlobalRef(lastWorldRef);
    }*/
    lastWorldRef = nullptr;

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedPlayers.clear();
    mojangCache.clear();
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
        }

        // Force RenderData to reinitialize
        RenderData::initialized = false;
        RenderData::initFailed = false;
        RenderData::initRetryCount = 0;
        RenderData::Reset();

        matrixDebugDone = false;

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
        cached.uuid = p.uuid;
        cached.posX = p.posX;
        cached.posY = p.posY;
        cached.posZ = p.posZ;
        cached.lastPosX = p.lastPosX;
        cached.lastPosY = p.lastPosY;
        cached.lastPosZ = p.lastPosZ;
        cached.distance = dist;
        cached.health = p.health;
        cached.maxHealth = p.maxHealth;
        cached.valid = true;

        // Mojang name resolution for obfuscated GameProfiles
        // Always lookup, but only override display name if different from server name
        if (!p.uuid.empty()) {
            auto& entry = mojangCache[p.uuid];

            if (entry.lookupDone) {
                // Compare with server name - only use Mojang if different
                if (!entry.realName.empty() && entry.realName != p.name) {
                    cached.mojangName = entry.realName;
                }
            } else if (!entry.lookupQueued) {
                // Queue async lookup
                entry.lookupQueued = true;
                std::string uuidCopy = p.uuid;

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
    debugCounter++;
    bool shouldLog = (debugCounter % 120 == 0);

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

    int renderedCount = 0;

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
            renderPlayerTag(draw, screenPos.x, screenPos.y, p.getDisplayName().c_str(), p.distance, p.health,
                            p.maxHealth);
            renderedCount++;
        }
    }

    // if (shouldLog) {
    // 	char buf[64];
    // 	snprintf(buf, sizeof(buf), "NameTags: %zu cached, %d rendered", localCache.size(), renderedCount);
    // 	log_debug(buf);
    // }
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

void NameTags::renderPlayerTag(ImDrawList* draw, float screenX, float screenY, const char* name, float distance,
                               float health, float maxHealth) {
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

    // Calculate sizes
    ImVec2 nameSize = ImGui::CalcTextSize(nameText);
    ImVec2 healthSize = ImGui::CalcTextSize(healthText);
    ImVec2 distSize = ImGui::CalcTextSize(distText);

    float totalWidth = nameSize.x + healthSize.x + distSize.x;
    float totalHeight = nameSize.y;  // Assumes all same font size

    float posX = screenX - totalWidth / 2.0f;
    float posY = screenY - totalHeight;

    // Background
    if (settings.background) {
        float paddingX = 6.0f * displayScale;
        float paddingY = 4.0f * displayScale;

        draw->AddRectFilled(ImVec2(posX - paddingX, posY - paddingY),
                            ImVec2(posX + totalWidth + paddingX, posY + totalHeight + paddingY), IM_COL32(0, 0, 0, 160),
                            5.0f);  // Soft rounding
    }

    // Draw Name (White)
    draw->AddText(ImVec2(posX + 1, posY + 1), IM_COL32(0, 0, 0, 255), nameText);  // Shadow
    draw->AddText(ImVec2(posX, posY), IM_COL32(255, 255, 255, 255), nameText);

    float currentX = posX + nameSize.x;

    // Draw Health (Dynamic Color)
    if (settings.showHealth) {
        ImU32 hpColor = GetHealthColor(health, maxHealth);
        draw->AddText(ImVec2(currentX + 1, posY + 1), IM_COL32(0, 0, 0, 255), healthText);  // Shadow
        draw->AddText(ImVec2(currentX, posY), hpColor, healthText);
        currentX += healthSize.x;
    }

    // Draw Distance (Gray)
    if (settings.showDistance) {
        draw->AddText(ImVec2(currentX + 1, posY + 1), IM_COL32(0, 0, 0, 255), distText);  // Shadow
        draw->AddText(ImVec2(currentX, posY), IM_COL32(200, 200, 200, 255), distText);
    }
}

void NameTags::onRenderMenu() {
    ui::two_column(
        "Settings",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();
            ui::SliderFloat("Scale", &settings.scale, 0.5f, 2.0f, "%.1f");
            ui::Checkbox("Background", &settings.background);
        },
        "Display",
        [this]() {
            ui::Checkbox("Show Health", &settings.showHealth);
            ui::Checkbox("Show Distance", &settings.showDistance);
        });
}

std::string NameTags::getDisplayInfo() {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0fm", settings.range);
    return buf;
}
