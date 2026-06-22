#include "blockesp.h"
#include <Windows.h>
#include <cmath>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/json.hpp"

BlockESP* blockESP = nullptr;

// Minecraft block IDs (1.8.9)
enum BlockID {
    COAL_ORE = 16,
    IRON_ORE = 15,
    GOLD_ORE = 14,
    DIAMOND_ORE = 56,
    LAPIS_ORE = 21,
    EMERALD_ORE = 129,
    REDSTONE_ORE = 73,
    REDSTONE_ORE_LIT = 74,
    CHEST = 54
};

BlockESP::BlockESP() : Module("BlockESP") {
    keybind = 0;
    renders = true;
    usesPlayerCache = true;
}

void BlockESP::onEnable() {
    log_ok("BlockESP enabled");

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedBlocks.clear();
    lastScanTime = 0;  // Force immediate scan
}

void BlockESP::onDisable() {
    log_ok("BlockESP disabled");

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedBlocks.clear();
}

void BlockESP::getBlockColor(int blockId, float* outColor) {
    switch (blockId) {
        case DIAMOND_ORE:
            outColor[0] = 0.1f;
            outColor[1] = 0.7f;
            outColor[2] = 1.0f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case EMERALD_ORE:
            outColor[0] = 0.2f;
            outColor[1] = 0.7f;
            outColor[2] = 0.2f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case IRON_ORE:
            outColor[0] = 0.7f;
            outColor[1] = 0.4f;
            outColor[2] = 0.2f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case GOLD_ORE:
            outColor[0] = 0.7f;
            outColor[1] = 0.6f;
            outColor[2] = 0.2f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case LAPIS_ORE:
            outColor[0] = 0.2f;
            outColor[1] = 0.2f;
            outColor[2] = 0.9f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case COAL_ORE:
            outColor[0] = 0.1f;
            outColor[1] = 0.1f;
            outColor[2] = 0.1f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case REDSTONE_ORE:
        case REDSTONE_ORE_LIT:
            outColor[0] = 0.7f;
            outColor[1] = 0.1f;
            outColor[2] = 0.1f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        case CHEST:
            outColor[0] = 0.4f;
            outColor[1] = 0.23f;
            outColor[2] = 0.125f;
            outColor[3] = settings.opacity / 100.0f;
            break;
        default:
            outColor[0] = 1.0f;
            outColor[1] = 1.0f;
            outColor[2] = 1.0f;
            outColor[3] = settings.opacity / 100.0f;
            break;
    }
}

void BlockESP::scanBlocks() {
    if (!player.isValid())
        return;

    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();

    // Round to block coordinates
    int centerX = (int)floor(localX);
    int centerY = (int)floor(localY);
    int centerZ = (int)floor(localZ);

    std::vector<BlockData> newBlocks;
    int radius = (std::clamp)((int)settings.range, 8, 32);
    const int radiusSquared = radius * radius;

    // Scan a sphere instead of a full cube to avoid unnecessary JNI calls.
    for (int x = centerX - radius; x <= centerX + radius; x++) {
        const int dx = x - centerX;
        for (int y = (std::max)(0, centerY - radius); y <= (std::min)(255, centerY + radius); y++) {
            const int dy = y - centerY;
            for (int z = centerZ - radius; z <= centerZ + radius; z++) {
                const int dz = z - centerZ;
                if (dx * dx + dy * dy + dz * dz > radiusSquared)
                    continue;
                int blockId = mc.getBlockId(x, y, z);

                // Check if this is a block type we want to highlight
                bool shouldRender = false;
                if (settings.drawDiamond && blockId == DIAMOND_ORE)
                    shouldRender = true;
                else if (settings.drawEmerald && blockId == EMERALD_ORE)
                    shouldRender = true;
                else if (settings.drawIron && blockId == IRON_ORE)
                    shouldRender = true;
                else if (settings.drawGold && blockId == GOLD_ORE)
                    shouldRender = true;
                else if (settings.drawLapis && blockId == LAPIS_ORE)
                    shouldRender = true;
                else if (settings.drawCoal && blockId == COAL_ORE)
                    shouldRender = true;
                else if (settings.drawRedstone && (blockId == REDSTONE_ORE || blockId == REDSTONE_ORE_LIT))
                    shouldRender = true;
                else if (settings.drawChests && blockId == CHEST)
                    shouldRender = true;

                if (shouldRender) {
                    BlockData block;
                    block.blockId = blockId;
                    block.x = x;
                    block.y = y;
                    block.z = z;
                    getBlockColor(blockId, block.color);
                    block.valid = true;
                    newBlocks.push_back(block);
                }
            }
        }
    }

    // Update cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedBlocks = std::move(newBlocks);
    }

    // log_debug("[BlockESP] Scanned %d blocks", cachedBlocks.size());
}

