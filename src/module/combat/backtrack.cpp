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

static constexpr int MAX_TICKS = 20;
static constexpr int MIN_TICKS = 1;
static constexpr int MAX_LATENCY_MS = 500;
static constexpr int MIN_LATENCY_MS = 50;

Backtrack::Backtrack() : Module("Backtrack") {
    keybind = 0;
    renders = true;
}

void Backtrack::onEnable() {
    clearHistory();
    isBacktracking = false;
    isLagging = false;
    currentTargetUUID = "";
    lastTargetChange = std::chrono::steady_clock::now();
    delayedPacketCount = 0;
}

void Backtrack::onDisable() {
    // If lag based mode was active, flush any delayed packets
    if (isLagging) {
        flushDelayedPackets();
    }
    clearHistory();
    isBacktracking = false;
    isLagging = false;
}

bool Backtrack::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    // Axes: 271, 275, 258, 279, 286
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
            itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

void Backtrack::clearHistory() {
    entityHistory.clear();
}

void Backtrack::clearEntityHistory(const std::string& uuid) {
    entityHistory.erase(uuid);
}

void Backtrack::recordPosition(const PlayerInfo& target) {
    PositionSnapshot snap;
    snap.x = target.posX;
    snap.y = target.posY;
    snap.z = target.posZ;
    snap.hurtTime = target.hurtTime;
    snap.tickAge = 0;
    snap.timestamp = std::chrono::steady_clock::now();

    auto& history = entityHistory[target.uuid];
    history.push_back(snap);

    // Manual mode: limit by tick count
    if (settings.mode == BacktrackMode::Manual) {
        while (history.size() > static_cast<size_t>(settings.ticks)) {
            history.pop_front();
        }
    } else {
        // Lag Based mode: limit by latency (ms)
        auto now = std::chrono::steady_clock::now();
        while (!history.empty()) {
            auto age = std::chrono::duration<float, std::milli>(now - history.front().timestamp).count();
            if (age > static_cast<float>(settings.latency)) {
                history.pop_front();
            } else {
                break;
            }
        }
    }

    // Update tick age for all entries
    for (auto& s : history) {
        s.tickAge++;
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

    // Get history for current target
    auto it = entityHistory.find(currentTargetUUID);
    if (it == entityHistory.end() || it->second.empty()) {
        isBacktracking = false;
        return false;
    }

    const auto& history = it->second;
    auto now = std::chrono::steady_clock::now();

    // Search from OLDEST to newest (we want the most delayed position)
    for (const auto& snap : history) {
        // In Manual mode, check tick age
        if (settings.mode == BacktrackMode::Manual) {
            if (snap.tickAge > settings.ticks)
                continue;
        } else {
            // In Lag Based mode, check timestamp
            float age = std::chrono::duration<float, std::milli>(now - snap.timestamp).count();
            if (age > static_cast<float>(settings.latency))
                continue;
        }

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

bool Backtrack::shouldDelayPacket() const {
    if (!enabled) return false;
    if (settings.mode != BacktrackMode::LagBased) return false;
    if (!isLagging) return false;
    return true;
}

void Backtrack::onPacketDelayed() {
    delayedPacketCount++;
}

void Backtrack::flushDelayedPackets() {
    isLagging = false;
    delayedPacketCount = 0;
    // Actual packet flush is handled by BacktrackGate
}

const std::deque<PositionSnapshot>* Backtrack::getPositionHistory(const std::string& uuid) const {
    auto it = entityHistory.find(uuid);
    if (it != entityHistory.end()) {
        return &it->second;
    }
    return nullptr;
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
        isLagging = false;
        return;
    }

    double playerX = player.getPosX();
    double playerY = player.getPosY();
    double playerZ = player.getPosZ();

    // Get player list
    JNIEnv* env = Java::Env();
    if (!env)
        return;
    
    // Clear any pending JNI exceptions from previous operations
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

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

        // Consider targets within extended range
        if (dist < closestDist && dist < settings.maxDistance + 2.0) {
            closestDist = dist;
            closestTarget = &p;
        }
    }

    if (!closestTarget) {
        isBacktracking = false;
        isLagging = false;
        clearHistory();
        currentTargetUUID = "";
        return;
    }

    // Check if target changed
    if (closestTarget->uuid != currentTargetUUID) {
        clearHistory();
        currentTargetUUID = closestTarget->uuid;
        lastTargetChange = std::chrono::steady_clock::now();
        isLagging = false;
    }

    // Check cooldown after target change
    auto now = std::chrono::steady_clock::now();
    float timeSinceChange = std::chrono::duration<float>(now - lastTargetChange).count();
    if (timeSinceChange < settings.cooldown) {
        isBacktracking = false;
        isLagging = false;
        return;
    }

    // Record current position to history
    recordPosition(*closestTarget);

    // Try to find a backtracked position
    bool foundPosition = findBacktrackedPosition(playerX, playerY, playerZ,
                                                  closestTarget->posX, closestTarget->posY, closestTarget->posZ);

    // Lag Based mode: start lagging if we can benefit
    if (settings.mode == BacktrackMode::LagBased) {
        // Always store the current real position for the indicator
        currentRealX = closestTarget->posX;
        currentRealY = closestTarget->posY;
        currentRealZ = closestTarget->posZ;

        if (foundPosition && !isLagging) {
            isLagging = true;
            lagStartTime = now;
            log_debug("[Backtrack] Started lagging packets");
        }

        // Check if we've been lagging too long
        if (isLagging) {
            float lagDuration = std::chrono::duration<float, std::milli>(now - lagStartTime).count();
            if (lagDuration > static_cast<float>(settings.latency)) {
                flushDelayedPackets();
                log_debug("[Backtrack] Flushed packets (max latency reached)");
            }
        }

        // Stop lagging if target is now out of range
        if (isLagging && closestDist > settings.maxDistance) {
            flushDelayedPackets();
            log_debug("[Backtrack] Flushed packets (target out of range)");
        }
    }

    // Cleanup JNI GlobalRefs to prevent memory leak
    // Each PlayerInfo.instance is a GlobalRef that must be freed with DeleteGlobalRef
    for (auto& p : players) {
        if (p.instance) {
            env->DeleteGlobalRef(p.instance);
            p.instance = nullptr;
        }
    }

    // Clear any exceptions that occurred during iteration
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}


void Backtrack::renderBox3D(double x, double y, double z, unsigned int color, unsigned int fillColor, bool filled) {
    if (!RenderData::updated)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    RenderPos renderPos = mc.getRenderPos();
    double renderX = renderPos.x;
    double renderY = renderPos.y;
    double renderZ = renderPos.z;

    if (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0) {
        float partialTicks = mc.getPartialTicks();
        double lastX = player.getLastPosX();
        double lastY = player.getLastPosY();
        double lastZ = player.getLastPosZ();
        renderX = lastX + (player.getPosX() - lastX) * partialTicks;
        renderY = lastY + (player.getPosY() - lastY) * partialTicks + 1.62;
        renderZ = lastZ + (player.getPosZ() - lastZ) * partialTicks;
    }

    static constexpr float HALF_WIDTH = 0.3f;
    static constexpr float HEIGHT = 1.8f;

    float fx = static_cast<float>(x);
    float fy = static_cast<float>(y);
    float fz = static_cast<float>(z);

    Vector3 corners[8] = {
        {fx - HALF_WIDTH - static_cast<float>(renderX), fy - static_cast<float>(renderY), fz - HALF_WIDTH - static_cast<float>(renderZ)},
        {fx + HALF_WIDTH - static_cast<float>(renderX), fy - static_cast<float>(renderY), fz - HALF_WIDTH - static_cast<float>(renderZ)},
        {fx + HALF_WIDTH - static_cast<float>(renderX), fy - static_cast<float>(renderY), fz + HALF_WIDTH - static_cast<float>(renderZ)},
        {fx - HALF_WIDTH - static_cast<float>(renderX), fy - static_cast<float>(renderY), fz + HALF_WIDTH - static_cast<float>(renderZ)},
        {fx - HALF_WIDTH - static_cast<float>(renderX), fy + HEIGHT - static_cast<float>(renderY), fz - HALF_WIDTH - static_cast<float>(renderZ)},
        {fx + HALF_WIDTH - static_cast<float>(renderX), fy + HEIGHT - static_cast<float>(renderY), fz - HALF_WIDTH - static_cast<float>(renderZ)},
        {fx + HALF_WIDTH - static_cast<float>(renderX), fy + HEIGHT - static_cast<float>(renderY), fz + HALF_WIDTH - static_cast<float>(renderZ)},
        {fx - HALF_WIDTH - static_cast<float>(renderX), fy + HEIGHT - static_cast<float>(renderY), fz + HALF_WIDTH - static_cast<float>(renderZ)}
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

    if (visibleCount < 3)
        return;

    // Draw edges
    static constexpr int edges[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0},
        {4, 5}, {5, 6}, {6, 7}, {7, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7}
    };

    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], b = edges[i][1];
        if (visible[a] && visible[b]) {
            draw->AddLine(ImVec2(screen[a].x, screen[a].y), ImVec2(screen[b].x, screen[b].y), color, 2.0f);
        }
    }

    // Draw filled faces if enabled
    if (filled) {
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
            draw->AddConvexPolyFilled(points, 4, fillColor);
        }
    }
}

