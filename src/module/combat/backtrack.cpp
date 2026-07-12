#include "backtrack.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/json.hpp"
#include "../../util/team/friends_manager.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <random>
#include <string>

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

// ============================================================================
// Ghost silhouette + animated status text helpers.
// ============================================================================
namespace {

struct RenderOrigin {
    double x, y, z;
};

// Interpolated camera/render origin (same math the box renderer used).
RenderOrigin getRenderOrigin() {
    RenderPos rp = mc.getRenderPos();
    double rX = rp.x, rY = rp.y, rZ = rp.z;
    if (rX == 0.0 && rY == 0.0 && rZ == 0.0) {
        float pt = mc.getPartialTicks();
        double lx = player.getLastPosX(), ly = player.getLastPosY(), lz = player.getLastPosZ();
        rX = lx + (player.getPosX() - lx) * pt;
        rY = ly + (player.getPosY() - ly) * pt + 1.62;
        rZ = lz + (player.getPosZ() - lz) * pt;
    }
    return {rX, rY, rZ};
}

bool projectPoint(const RenderOrigin& o, double x, double y, double z, Vector2& out) {
    Vector3 rel{static_cast<float>(x - o.x), static_cast<float>(y - o.y), static_cast<float>(z - o.z)};
    return CWorldToScreen::WorldToScreen(rel, RenderData::modelView, RenderData::projection,
                                         RenderData::screenWidth, RenderData::screenHeight, out);
}

// A soft humanoid silhouette (body capsule + head + glow) standing at a world
// position. Stacked with fading alpha along the target's recent path, a series of
// these reads as a motion-blur "replica" of the player moving, instead of a dry
// wireframe box.
void drawGhost(ImDrawList* draw, const RenderOrigin& o, double x, double y, double z, int r, int g, int b,
               float alpha, bool solid) {
    Vector2 foot, head;
    if (!projectPoint(o, x, y, z, foot))
        return;
    if (!projectPoint(o, x, y + 1.8, z, head))
        return;

    float h = foot.y - head.y;  // on-screen height (foot below head)
    if (h < 3.0f || h > 6000.0f)
        return;

    int A = static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
    if (A <= 1)
        return;

    float cx = (foot.x + head.x) * 0.5f;
    float w = h * 0.28f;   // body width
    float hr = w * 0.46f;  // head radius
    float headCy = head.y + hr;
    float bodyTop = headCy + hr * 0.55f;
    float bodyBot = foot.y;

    ImU32 col = IM_COL32(r, g, b, A);
    ImU32 glow = IM_COL32(r, g, b, static_cast<int>(A * 0.30f));

    // Soft glow underlay (wider) — this is what kills the "dry" look.
    float gw = w * 1.7f;
    draw->AddRectFilled(ImVec2(cx - gw * 0.5f, bodyTop - hr * 0.4f), ImVec2(cx + gw * 0.5f, bodyBot + hr * 0.3f),
                        glow, gw * 0.5f);
    draw->AddCircleFilled(ImVec2(cx, headCy), hr * 1.55f, glow);

    // Body capsule + head.
    draw->AddRectFilled(ImVec2(cx - w * 0.5f, bodyTop), ImVec2(cx + w * 0.5f, bodyBot), col, w * 0.5f);
    draw->AddCircleFilled(ImVec2(cx, headCy), hr, col);

    if (solid) {
        // Crisp rim on the primary (hittable) silhouette.
        ImU32 rim = IM_COL32(255, 255, 255, static_cast<int>(A * 0.55f));
        draw->AddRect(ImVec2(cx - w * 0.5f, bodyTop), ImVec2(cx + w * 0.5f, bodyBot), rim, w * 0.5f, 0, 1.4f);
        draw->AddCircle(ImVec2(cx, headCy), hr, rim, 0, 1.4f);
    }
}

// Persistent animation state for the status text (single Backtrack instance).
float g_statusAnim = 0.0f;
std::string g_lastInfo;

// Animated, letter-spaced, glowing "BACKTRACK" with a growing underline and the
// live amount below. Fades/slides in and out via `anim` (0..1). No pill.
void drawBacktrackStatus(float anim) {
    if (anim <= 0.01f)
        return;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    const ImVec2 ss = ImGui::GetIO().DisplaySize;
    ImFont* f = font(2);
    if (!f)
        return;

    float ease = anim * anim * (3.0f - 2.0f * anim);  // smoothstep
    int a = static_cast<int>(ease * 255.0f);

    const char* label = "Backtrack";
    const int N = static_cast<int>(std::char_traits<char>::length(label));
    float fs = ui::s(19.0f);
    float spacing = ui::s(1.0f);  // letter spacing

    float widths[16] = {0};
    float totalW = 0.0f;
    for (int i = 0; i < N && i < 16; ++i) {
        char ch[2] = {label[i], 0};
        widths[i] = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, ch).x;
        totalW += widths[i] + spacing;
    }
    totalW -= spacing;
    float lineH = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label).y;

    float cx = ss.x * 0.5f;
    float baseY = ss.y * 0.5f + ui::s(52.0f);  // sits lower on screen
    float startX = cx - totalW * 0.5f;

    // Staggered left->right reveal: each letter fades + rises into place, so it
    // "types" in and unwinds the same way on exit — a different feel to the old
    // centre-out pop.
    float x = startX;
    float go = ui::s(1.5f);
    for (int i = 0; i < N && i < 16; ++i) {
        char ch[2] = {label[i], 0};
        float frac = (N > 1) ? static_cast<float>(i) / static_cast<float>(N - 1) : 0.0f;
        float lt = std::clamp((anim - frac * 0.5f) / 0.5f, 0.0f, 1.0f);
        lt = lt * lt * (3.0f - 2.0f * lt);  // smoothstep
        int la = static_cast<int>(lt * 255.0f);
        if (la > 3) {
            float rise = (1.0f - lt) * ui::s(7.0f);
            ImVec2 pos(x, baseY + rise);
            ImU32 amber = IM_COL32(255, 178, 66, la);
            ImU32 glow = IM_COL32(255, 150, 40, static_cast<int>(la * 0.24f));
            ImU32 shadow = IM_COL32(0, 0, 0, static_cast<int>(la * 0.8f));
            draw->AddText(f, fs, ImVec2(pos.x - go, pos.y), glow, ch);
            draw->AddText(f, fs, ImVec2(pos.x + go, pos.y), glow, ch);
            draw->AddText(f, fs, ImVec2(pos.x, pos.y - go), glow, ch);
            draw->AddText(f, fs, ImVec2(pos.x, pos.y + go), glow, ch);
            draw->AddText(f, fs, ImVec2(pos.x + ui::s(1.0f), pos.y + ui::s(1.0f)), shadow, ch);
            draw->AddText(f, fs, pos, amber, ch);
        }
        x += widths[i] + spacing;
    }

    // Accent line under the text, wiping left->right in sync with the reveal.
    float ulY = baseY + lineH + ui::s(5.0f);
    float ulH = ui::s(2.0f);
    float ulW = totalW * ease;
    draw->AddRectFilled(ImVec2(startX, ulY), ImVec2(startX + ulW, ulY + ulH), IM_COL32(255, 178, 66, a), ulH * 0.5f);

    // Live amount (ticks / ms) centred below the line.
    if (!g_lastInfo.empty()) {
        ImFont* fr = font(5) ? font(5) : f;  // Inter Regular
        float ifs = ui::s(12.0f);
        ImVec2 isz = fr->CalcTextSizeA(ifs, FLT_MAX, 0.0f, g_lastInfo.c_str());
        float ix = cx - isz.x * 0.5f;
        float iy = ulY + ui::s(6.0f);
        draw->AddText(fr, ifs, ImVec2(ix + ui::s(1.0f), iy + ui::s(1.0f)),
                      IM_COL32(0, 0, 0, static_cast<int>(ease * 175.0f)), g_lastInfo.c_str());
        draw->AddText(fr, ifs, ImVec2(ix, iy), IM_COL32(185, 190, 200, static_cast<int>(ease * 215.0f)),
                      g_lastInfo.c_str());
    }
}

}  // namespace

