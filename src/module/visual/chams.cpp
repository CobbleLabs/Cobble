#include "chams.h"

#include <algorithm>
#include <cmath>

#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/json.hpp"

Chams* chams = nullptr;

Chams::Chams() : Module("Chams") {
    keybind = 0;
    renders = true;          // Enable onRender() for 2D overlay
    usesPlayerCache = true;
}

// ---------------------------------------------------------------------------
// 2D helpers (same approach as pings — projects bounding box to screen space)
// ---------------------------------------------------------------------------

// Project a player-sized bounding box to screen-space bounds.
static bool GetPlayerScreenBounds(float x, float y, float z, double renderX, double renderY, double renderZ,
                                  ImVec2& outMin, ImVec2& outMax) {
    static constexpr float HALF_WIDTH = 0.34f;
    static constexpr float HEIGHT = 1.86f;
    const Vector3 corners[8] = {
        {x - HALF_WIDTH, y, z - HALF_WIDTH}, {x + HALF_WIDTH, y, z - HALF_WIDTH},
        {x + HALF_WIDTH, y, z + HALF_WIDTH}, {x - HALF_WIDTH, y, z + HALF_WIDTH},
        {x - HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH}, {x + HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH},
        {x + HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH}, {x - HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH}
    };

    float minX = 100000.0f, minY = 100000.0f;
    float maxX = -100000.0f, maxY = -100000.0f;
    int projected = 0;
    for (const Vector3& corner : corners) {
        Vector2 screen;
        Vector3 relative = {corner.x - (float)renderX, corner.y - (float)renderY, corner.z - (float)renderZ};
        if (!CWorldToScreen::WorldToScreen(relative, RenderData::modelView, RenderData::projection,
                                           RenderData::screenWidth, RenderData::screenHeight, screen)) {
            continue;
        }

        minX = (std::min)(minX, screen.x);
        minY = (std::min)(minY, screen.y);
        maxX = (std::max)(maxX, screen.x);
        maxY = (std::max)(maxY, screen.y);
        ++projected;
    }

    if (projected < 2 || maxX - minX < 4.0f || maxY - minY < 8.0f)
        return false;

    outMin = ImVec2(minX, minY);
    outMax = ImVec2(maxX, maxY);
    return true;
}

// Draw a filled + outlined player box on screen (visible through walls).
static void DrawChamsBox(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 fillColor, ImU32 outlineColor) {
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    const float padding = (std::max)(2.0f, (std::min)(width, height) * 0.04f);
    min.x -= padding;
    min.y -= padding;
    max.x += padding;
    max.y += padding;

    const float rounding = (std::min)(4.0f, (std::min)(width, height) * 0.08f);

    // Filled interior
    draw->AddRectFilled(min, max, fillColor, rounding);

    // Glow outlines (layered for a soft glow like pings focus)
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(outlineColor);
    auto alpha = [&](float mul) {
        return ImGui::ColorConvertFloat4ToU32(ImVec4(base.x, base.y, base.z, base.w * mul));
    };

    draw->AddRect(min, max, alpha(0.15f), rounding, 0, 5.0f);
    draw->AddRect(min, max, alpha(0.35f), rounding, 0, 2.5f);
    draw->AddRect(min, max, alpha(0.95f), rounding, 0, 1.25f);
}

// ---------------------------------------------------------------------------
// onRender — 2D overlay (Color mode), drawn through walls via ImGui
// ---------------------------------------------------------------------------
void Chams::onRender() {
    if (!enabled)
        return;

    if (!RenderData::updated || !mc.isValid())
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    const auto snap = VisualCache::GetSnapshot();
    if (!snap.valid)
        return;

    RenderPos renderPos = mc.getRenderPos();
    const double renderX = renderPos.x;
    const double renderY = renderPos.y;
    const double renderZ = renderPos.z;

    const float partial = std::clamp(snap.partialTicks, 0.0f, 1.0f);
    const double localX = snap.localLastX + (snap.localX - snap.localLastX) * partial;
    const double localY = snap.localLastY + (snap.localY - snap.localLastY) * partial;
    const double localZ = snap.localLastZ + (snap.localZ - snap.localLastZ) * partial;
    const double maxDistanceSq = static_cast<double>(settings.range) * settings.range;

    const float alpha = std::clamp(settings.color[3], 0.05f, 1.0f);
    const ImU32 fillColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(settings.color[0], settings.color[1], settings.color[2], alpha * 0.45f));
    const ImU32 outlineColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(settings.color[0], settings.color[1], settings.color[2], alpha));

    for (const auto& target : snap.players) {
        if (target.entityId <= 0 || target.health <= 0.0f)
            continue;
        if (target.isInvisible && !settings.drawInvisiblePlayers)
            continue;

        const float x = (float)(target.lastPosX + (target.posX - target.lastPosX) * partial);
        const float y = (float)(target.lastPosY + (target.posY - target.lastPosY) * partial);
        const float z = (float)(target.lastPosZ + (target.posZ - target.lastPosZ) * partial);
        const double dx = (double)x - localX;
        const double dy = (double)y - localY;
        const double dz = (double)z - localZ;
        if (dx * dx + dy * dy + dz * dz > maxDistanceSq)
            continue;

        ImVec2 boundsMin, boundsMax;
        if (GetPlayerScreenBounds(x, y, z, renderX, renderY, renderZ, boundsMin, boundsMax)) {
            DrawChamsBox(draw, boundsMin, boundsMax, fillColor, outlineColor);
        }
    }
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------
void Chams::onRenderMenu() {
    bool enabledValue = enabled;

    ui::two_column(
        "Settings",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);

            ui::Checkbox("Draw Invisibles", &settings.drawInvisiblePlayers);
            ui::SliderFloat("Range", &settings.range, 16.0f, 255.0f, "%.0fm");
        },
        "Appearance",
        [this]() {
            ui::color("Color", settings.color, false);
            ui::SliderFloat("Opacity", &settings.color[3], 0.05f, 1.0f, "%.2f");
        });
}

std::string Chams::getDisplayInfo() {
    return enabled ? "Enabled" : "Disabled";
}

void Chams::saveSettings(json::Value& node) const {
    node["drawInvisiblePlayers"] = settings.drawInvisiblePlayers;
    node["range"] = settings.range;
    json::writeColor(node, "color", settings.color);
}

void Chams::loadSettings(const json::Value& node) {
    settings.drawInvisiblePlayers = node["drawInvisiblePlayers"].as_bool(settings.drawInvisiblePlayers);
    settings.range = std::clamp(node["range"].as_float(settings.range), 16.0f, 255.0f);
    json::readColor(node, "color", settings.color);
    settings.color[3] = std::clamp(settings.color[3], 0.05f, 1.0f);
}