void Backtrack::renderManualModeVisuals() {
    if (!settings.renderPreviousTicks)
        return;
    if (currentTargetUUID.empty())
        return;

    ImU32 shadowCol = IM_COL32(
        static_cast<int>(settings.shadowColor[0] * 255),
        static_cast<int>(settings.shadowColor[1] * 255),
        static_cast<int>(settings.shadowColor[2] * 255),
        static_cast<int>(settings.shadowColor[3] * 255)
    );
    ImU32 shadowFillCol = IM_COL32(
        static_cast<int>(settings.shadowColor[0] * 255),
        static_cast<int>(settings.shadowColor[1] * 255),
        static_cast<int>(settings.shadowColor[2] * 255),
        40
    );

    // Render all historical positions for current target
    auto it = entityHistory.find(currentTargetUUID);
    if (it != entityHistory.end() && !it->second.empty()) {
        // Copy to avoid issues during iteration
        std::deque<PositionSnapshot> historyCopy = it->second;
        for (const auto& snap : historyCopy) {
            // Skip the most recent position (that's the current)
            if (snap.tickAge <= 1)
                continue;

            renderBox3D(snap.x, snap.y, snap.z, shadowCol, shadowFillCol, settings.filledBox);
        }
    }
}

void Backtrack::renderLagBasedModeVisuals() {
    if (!isBacktracking)
        return;
    if (currentTargetUUID.empty())
        return;

    // Ghost at LAGGED position (where they were) - shadow color
    ImU32 ghostCol = IM_COL32(
        static_cast<int>(settings.shadowColor[0] * 255),
        static_cast<int>(settings.shadowColor[1] * 255),
        static_cast<int>(settings.shadowColor[2] * 255),
        static_cast<int>(settings.shadowColor[3] * 255)
    );
    ImU32 ghostFillCol = IM_COL32(
        static_cast<int>(settings.shadowColor[0] * 255),
        static_cast<int>(settings.shadowColor[1] * 255),
        static_cast<int>(settings.shadowColor[2] * 255),
        50
    );
    renderBox3D(backtrackedX, backtrackedY, backtrackedZ, ghostCol, ghostFillCol, settings.filledBox);

    // Hitbox at REAL position (where to aim) - box color
    if (settings.renderServerPos) {
        ImU32 hitboxCol = IM_COL32(
            static_cast<int>(settings.boxColor[0] * 255),
            static_cast<int>(settings.boxColor[1] * 255),
            static_cast<int>(settings.boxColor[2] * 255),
            static_cast<int>(settings.boxColor[3] * 255)
        );
        ImU32 hitboxFillCol = IM_COL32(
            static_cast<int>(settings.boxColor[0] * 255),
            static_cast<int>(settings.boxColor[1] * 255),
            static_cast<int>(settings.boxColor[2] * 255),
            80
        );
        renderBox3D(currentRealX, currentRealY, currentRealZ, hitboxCol, hitboxFillCol, settings.filledBox);
    }
}