Backtrack::Backtrack() : Module("Backtrack") {
    keybind = 0;
    renders = true;
}

void Backtrack::onEnable() {
    clearHistory();
    isBacktracking = false;
    wasBacktracking = false;
    isLagging = false;
    currentTargetUUID = "";
    lastTargetChange = std::chrono::steady_clock::now();
    delayedPacketCount = 0;
    rollLatency();
}

void Backtrack::onDisable() {
    // If lag based mode was active, flush any delayed packets
    if (isLagging) {
        flushDelayedPackets();
    }
    clearHistory();
    isBacktracking = false;
    wasBacktracking = false;
    isLagging = false;
    ghostEntityId = -1;
}

bool Backtrack::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    // Axes: 271, 275, 258, 279, 286
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
            itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

void Backtrack::rollLatency() {
    int lo = settings.latencyMin;
    int hi = settings.latencyMax;
    if (lo > hi)
        std::swap(lo, hi);
    if (hi <= lo) {
        currentLatency = lo;
        return;
    }
    static std::mt19937 rng(static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> d(lo, hi);
    currentLatency = d(rng);
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
            // Keep enough history to cover the widest possible window.
            if (age > static_cast<float>(settings.latencyMax)) {
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
            // In Lag Based mode, check timestamp against this session's rolled latency
            float age = std::chrono::duration<float, std::milli>(now - snap.timestamp).count();
            if (age > static_cast<float>(currentLatency))
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

bool Backtrack::getGhostRenderTarget(int& entityId, double& x, double& y, double& z) const {
    if (!enabled || !isBacktracking || ghostEntityId <= 0)
        return false;
    entityId = ghostEntityId;
    x = backtrackedX;
    y = backtrackedY;
    z = backtrackedZ;
    return true;
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

    // Cache local team color (refreshed every second)
    static int localTeamColor = -1;
    static unsigned long lastColorRefresh = 0;
    unsigned long nowTick = GetTickCount();
    if (nowTick - lastColorRefresh > 1000) {
        localTeamColor = mc.getLocalPlayerTeamColor();
        lastColorRefresh = nowTick;
    }
    static std::string localPlayerName;
    static unsigned long lastNameRefresh = 0;
    if (localPlayerName.empty() || nowTick - lastNameRefresh > 1000) {
        localPlayerName = mc.getLocalPlayerName();
        lastNameRefresh = nowTick;
    }

    double closestDist = 999.0;
    PlayerInfo* closestTarget = nullptr;

    for (auto& p : players) {
        if (p.isNPC || p.isInvisible)
            continue;

        // Skip friends and teammates
        if (friendsManager) {
            // Friends list
            bool isFriendByUUID = !p.uuid.empty() && friendsManager->isFriendByUUID(p.uuid);
            bool isFriendByName = friendsManager->isFriend(FriendsManager::stripColorCodes(p.name));
            if (isFriendByUUID || isFriendByName)
                continue;

            // Teammates (local scoreboard, Apollo/Lunar)
            if (p.isOnLocalTeam)
                continue;
            if (!p.uuid.empty() && friendsManager->isLunarTeammate(p.uuid))
                continue;
            if (!p.uuid.empty() && friendsManager->isApolloTeammate(p.uuid))
                continue;

            // Color-based scoreboard team
            ChatColor playerColor = static_cast<ChatColor>(p.teamColor);
            ChatColor localColor  = static_cast<ChatColor>(localTeamColor);
            if (playerColor != ChatColor::WHITE && playerColor != ChatColor::GRAY &&
                playerColor != ChatColor::UNKNOWN) {
                if (friendsManager->isScoreboardTeammate(localPlayerName,
                                                         p.profileName.empty() ? p.name : p.profileName))
                    continue;
            }
            if (friendsManager->isTeammate(p.uuid, playerColor, localColor))
                continue;
        }

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
        ghostEntityId = -1;
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

    // Remember the target entity id so the render hook can draw its real 3D model.
    ghostEntityId = isBacktracking ? closestTarget->entityId : -1;

    // Notification on activation (false -> true transition)
    if (isBacktracking && !wasBacktracking) {
        std::string targetName = FriendsManager::stripColorCodes(closestTarget->name);
        NotificationManager::addSystem("Backtrack active: " + targetName);
    }
    wasBacktracking = isBacktracking;

    // Lag Based mode: start lagging if we can benefit
    if (settings.mode == BacktrackMode::LagBased) {
        // Always store the current real position for the indicator
        currentRealX = closestTarget->posX;
        currentRealY = closestTarget->posY;
        currentRealZ = closestTarget->posZ;

        if (foundPosition && !isLagging) {
            isLagging = true;
            rollLatency();  // fresh effective latency for this session (dynamic)
            lagStartTime = now;
            log_debug("[Backtrack] Started lagging packets (%d ms)", currentLatency);
        }

        // Check if we've been lagging too long
        if (isLagging) {
            float lagDuration = std::chrono::duration<float, std::milli>(now - lagStartTime).count();
            if (lagDuration > static_cast<float>(currentLatency)) {
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

    // Two-pass edges: a soft dark underlay then the crisp coloured line on top, so
    // the box reads cleanly over any background instead of a thin flat wireframe.
    for (int i = 0; i < 12; i++) {
        int a = edges[i][0], b = edges[i][1];
        if (visible[a] && visible[b]) {
            const ImVec2 pa(screen[a].x, screen[a].y);
            const ImVec2 pb(screen[b].x, screen[b].y);
            draw->AddLine(pa, pb, IM_COL32(0, 0, 0, 150), 3.6f);
            draw->AddLine(pa, pb, color, 1.7f);
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
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    RenderOrigin o = getRenderOrigin();

    const int sr = static_cast<int>(settings.shadowColor[0] * 255);
    const int sg = static_cast<int>(settings.shadowColor[1] * 255);
    const int sb = static_cast<int>(settings.shadowColor[2] * 255);

    // Motion-blur trail: a fading silhouette at each recorded position along the
    // target's recent path (oldest = faintest), so it smears through time.
    if (settings.renderPreviousTicks && !currentTargetUUID.empty()) {
        auto it = entityHistory.find(currentTargetUUID);
        if (it != entityHistory.end() && !it->second.empty()) {
            const int maxT = (std::max)(1, settings.ticks);
            std::deque<PositionSnapshot> historyCopy = it->second;
            for (const auto& snap : historyCopy) {
                if (snap.tickAge <= 1)
                    continue;  // the freshest one is the live position
                float t = 1.0f - std::clamp(static_cast<float>(snap.tickAge) / static_cast<float>(maxT), 0.0f, 1.0f);
                drawGhost(draw, o, snap.x, snap.y, snap.z, sr, sg, sb, 0.05f + 0.28f * t, false);
            }
        }
    }

    // The primary backtracked silhouette (the one you're actually hitting).
    if (isBacktracking) {
        drawGhost(draw, o, backtrackedX, backtrackedY, backtrackedZ, static_cast<int>(settings.boxColor[0] * 255),
                  static_cast<int>(settings.boxColor[1] * 255), static_cast<int>(settings.boxColor[2] * 255), 0.85f,
                  true);
    }
}

void Backtrack::renderLagBasedModeVisuals() {
    if (currentTargetUUID.empty())
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    RenderOrigin o = getRenderOrigin();

    const int sr = static_cast<int>(settings.shadowColor[0] * 255);
    const int sg = static_cast<int>(settings.shadowColor[1] * 255);
    const int sb = static_cast<int>(settings.shadowColor[2] * 255);

    // Motion-blur trail from the buffered positions (newest = brightest).
    auto it = entityHistory.find(currentTargetUUID);
    if (it != entityHistory.end() && !it->second.empty()) {
        std::deque<PositionSnapshot> historyCopy = it->second;
        const int n = static_cast<int>(historyCopy.size());
        int idx = 0;
        for (const auto& snap : historyCopy) {
            float t = n > 1 ? static_cast<float>(idx) / static_cast<float>(n - 1) : 1.0f;
            drawGhost(draw, o, snap.x, snap.y, snap.z, sr, sg, sb, 0.05f + 0.26f * t, false);
            idx++;
        }
    }

    if (!isBacktracking)
        return;

    // Primary lagged silhouette (where they were = what your hits land on).
    drawGhost(draw, o, backtrackedX, backtrackedY, backtrackedZ, static_cast<int>(settings.boxColor[0] * 255),
              static_cast<int>(settings.boxColor[1] * 255), static_cast<int>(settings.boxColor[2] * 255), 0.85f, true);

    // Faint marker of where they actually are right now.
    if (settings.renderServerPos) {
        drawGhost(draw, o, currentRealX, currentRealY, currentRealZ, 225, 230, 240, 0.22f, false);
    }
}

void Backtrack::onRender() {
    if (!enabled)
        return;
    if (!player.isValid())
        return;
    if (!RenderData::updated)
        return;

    // Animate the on-screen "Backtrack" status text when active.
    float dt = ImGui::GetIO().DeltaTime;
    float target = isBacktracking ? 1.0f : 0.0f;
    float speed = 4.5f;
    if (g_statusAnim < target)
        g_statusAnim = std::min(g_statusAnim + dt * speed, target);
    else
        g_statusAnim = std::max(g_statusAnim - dt * speed, target);

    if (isBacktracking) {
        if (settings.mode == BacktrackMode::Manual)
            g_lastInfo = std::to_string(settings.ticks) + " ticks";
        else
            g_lastInfo = std::to_string(currentLatency) + " ms";
    }

    drawBacktrackStatus(g_statusAnim);
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
                ui::TickTooltip();
                ImGui::Spacing();
                ui::Checkbox("Render Previous Ticks", &settings.renderPreviousTicks);
            } else {
                ui::SliderInt("Min Latency", &settings.latencyMin, MIN_LATENCY_MS, MAX_LATENCY_MS, "%d ms");
                ui::SliderInt("Max Latency", &settings.latencyMax, MIN_LATENCY_MS, MAX_LATENCY_MS, "%d ms");
                if (settings.latencyMin > settings.latencyMax)
                    settings.latencyMax = settings.latencyMin;
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
        return std::to_string(settings.latencyMin) + "-" + std::to_string(settings.latencyMax) + "ms";
    }
}



void Backtrack::saveSettings(json::Value& node) const {
    node["mode"] = static_cast<int>(settings.mode);
    node["ticks"] = settings.ticks;
    node["latencyMin"] = settings.latencyMin;
    node["latencyMax"] = settings.latencyMax;
    node["renderPreviousTicks"] = settings.renderPreviousTicks;
    node["renderServerPos"] = settings.renderServerPos;
    node["minDistance"] = settings.minDistance;
    node["maxDistance"] = settings.maxDistance;
    node["maxHurtTime"] = settings.maxHurtTime;
    node["cooldown"] = settings.cooldown;
    node["weaponOnly"] = settings.weaponOnly;
    node["showBacktrackBox"] = settings.showBacktrackBox;
    node["filledBox"] = settings.filledBox;
    json::writeColor(node, "boxColor", settings.boxColor);
    json::writeColor(node, "shadowColor", settings.shadowColor);
}

void Backtrack::loadSettings(const json::Value& node) {
    settings.mode = static_cast<BacktrackMode>(node["mode"].as_int(static_cast<int>(settings.mode)));
    settings.ticks = node["ticks"].as_int(settings.ticks);
    // Migrate the old single "latency" key into the new window if present.
    int legacyLatency = node["latency"].as_int(-1);
    settings.latencyMin = node["latencyMin"].as_int(legacyLatency > 0 ? legacyLatency : settings.latencyMin);
    settings.latencyMax = node["latencyMax"].as_int(legacyLatency > 0 ? legacyLatency : settings.latencyMax);
    if (settings.latencyMin > settings.latencyMax)
        std::swap(settings.latencyMin, settings.latencyMax);
    settings.renderPreviousTicks = node["renderPreviousTicks"].as_bool(settings.renderPreviousTicks);
    settings.renderServerPos = node["renderServerPos"].as_bool(settings.renderServerPos);
    settings.minDistance = node["minDistance"].as_float(settings.minDistance);
    settings.maxDistance = node["maxDistance"].as_float(settings.maxDistance);
    settings.maxHurtTime = node["maxHurtTime"].as_int(settings.maxHurtTime);
    settings.cooldown = node["cooldown"].as_float(settings.cooldown);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    settings.showBacktrackBox = node["showBacktrackBox"].as_bool(settings.showBacktrackBox);
    settings.filledBox = node["filledBox"].as_bool(settings.filledBox);
    json::readColor(node, "boxColor", settings.boxColor);
    json::readColor(node, "shadowColor", settings.shadowColor);
}
