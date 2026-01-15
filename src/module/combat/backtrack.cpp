#include "backtrack.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"

#include <imgui.h>
#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

Backtrack* backtrack = nullptr;

Backtrack::Backtrack() : Module("Backtrack") {
    keybind = 0;
}

void Backtrack::onEnable() {
    clearHistory();
    isBacktracking = false;
    currentTargetUUID = "";
    lastTargetChange = std::chrono::steady_clock::now();
}

void Backtrack::onDisable() {
    clearHistory();
    isBacktracking = false;
}

bool Backtrack::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    // Axes: 271, 275, 258, 279, 286
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
            itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

void Backtrack::clearHistory() {
    positionHistory.clear();
}

void Backtrack::recordPosition(const PlayerInfo& target) {
    PositionSnapshot snap;
    snap.x = target.posX;
    snap.y = target.posY;
    snap.z = target.posZ;
    snap.hurtTime = target.hurtTime;
    snap.timestamp = std::chrono::steady_clock::now();
    
    positionHistory.push_back(snap);
    
    // Remove old entries beyond maxDelay
    auto now = std::chrono::steady_clock::now();
    while (!positionHistory.empty()) {
        auto age = std::chrono::duration<float, std::milli>(now - positionHistory.front().timestamp).count();
        if (age > (float)settings.maxDelay) {
            positionHistory.pop_front();
        } else {
            break;
        }
    }
}

bool Backtrack::findBacktrackedPosition(double playerX, double playerY, double playerZ,
                                         double currentX, double currentY, double currentZ) {
    // Check current distance
    double dx = currentX - playerX;
    double dy = currentY - playerY;
    double dz = currentZ - playerZ;
    double currentDist = sqrt(dx * dx + dy * dy + dz * dz);
    
    // Only work when target is in range
    bool inRange = currentDist >= settings.minDistance && currentDist <= settings.maxDistance;
    
    if (!inRange) {
        isBacktracking = false;
        return false;
    }
    
    // Target is in range - find the OLDEST valid position in history (most "delayed")
    if (positionHistory.empty()) {
        isBacktracking = false;
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    
    // Search from OLDEST to newest (we want the most delayed position)
    for (auto it = positionHistory.begin(); it != positionHistory.end(); ++it) {
        const PositionSnapshot& snap = *it;
        
        // Check age - must be within maxDelay
        float age = std::chrono::duration<float, std::milli>(now - snap.timestamp).count();
        if (age > (float)settings.maxDelay)
            continue;
        
        // Check hurt time
        int timeUntilCanHit = snap.hurtTime * 50;
        if (timeUntilCanHit > settings.maxHurtTime)
            continue;
        
        // Check distance of historical position
        double hdx = snap.x - playerX;
        double hdy = snap.y - playerY;
        double hdz = snap.z - playerZ;
        double histDist = sqrt(hdx * hdx + hdy * hdy + hdz * hdz);
        
        // Accept if historical position is also in range
        if (histDist >= settings.minDistance && histDist <= settings.maxDistance) {
            // Found the oldest valid backtrack position!
            backtrackedX = snap.x;
            backtrackedY = snap.y;
            backtrackedZ = snap.z;
            isBacktracking = true;
            return true;
        }
    }
    
    isBacktracking = false;
    return false;
}

void Backtrack::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Weapon only check
    if (settings.weaponOnly && !isHoldingWeapon()) {
        isBacktracking = false;
        return;
    }

    double playerX = player.getPosX();
    double playerY = player.getPosY();
    double playerZ = player.getPosZ();

    // Get closest target
    std::vector<PlayerInfo> players = mc.getPlayerList();
    
    double closestDist = 999.0;
    PlayerInfo* closestTarget = nullptr;
    
    for (auto& p : players) {
        if (p.isNPC || p.isInvisible)
            continue;
        
        double dx = p.posX - playerX;
        double dy = p.posY - playerY;
        double dz = p.posZ - playerZ;
        double dist = sqrt(dx * dx + dy * dy + dz * dz);
        
        // Only consider targets within extended range (maxDistance + some buffer)
        if (dist < closestDist && dist < settings.maxDistance + 2.0) {
            closestDist = dist;
            closestTarget = &p;
        }
    }

    if (!closestTarget) {
        isBacktracking = false;
        clearHistory();
        currentTargetUUID = "";
        return;
    }

    // Check if target changed
    if (closestTarget->uuid != currentTargetUUID) {
        clearHistory();
        currentTargetUUID = closestTarget->uuid;
        lastTargetChange = std::chrono::steady_clock::now();
    }

    // Check cooldown after target change
    auto now = std::chrono::steady_clock::now();
    float timeSinceChange = std::chrono::duration<float>(now - lastTargetChange).count();
    if (timeSinceChange < settings.cooldown) {
        isBacktracking = false;
        return;
    }

    // Record current position to history
    recordPosition(*closestTarget);

    // Try to find a backtracked position
    findBacktrackedPosition(playerX, playerY, playerZ,
                            closestTarget->posX, closestTarget->posY, closestTarget->posZ);
}

