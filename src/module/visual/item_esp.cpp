#include "item_esp.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/logger.hpp"

ItemESP* itemEsp = nullptr;

ItemESP::ItemESP() : Module("ItemESP") {
    keybind = 0;
    settings.box = true;
    settings.filled = true;
    settings.outline = true;
    settings.box3D = false;
    settings.showQuantity = true;
}

void ItemESP::onEnable() {
    log_ok("ItemESP enabled");
    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedItems.clear();
}

void ItemESP::onDisable() {
    log_ok("ItemESP disabled");
    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedItems.clear();
}

void ItemESP::onUpdate() {
    lastUpdateTick = GetTickCount64();

    if (!Java::Env() || !Java::Initialized)
        return;

    // Fetch items
    std::vector<ItemEntityInfo> items = mc.getItemEntityList();

    // Fetch render pos directly from MC (since we are on game thread)
    RenderPos rPos = mc.getRenderPos();
    float pTicks = mc.getPartialTicks();
    
    // Fetch local player for fallback interpolation logic (optional, but good for consistency)
    jobject localPlayer = mc.getPlayer();
    double lx = 0, ly = 0, lz = 0;
    double llx = 0, lly = 0, llz = 0;
    
    if (localPlayer) {
        if (JniCache::fid_posX) lx = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_posX);
        if (JniCache::fid_posY) ly = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_posY);
        if (JniCache::fid_posZ) lz = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_posZ);
        if (JniCache::fid_lastTickPosX) llx = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_lastTickPosX);
        if (JniCache::fid_lastTickPosY) lly = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_lastTickPosY);
        if (JniCache::fid_lastTickPosZ) llz = Java::Env()->GetDoubleField(localPlayer, JniCache::fid_lastTickPosZ);
        Java::Env()->DeleteLocalRef(localPlayer);
    }

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedItems = std::move(items);
        cachedRenderX = rPos.x;
        cachedRenderY = rPos.y;
        cachedRenderZ = rPos.z;
        cachedPartialTicks = pTicks;
        cachedLocalX = lx;
        cachedLocalY = ly;
        cachedLocalZ = lz;
        cachedLocalLastX = llx;
        cachedLocalLastY = lly;
        cachedLocalLastZ = llz;
    }
}

// Render 3D wireframe box (Adapted from esp.cpp)
static void Render3DBox(ImDrawList* draw, float x, float y, float z, double renderX, double renderY, double renderZ,
                        ImU32 boxCol, ImU32 outlineCol, ImU32 fillCol, bool drawOutline, bool drawFilled) {
    // Item Frame dimensions - Items are small 0.25 x 0.25 usually
    // Item Frame dimensions - Items are small 0.25 x 0.25 usually
    // User requested taller square box
    static constexpr float HALF_WIDTH = 0.25f;  // Width 0.5
    static constexpr float HEIGHT = 0.5f;       // Height 0.5 (Cube-ish/Tall)

    // 8 corners of the hitbox
    Vector3 corners[8] = {
        {x - HALF_WIDTH, y, z - HALF_WIDTH},           // 0: bottom back-left
        {x + HALF_WIDTH, y, z - HALF_WIDTH},           // 1: bottom back-right
        {x + HALF_WIDTH, y, z + HALF_WIDTH},           // 2: bottom front-right
        {x - HALF_WIDTH, y, z + HALF_WIDTH},           // 3: bottom front-left
        {x - HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH},  // 4: top back-left
        {x + HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH},  // 5: top back-right
        {x + HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH},  // 6: top front-right
        {x - HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH},  // 7: top front-left
    };

    // Project all corners to screen space
    Vector2 screenCorners[8];
    bool visible[8];
    int visibleCount = 0;
    for (int i = 0; i < 8; i++) {
        Vector3 v = {corners[i].x - (float)renderX, corners[i].y - (float)renderY, corners[i].z - (float)renderZ};
        visible[i] = CWorldToScreen::WorldToScreen(v, RenderData::modelView, RenderData::projection,
                                                   RenderData::screenWidth, RenderData::screenHeight, screenCorners[i]);
        if (visible[i])
            visibleCount++;
    }

    // Need at least 3 visible corners to render faces
    if (visibleCount < 3)
        return;

    static constexpr int faces[6][4] = {
        {0, 1, 2, 3}, {7, 6, 5, 4}, {3, 2, 6, 7}, {1, 0, 4, 5}, {0, 3, 7, 4}, {2, 1, 5, 6},
    };

    if (drawFilled) {
        for (int f = 0; f < 6; f++) {
            int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
            if (!visible[i0] || !visible[i1] || !visible[i2] || !visible[i3]) continue;

            float ax = screenCorners[i1].x - screenCorners[i0].x;
            float ay = screenCorners[i1].y - screenCorners[i0].y;
            float bx = screenCorners[i2].x - screenCorners[i0].x;
            float by = screenCorners[i2].y - screenCorners[i0].y;
            float cross = ax * by - ay * bx;

            if (cross > 0) {
                ImVec2 points[4] = {
                    ImVec2(screenCorners[i0].x, screenCorners[i0].y),
                    ImVec2(screenCorners[i1].x, screenCorners[i1].y),
                    ImVec2(screenCorners[i2].x, screenCorners[i2].y),
                    ImVec2(screenCorners[i3].x, screenCorners[i3].y),
                };
                draw->AddConvexPolyFilled(points, 4, fillCol);
            }
        }
    }

    static constexpr int edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, 
                                         {0, 4}, {1, 5}, {2, 6}, {3, 7}};

    if (drawOutline) {
        for (int i = 0; i < 12; i++) {
            int a = edges[i][0], b = edges[i][1];
            if (visible[a] && visible[b]) {
                draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y),
                              ImVec2(screenCorners[b].x, screenCorners[b].y), outlineCol, 2.0f);
            }
        }
    }

    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], b = edges[i][1];
        if (visible[a] && visible[b]) {
            draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y),
                          ImVec2(screenCorners[b].x, screenCorners[b].y), boxCol, 1.0f);
        }
    }
}