void BlockESP::onUpdate() {
    lastUpdateTick = GetTickCount64();

    if (!player.isValid()) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedBlocks.clear();
        return;
    }

    auto snap = VisualCache::GetSnapshot();
    if (!snap.valid) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedBlocks.clear();
        return;
    }

    // Smart auto-rescan: movement, settings changes, or a periodic refresh.
    long long currentTime = GetTickCount64();
    double localX = player.getPosX();
    double localY = player.getPosY();
    double localZ = player.getPosZ();

    double dx = localX - lastScanX;
    double dy = localY - lastScanY;
    double dz = localZ - lastScanZ;
    double moveDistance = std::sqrt(dx * dx + dy * dy + dz * dz);

    unsigned int signature = (unsigned int)(std::clamp((int)settings.range, 8, 32) << 8);
    signature |= settings.drawDiamond ? 1u << 0 : 0;
    signature |= settings.drawEmerald ? 1u << 1 : 0;
    signature |= settings.drawIron ? 1u << 2 : 0;
    signature |= settings.drawGold ? 1u << 3 : 0;
    signature |= settings.drawLapis ? 1u << 4 : 0;
    signature |= settings.drawRedstone ? 1u << 5 : 0;
    signature |= settings.drawCoal ? 1u << 6 : 0;
    signature |= settings.drawChests ? 1u << 7 : 0;
    bool shouldRescan = moveDistance > 3.0 || currentTime - lastScanTime >= 1500 ||
                        signature != lastSettingsSignature;

    if (shouldRescan) {
        scanBlocks();
        lastScanTime = currentTime;
        lastScanX = localX;
        lastScanY = localY;
        lastScanZ = localZ;
        lastSettingsSignature = signature;
    }

    // Cache player position for render thread
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedLocalX = snap.localX;
        cachedLocalY = snap.localY;
        cachedLocalZ = snap.localZ;
        cachedLocalLastX = snap.localLastX;
        cachedLocalLastY = snap.localLastY;
        cachedLocalLastZ = snap.localLastZ;
        cachedRenderX = snap.renderPos.x;
        cachedRenderY = snap.renderPos.y;
        cachedRenderZ = snap.renderPos.z;
        cachedPartialTicks = snap.partialTicks;
    }
}