void Backtrack::onRender() {
    if (!enabled)
        return;
    if (!settings.showBacktrackBox)
        return;
    if (!isBacktracking)
        return;
    if (!player.isValid())
        return;
    if (!RenderData::updated)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    // Get render position (camera position for WorldToScreen)
    RenderPos renderPos = mc.getRenderPos();
    
    // If renderPos is invalid, use player eye position as fallback
    double renderX = renderPos.x;
    double renderY = renderPos.y;
    double renderZ = renderPos.z;
    
    if (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0) {
        float partialTicks = mc.getPartialTicks();
        double lastX = player.getLastPosX();
        double lastY = player.getLastPosY();
        double lastZ = player.getLastPosZ();
        renderX = lastX + (player.getPosX() - lastX) * partialTicks;
        renderY = lastY + (player.getPosY() - lastY) * partialTicks + 1.62;  // Eye height
        renderZ = lastZ + (player.getPosZ() - lastZ) * partialTicks;
    }

    // Player hitbox dimensions
    static constexpr float HALF_WIDTH = 0.3f;
    static constexpr float HEIGHT = 1.8f;

    float x = (float)backtrackedX;
    float y = (float)backtrackedY;
    float z = (float)backtrackedZ;

    // 8 corners of the hitbox (world space, then convert to camera-relative)
    Vector3 corners[8] = {
        {x - HALF_WIDTH - (float)renderX, y - (float)renderY, z - HALF_WIDTH - (float)renderZ},
        {x + HALF_WIDTH - (float)renderX, y - (float)renderY, z - HALF_WIDTH - (float)renderZ},
        {x + HALF_WIDTH - (float)renderX, y - (float)renderY, z + HALF_WIDTH - (float)renderZ},
        {x - HALF_WIDTH - (float)renderX, y - (float)renderY, z + HALF_WIDTH - (float)renderZ},
        {x - HALF_WIDTH - (float)renderX, y + HEIGHT - (float)renderY, z - HALF_WIDTH - (float)renderZ},
        {x + HALF_WIDTH - (float)renderX, y + HEIGHT - (float)renderY, z - HALF_WIDTH - (float)renderZ},
        {x + HALF_WIDTH - (float)renderX, y + HEIGHT - (float)renderY, z + HALF_WIDTH - (float)renderZ},
        {x - HALF_WIDTH - (float)renderX, y + HEIGHT - (float)renderY, z + HALF_WIDTH - (float)renderZ}
    };

    Vector2 screen[8];
    bool visible[8];
    int visibleCount = 0;
    
    for (int i = 0; i < 8; i++) {
        visible[i] = CWorldToScreen::WorldToScreen(corners[i], RenderData::modelView, RenderData::projection,
                                                    RenderData::screenWidth, RenderData::screenHeight, screen[i]);
        if (visible[i])
            visibleCount++;
    }

    // Need at least 3 visible corners
    if (visibleCount < 3)
        return;

    ImU32 col = IM_COL32(
        (int)(settings.boxColor[0] * 255),
        (int)(settings.boxColor[1] * 255),
        (int)(settings.boxColor[2] * 255),
        (int)(settings.boxColor[3] * 255)
    );
    
    // Draw edges
    static constexpr int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };
    
    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], b = edges[i][1];
        if (visible[a] && visible[b]) {
            draw->AddLine(
                ImVec2(screen[a].x, screen[a].y),
                ImVec2(screen[b].x, screen[b].y),
                col, 2.0f
            );
        }
    }

    // Draw filled faces if enabled
    if (settings.filledBox) {
        ImU32 fillCol = IM_COL32(
            (int)(settings.boxColor[0] * 255),
            (int)(settings.boxColor[1] * 255),
            (int)(settings.boxColor[2] * 255),
            80
        );
        
        // 6 faces
        static constexpr int faces[6][4] = {
            {0, 1, 2, 3}, {7, 6, 5, 4}, {3, 2, 6, 7},
            {1, 0, 4, 5}, {0, 3, 7, 4}, {2, 1, 5, 6}
        };
        
        for (int f = 0; f < 6; f++) {
            int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
            if (!visible[i0] || !visible[i1] || !visible[i2] || !visible[i3])
                continue;
            
            ImVec2 points[4] = {
                ImVec2(screen[i0].x, screen[i0].y),
                ImVec2(screen[i1].x, screen[i1].y),
                ImVec2(screen[i2].x, screen[i2].y),
                ImVec2(screen[i3].x, screen[i3].y)
            };
            draw->AddConvexPolyFilled(points, 4, fillCol);
        }
    }

    // Status text
    char buf[64];
    snprintf(buf, sizeof(buf), "Backtrack Active");
    
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    
    draw->AddText(
        ImVec2(screenSize.x / 2 - textSize.x / 2, screenSize.y / 2 + 60),
        IM_COL32(255, 100, 100, 255), buf
    );
}

void Backtrack::onRenderMenu() {
    ui::two_column(
        "Settings",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();

            // Main setting - how far back to look
            ui::SliderInt("Delay", &settings.maxDelay, 50, 500, "%d ms");
            
            ImGui::Spacing();
            
            // Range setting  
            ui::SliderFloat("Range", &settings.maxDistance, 2.0f, 6.0f, "%.1f blocks");
        },
        "Visuals",
        [this]() {
            ui::Checkbox("Show Box", &settings.showBacktrackBox);
            if (settings.showBacktrackBox) {
                ui::Checkbox("Filled", &settings.filledBox);
                ui::color("Color", settings.boxColor);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Status indicator
            if (isBacktracking) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Active");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
            }
        });
}

