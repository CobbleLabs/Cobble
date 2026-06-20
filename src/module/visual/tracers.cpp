#define IMGUI_DEFINE_MATH_OPERATORS
#include "tracers.h"
#include <algorithm>
#include <cmath>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/visual_cache.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"

Tracers* tracers = nullptr;

Tracers::Tracers() : Module("Tracers") {
    keybind = 0;
    renders = true;
    usesPlayerCache = true;
}

void Tracers::onEnable() {
    log_ok("Tracers enabled");
    lastWorldRef = nullptr;

    // Reset RenderData
    RenderData::initialized = false;
    RenderData::Reset();

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedPlayers.clear();
    smoothedPlayers.clear();
    lastRenderTime = 0;
}

void Tracers::onDisable() {
    log_ok("Tracers disabled");
    lastWorldRef = nullptr;

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedPlayers.clear();
    smoothedPlayers.clear();
    lastRenderTime = 0;
}

void Tracers::checkWorldChange() {
    if (!Java::Env() || !Java::Initialized)
        return;

    jobject currentWorld = mc.getWorld();
    bool worldChanged = false;

    if (lastWorldRef == nullptr && currentWorld != nullptr)
        worldChanged = true;
    else if (lastWorldRef != nullptr && currentWorld == nullptr)
        worldChanged = true;
    else if (lastWorldRef != nullptr && currentWorld != nullptr) {
        if (!Java::Env()->IsSameObject(lastWorldRef, currentWorld))
            worldChanged = true;
    }

    if (worldChanged) {
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            cachedPlayers.clear();
        }

        RenderData::initialized = false;
        RenderData::Reset();

        if (lastWorldRef) {
            Java::Env()->DeleteGlobalRef(lastWorldRef);
            lastWorldRef = nullptr;
        }

        if (currentWorld) {
            lastWorldRef = Java::Env()->NewGlobalRef(currentWorld);
        }
    }

    if (currentWorld)
        Java::Env()->DeleteLocalRef(currentWorld);
}

void Tracers::onUpdate() {
    lastUpdateTick = GetTickCount64();

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

    RenderPos renderPos = snap.renderPos;
    float partialTicks = snap.partialTicks;

    double localX = snap.localX;
    double localY = snap.localY;
    double localZ = snap.localZ;

    auto& players = snap.players;
    std::vector<TracerPlayer> newCache;

    for (auto& p : players) {
        double dx = p.posX - localX;
        double dy = p.posY - localY;
        double dz = p.posZ - localZ;
        float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist > settings.range || dist < 0.5f)
            continue;

        TracerPlayer cached;
        cached.entityId = p.entityId;
        cached.name = p.name;
        cached.posX = p.posX;
        cached.posY = p.posY;
        cached.posZ = p.posZ;
        cached.lastPosX = p.lastPosX;
        cached.lastPosY = p.lastPosY;
        cached.lastPosZ = p.lastPosZ;
        cached.distance = dist;
        cached.health = p.health;
        cached.valid = true;

        newCache.push_back(cached);
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedPlayers = std::move(newCache);
        cachedRenderX = renderPos.x;
        cachedRenderY = renderPos.y;
        cachedRenderZ = renderPos.z;
        cachedPartialTicks = partialTicks;
        cachedLocalX = localX;
        cachedLocalY = localY;
        cachedLocalZ = localZ;
        cachedLocalLastX = snap.localLastX;
        cachedLocalLastY = snap.localLastY;
        cachedLocalLastZ = snap.localLastZ;
    }
}

// Helper: Get color based on distance
static ImU32 GetDistanceColor(float distance, float maxRange) {
    float percent = 1.0f - (distance / maxRange);
    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 1.0f)
        percent = 1.0f;

    int r, g;
    if (percent > 0.5f) {
        r = (int)((1.0f - percent) * 2.0f * 255.0f);
        g = 255;
    } else {
        r = 255;
        g = (int)(percent * 2.0f * 255.0f);
    }

    return IM_COL32(r, g, 0, 255);
}

// Helper: Get color based on health
static ImU32 GetHealthColor(float health) {
    float percent = health / 20.0f;
    if (percent < 0.0f)
        percent = 0.0f;
    if (percent > 1.0f)
        percent = 1.0f;

    int r, g;
    if (percent > 0.5f) {
        r = (int)((1.0f - percent) * 2.0f * 255.0f);
        g = 255;
    } else {
        r = 255;
        g = (int)(percent * 2.0f * 255.0f);
    }

    return IM_COL32(r, g, 0, 255);
}

static ImU32 WithAlpha(ImU32 color, float alpha) {
    const ImU32 byteAlpha = (ImU32)((std::clamp)(alpha, 0.0f, 1.0f) * 255.0f);
    return (color & 0x00FFFFFFu) | (byteAlpha << 24);
}