void Backtrack::onRender() {
    if (!enabled)
        return;
    if (!settings.showBacktrackBox)
        return;
    if (!player.isValid())
        return;
    if (!RenderData::updated)
        return;

    if (settings.mode == BacktrackMode::Manual) {
        renderManualModeVisuals();
        
        // In Manual mode, also render the primary backtrack position (target box)
        if (isBacktracking) {
            ImU32 col = IM_COL32(
                static_cast<int>(settings.boxColor[0] * 255),
                static_cast<int>(settings.boxColor[1] * 255),
                static_cast<int>(settings.boxColor[2] * 255),
                static_cast<int>(settings.boxColor[3] * 255)
            );
            ImU32 fillCol = IM_COL32(
                static_cast<int>(settings.boxColor[0] * 255),
                static_cast<int>(settings.boxColor[1] * 255),
                static_cast<int>(settings.boxColor[2] * 255),
                80
            );
            renderBox3D(backtrackedX, backtrackedY, backtrackedZ, col, fillCol, settings.filledBox);
        }
    } else {
        // Lag Based mode - rendering handled in renderLagBasedModeVisuals
        renderLagBasedModeVisuals();
    }

    // Status text
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    ImVec2 screenSize = ImGui::GetIO().DisplaySize;

    const char* statusText = nullptr;
    ImU32 statusColor = IM_COL32(255, 100, 100, 255);

    if (settings.mode == BacktrackMode::LagBased && isBacktracking) {
        statusText = "Backtrack";
        statusColor = IM_COL32(255, 165, 0, 255);  // Orange
    } else if (isBacktracking) {
        statusText = "Backtrack Active";
    }

    if (statusText) {
        ImVec2 textSize = ImGui::CalcTextSize(statusText);
        draw->AddText(
            ImVec2(screenSize.x / 2 - textSize.x / 2, screenSize.y / 2 + 60),
            statusColor,
            statusText
        );
    }
}