void BlockESP::onRender() {
    if (!RenderData::updated)
        return;

    // Prevent rendering if game loop is paused
    if (GetTickCount64() - lastUpdateTick > 300)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    std::vector<BlockData> localBlocks;
    double localX, localY, localZ;
    double localLastX, localLastY, localLastZ;
    double renderX, renderY, renderZ;
    float partialTicks;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        localBlocks = cachedBlocks;
        localX = cachedLocalX;
        localY = cachedLocalY;
        localZ = cachedLocalZ;
        localLastX = cachedLocalLastX;
        localLastY = cachedLocalLastY;
        localLastZ = cachedLocalLastZ;
        renderX = cachedRenderX;
        renderY = cachedRenderY;
        renderZ = cachedRenderZ;
        partialTicks = cachedPartialTicks;
    }

    // Fallback
    if (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0) {
        renderX = localLastX + (localX - localLastX) * partialTicks;
        renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;
        renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
    }

    // Render each block
    for (const auto& block : localBlocks) {
        if (!block.valid)
            continue;

        // Calculate alpha based on distance if enabled
        float alpha = block.color[3];
        if (settings.fadeWithDistance) {
            double dx = block.x - localX;
            double dy = block.y - localY;
            double dz = block.z - localZ;
            float distance = (float)std::sqrt(dx * dx + dy * dy + dz * dz);
            float fadeFactor = 1.0f - (std::min)(distance / settings.range, 1.0f);
            alpha = block.color[3] * fadeFactor;
        }

        // Block bounds (camera-relative)
        float minX = (float)(block.x - renderX);
        float minY = (float)(block.y - renderY);
        float minZ = (float)(block.z - renderZ);
        float maxX = minX + 1.0f;
        float maxY = minY + 1.0f;
        float maxZ = minZ + 1.0f;

        // 8 corners of the block
        Vector3 corners[8] = {
            {minX, minY, minZ},  // 0: bottom back-left
            {maxX, minY, minZ},  // 1: bottom back-right
            {maxX, minY, maxZ},  // 2: bottom front-right
            {minX, minY, maxZ},  // 3: bottom front-left
            {minX, maxY, minZ},  // 4: top back-left
            {maxX, maxY, minZ},  // 5: top back-right
            {maxX, maxY, maxZ},  // 6: top front-right
            {minX, maxY, maxZ}   // 7: top front-left
        };

        // Project corners to screen
        Vector2 screenCorners[8];
        bool visible[8];
        int visibleCount = 0;

        for (int i = 0; i < 8; i++) {
            visible[i] =
                CWorldToScreen::WorldToScreen(corners[i], RenderData::modelView, RenderData::projection,
                                              RenderData::screenWidth, RenderData::screenHeight, screenCorners[i]);
            if (visible[i])
                visibleCount++;
        }

        // Need at least 3 visible corners
        if (visibleCount < 3)
            continue;

        ImU32 color = IM_COL32((int)(block.color[0] * 255), (int)(block.color[1] * 255), (int)(block.color[2] * 255),
                               (int)(alpha * 0.45f * 255));
        ImU32 outline = IM_COL32((int)(block.color[0] * 255), (int)(block.color[1] * 255),
                                 (int)(block.color[2] * 255), (int)((std::min)(1.0f, alpha + 0.25f) * 255));

        // Draw the 6 faces of the cube
        // Bottom face (0-1-2-3)
        if (visible[0] && visible[1] && visible[2] && visible[3]) {
            ImVec2 points[4] = {{screenCorners[0].x, screenCorners[0].y},
                                {screenCorners[1].x, screenCorners[1].y},
                                {screenCorners[2].x, screenCorners[2].y},
                                {screenCorners[3].x, screenCorners[3].y}};
            draw->AddConvexPolyFilled(points, 4, color);
        }

        // Top face (4-5-6-7)
        if (visible[4] && visible[5] && visible[6] && visible[7]) {
            ImVec2 points[4] = {{screenCorners[4].x, screenCorners[4].y},
                                {screenCorners[5].x, screenCorners[5].y},
                                {screenCorners[6].x, screenCorners[6].y},
                                {screenCorners[7].x, screenCorners[7].y}};
            draw->AddConvexPolyFilled(points, 4, color);
        }

        // Front face (2-3-7-6)
        if (visible[2] && visible[3] && visible[7] && visible[6]) {
            ImVec2 points[4] = {{screenCorners[2].x, screenCorners[2].y},
                                {screenCorners[3].x, screenCorners[3].y},
                                {screenCorners[7].x, screenCorners[7].y},
                                {screenCorners[6].x, screenCorners[6].y}};
            draw->AddConvexPolyFilled(points, 4, color);
        }

        // Back face (0-1-5-4)
        if (visible[0] && visible[1] && visible[5] && visible[4]) {
            ImVec2 points[4] = {{screenCorners[0].x, screenCorners[0].y},
                                {screenCorners[1].x, screenCorners[1].y},
                                {screenCorners[5].x, screenCorners[5].y},
                                {screenCorners[4].x, screenCorners[4].y}};
            draw->AddConvexPolyFilled(points, 4, color);
        }

        // Left face (0-3-7-4)
        if (visible[0] && visible[3] && visible[7] && visible[4]) {
            ImVec2 points[4] = {{screenCorners[0].x, screenCorners[0].y},
                                {screenCorners[3].x, screenCorners[3].y},
                                {screenCorners[7].x, screenCorners[7].y},
                                {screenCorners[4].x, screenCorners[4].y}};
            draw->AddConvexPolyFilled(points, 4, color);
        }

        // Right face (1-2-6-5)
        if (visible[1] && visible[2] && visible[6] && visible[5]) {
            ImVec2 points[4] = {{screenCorners[1].x, screenCorners[1].y},
                                {screenCorners[2].x, screenCorners[2].y},
                                {screenCorners[6].x, screenCorners[6].y},
                                {screenCorners[5].x, screenCorners[5].y}};
            draw->AddConvexPolyFilled(points, 4, color);
        }

        static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
                                             {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& edge : edges) {
            if (visible[edge[0]] && visible[edge[1]]) {
                draw->AddLine({screenCorners[edge[0]].x, screenCorners[edge[0]].y},
                              {screenCorners[edge[1]].x, screenCorners[edge[1]].y}, outline, 1.5f);
            }
        }
    }
}