void ItemESP::onRender() {
    if (!RenderData::updated) return;
    if (GetTickCount64() - lastUpdateTick > 300) return;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (!draw) return;

    std::vector<ItemEntityInfo> localCache;
    double renderX, renderY, renderZ;
    float partialTicks;
    double localX, localY, localZ;
    double localLastX, localLastY, localLastZ;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        localCache = cachedItems;
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
    
    // Fallback
    bool useFallback = (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0);
    if (useFallback) {
        renderX = localLastX + (localX - localLastX) * partialTicks;
        renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;
        renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
    }

    auto outlineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.outlineColor[0], settings.outlineColor[1], settings.outlineColor[2], settings.outlineColor[3]));
    auto boxCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3]));
    auto fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.filledColor[0], settings.filledColor[1], settings.filledColor[2], settings.filledColor[3]));

    for (const auto& item : localCache) {
        // Distance check
        double dx = item.posX - localX;
        double dy = item.posY - localY;
        double dz = item.posZ - localZ;
        float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);

        if (dist > settings.range) continue;

        // Entities don't have lastTickPos in our struct yet, so we just use current pos
        // Or we could add lastTickPos to ItemEntityInfo if we want smooth interpolation
        // For items, they usually don't move fast unless sliding. 
        // For now, use static pos.
        float interpX = (float)item.posX; 
        float interpY = (float)item.posY;
        float interpZ = (float)item.posZ;

        if (settings.box3D) {
            Render3DBox(draw, interpX, interpY, interpZ, renderX, renderY, renderZ, boxCol, outlineCol, fillCol, settings.outline, settings.filled);
        }

        bool needs2DBox = settings.box;
        bool needs2DFilled = settings.filled && !settings.box3D;
        
        // Project center for Quantity Text
        // Item roughly 0.25 height
        Vector3 topPos = {interpX - (float)renderX, interpY + 0.5f - (float)renderY, interpZ - (float)renderZ};
        Vector3 bottomPos = {interpX - (float)renderX, interpY - (float)renderY, interpZ - (float)renderZ};
        
        Vector2 sTop, sBot;
        bool topVis = CWorldToScreen::WorldToScreen(topPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, sTop);
        bool botVis = CWorldToScreen::WorldToScreen(bottomPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, sBot);

        if (!topVis && !botVis) continue;

        if (needs2DBox || needs2DFilled) {
             float height = sBot.y - sTop.y;
             if (height < 1.0f) height = 1.0f;
             if (height > 500.0f) height = 500.0f; // clamp

             float width = height; // Square for items usually
             float left = sBot.x - width * 0.5f;
             float right = sBot.x + width * 0.5f;
             float top = sTop.y;
             float bottom = sBot.y;

             if (needs2DFilled) {
                 draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), fillCol);
             }
             if (needs2DBox) {
                 if (settings.outline) {
                     draw->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1), outlineCol);
                     draw->AddRect(ImVec2(left + 1, top + 1), ImVec2(right - 1, bottom - 1), outlineCol);
                 }
                 draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxCol);
             }
        }

        // Quantity (Show Quantity)
        if (settings.showQuantity && item.isStackable()) {
             std::string qty = "x" + std::to_string(item.stackSize);
             
             ImVec2 textSize = ImGui::CalcTextSize(qty.c_str());
             float tX = sBot.x - textSize.x * 0.5f; // Center horizontally
             float tY = sTop.y - textSize.y - 2.0f; // Above the box
             
             draw->AddText(ImVec2(tX + 1, tY + 1), IM_COL32(0, 0, 0, 255), qty.c_str()); // Shadow
             draw->AddText(ImVec2(tX, tY), IM_COL32(255, 255, 255, 255), qty.c_str());
        }
    }
}

void ItemESP::onRenderMenu() {
    bool prevBox2D = settings.box;
    bool prevBox3D = settings.box3D;

    ui::two_column(
        "Settings",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();
            ui::Checkbox("Box 2D", &settings.box);
            ui::Checkbox("Box 3D", &settings.box3D);
            ui::Checkbox("Filled", &settings.filled);
            ui::Checkbox("Outline", &settings.outline);
            ui::Checkbox("Show Quantity", &settings.showQuantity);
            ui::SliderFloat("Range", &settings.range, 16.0f, 256.0f, "%.0fm");
        },
        "Colors",
        [this]() {
            ui::color("Box Color", settings.boxColor);
            ui::color("Fill Color", settings.filledColor);
            ui::color("Outline Color", settings.outlineColor);
        });

    if (settings.box && !prevBox2D) settings.box3D = false;
    if (settings.box3D && !prevBox3D) settings.box = false;
}

std::string ItemESP::getDisplayInfo() {
    return "";
}