void Tracers::onRender() {
    if (!RenderData::updated)
        return;

    // Prevent rendering if game loop is paused
    if (GetTickCount64() - lastUpdateTick > 300)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    std::vector<TracerPlayer> localCache;
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

    // Fallback logic for render position
    bool useFallback = (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0);
    if (useFallback) {
        renderX = localLastX + (localX - localLastX) * partialTicks;
        renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;  // Eye height
        renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
    }

    // Determine start position for tracers
    ImVec2 startPos;
    if (settings.lineFrom == 0) {
        // Center of screen
        startPos = ImVec2(RenderData::screenWidth / 2.0f, RenderData::screenHeight / 2.0f);
    } else {
        // Bottom center of screen
        startPos = ImVec2(RenderData::screenWidth / 2.0f, RenderData::screenHeight - 8.0f);
    }

    const long long now = GetTickCount64();
    const float frameSeconds =
        lastRenderTime > 0 ? (std::min)(0.05f, (float)(now - lastRenderTime) / 1000.0f) : 1.0f / 60.0f;
    lastRenderTime = now;
    const float positionAlpha = 1.0f - std::exp(-20.0f * frameSeconds);

    // Draw tracers to each player
    for (const auto& p : localCache) {
        if (!p.valid)
            continue;

        const double targetX = p.lastPosX + (p.posX - p.lastPosX) * partialTicks;
        const double targetY = p.lastPosY + (p.posY - p.lastPosY) * partialTicks;
        const double targetZ = p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks;
        auto& smooth = smoothedPlayers[p.entityId];
        const double jumpX = targetX - smooth.x;
        const double jumpY = targetY - smooth.y;
        const double jumpZ = targetZ - smooth.z;
        const bool teleported = jumpX * jumpX + jumpY * jumpY + jumpZ * jumpZ > 36.0;

        if (!smooth.initialized || teleported) {
            smooth.x = targetX;
            smooth.y = targetY;
            smooth.z = targetZ;
            smooth.initialized = true;
        } else {
            smooth.x += (targetX - smooth.x) * positionAlpha;
            smooth.y += (targetY - smooth.y) * positionAlpha;
            smooth.z += (targetZ - smooth.z) * positionAlpha;
        }
        smooth.lastSeen = now;

        const float interpX = (float)smooth.x;
        const float interpY = (float)smooth.y + 0.9f;
        const float interpZ = (float)smooth.z;

        // Convert to camera-relative coordinates
        Vector3 targetPos{interpX - (float)renderX, interpY - (float)renderY, interpZ - (float)renderZ};

        // Project to screen
        Vector2 screenPos;
        bool visible = CWorldToScreen::WorldToScreen(targetPos, RenderData::modelView, RenderData::projection,
                                                     RenderData::screenWidth, RenderData::screenHeight, screenPos);

        if (!visible)
            continue;

        // Determine color
        ImU32 color;
        if (settings.colorByHealth) {
            color = GetHealthColor(p.health);
        } else if (settings.colorByDistance) {
            color = GetDistanceColor(p.distance, settings.range);
        } else {
            // Use custom color
            color = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.tracerColor[0], settings.tracerColor[1],
                                                          settings.tracerColor[2], settings.tracerColor[3]));
        }

        const float distanceFade = (std::clamp)(1.0f - p.distance / (std::max)(settings.range, 1.0f), 0.25f, 0.9f);
        color = WithAlpha(color, distanceFade);

        const ImVec2 endPos(screenPos.x, screenPos.y);
        const float verticalSpan = endPos.y - startPos.y;
        const float bend = (std::min)(std::abs(endPos.x - startPos.x) * 0.08f, 24.0f);
        const float bendDirection = endPos.x < startPos.x ? -bend : bend;
        const ImVec2 controlA(startPos.x + bendDirection, startPos.y + verticalSpan * 0.33f);
        const ImVec2 controlB(endPos.x - bendDirection, startPos.y + verticalSpan * 0.72f);

        draw->AddBezierCubic(startPos, controlA, controlB, endPos, IM_COL32(0, 0, 0, 150),
                             settings.thickness + 2.5f);
        draw->AddBezierCubic(startPos, controlA, controlB, endPos, color, settings.thickness);
        draw->AddCircleFilled(endPos, (std::max)(1.75f, settings.thickness), color, 12);
        draw->AddCircle(endPos, (std::max)(2.5f, settings.thickness + 0.75f), IM_COL32(0, 0, 0, 150), 12, 1.0f);
    }

    for (auto it = smoothedPlayers.begin(); it != smoothedPlayers.end();) {
        if (now - it->second.lastSeen > 500)
            it = smoothedPlayers.erase(it);
        else
            ++it;
    }
}

void Tracers::onRenderMenu() {
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

            // Line start position
            const char* lineFromOptions[] = {"Center", "Bottom"};
            ui::Combo("Line From", &settings.lineFrom, lineFromOptions, 2);

            ui::SliderFloat("Thickness", &settings.thickness, 1.0f, 5.0f, "%.1fpx");
            ui::SliderFloat("Range", &settings.range, 16.0f, 256.0f, "%.0fm");
        },
        "Colors",
        [this]() {
            ui::Checkbox("Color by Distance", &settings.colorByDistance);
            ui::Checkbox("Color by Health", &settings.colorByHealth);

            if (!settings.colorByDistance && !settings.colorByHealth) {
                ui::color("Tracer Color", settings.tracerColor);
            }
        });

    // Make color modes mutually exclusive
    if (settings.colorByDistance) {
        settings.colorByHealth = false;
    }
}