void BlockESP::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Block Types",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();

            ui::Checkbox("Diamond Ore", &settings.drawDiamond);
            ui::Checkbox("Emerald Ore", &settings.drawEmerald);
            ui::Checkbox("Iron Ore", &settings.drawIron);
            ui::Checkbox("Gold Ore", &settings.drawGold);
            ui::Checkbox("Lapis Ore", &settings.drawLapis);
            ui::Checkbox("Redstone Ore", &settings.drawRedstone);
            ui::Checkbox("Coal Ore", &settings.drawCoal);
            ui::Checkbox("Chests", &settings.drawChests);
        },
        "Settings",
        [this]() {
            ui::SliderFloat("Range", &settings.range, 8.0f, 32.0f, "%.0f blocks");
            ui::SliderInt("Opacity", &settings.opacity, 10, 100, "%d%%");
            ui::Checkbox("Fade with Distance", &settings.fadeWithDistance);
        });
}



void BlockESP::saveSettings(json::Value& node) const {
    node["drawDiamond"] = settings.drawDiamond;
    node["drawEmerald"] = settings.drawEmerald;
    node["drawIron"] = settings.drawIron;
    node["drawGold"] = settings.drawGold;
    node["drawLapis"] = settings.drawLapis;
    node["drawRedstone"] = settings.drawRedstone;
    node["drawCoal"] = settings.drawCoal;
    node["drawChests"] = settings.drawChests;
    node["range"] = settings.range;
    node["opacity"] = settings.opacity;
    node["fadeWithDistance"] = settings.fadeWithDistance;
}

void BlockESP::loadSettings(const json::Value& node) {
    settings.drawDiamond = node["drawDiamond"].as_bool(settings.drawDiamond);
    settings.drawEmerald = node["drawEmerald"].as_bool(settings.drawEmerald);
    settings.drawIron = node["drawIron"].as_bool(settings.drawIron);
    settings.drawGold = node["drawGold"].as_bool(settings.drawGold);
    settings.drawLapis = node["drawLapis"].as_bool(settings.drawLapis);
    settings.drawRedstone = node["drawRedstone"].as_bool(settings.drawRedstone);
    settings.drawCoal = node["drawCoal"].as_bool(settings.drawCoal);
    settings.drawChests = node["drawChests"].as_bool(settings.drawChests);
    settings.range = (std::clamp)(node["range"].as_float(settings.range), 8.0f, 32.0f);
    settings.opacity = (std::clamp)(node["opacity"].as_int(settings.opacity), 10, 100);
    settings.fadeWithDistance = node["fadeWithDistance"].as_bool(settings.fadeWithDistance);
}