void Backtrack::onRenderMenu() {
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

            // Mode selector
            const char* modeNames[] = {"Manual", "Lag Based"};
            int modeIndex = static_cast<int>(settings.mode);
            if (ui::Combo("Mode", &modeIndex, modeNames, 2)) {
                settings.mode = static_cast<BacktrackMode>(modeIndex);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Mode-specific settings
            if (settings.mode == BacktrackMode::Manual) {
                ui::SliderInt("Ticks", &settings.ticks, MIN_TICKS, MAX_TICKS, "%d");
                ImGui::Spacing();
                ui::Checkbox("Render Previous Ticks", &settings.renderPreviousTicks);
            } else {
                ui::SliderInt("Latency", &settings.latency, MIN_LATENCY_MS, MAX_LATENCY_MS, "%d ms");
                ImGui::Spacing();
                ui::Checkbox("Render Server Pos", &settings.renderServerPos);
            }

            ImGui::Spacing();

            // Common settings
            ui::SliderFloat("Range", &settings.maxDistance, 2.0f, 6.0f, "%.1f blocks");
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
        },
        "Visuals",
        [this]() {
            ui::Checkbox("Show Box", &settings.showBacktrackBox);
            if (settings.showBacktrackBox) {
                ui::Checkbox("Filled", &settings.filledBox);
                ui::color("Box Color", settings.boxColor);

                if (settings.mode == BacktrackMode::Manual && settings.renderPreviousTicks) {
                    ui::color("Shadow Color", settings.shadowColor);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Status indicator
            if (settings.mode == BacktrackMode::LagBased && isLagging) {
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Lagging... (%d pkts)", delayedPacketCount);
            } else if (isBacktracking) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Active");
            } else {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Waiting...");
            }
        }
    );
}

std::string Backtrack::getDisplayInfo() {
    if (settings.mode == BacktrackMode::Manual) {
        return std::to_string(settings.ticks) + " ticks";
    } else {
        return std::to_string(settings.latency) + "ms";
    }
}
