#include "clutch.h"

#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/render_data.h"
#include "../../sdk/player/visual_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/inject_trace.hpp"
#include "../../util/math/worldToScreen.h"

#include <cstdio>

// Redefine COBBLE_TRACE to COBBLE_TRACE_ONCE to prevent log spam in this per-frame loop module
#ifdef COBBLE_TRACE
#undef COBBLE_TRACE
#define COBBLE_TRACE(step) COBBLE_TRACE_ONCE(step)
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

Clutch* clutch = nullptr;

namespace {
constexpr double kPi = 3.14159265358979323846;

struct Direction {
    int x;
    int y;
    int z;
};

constexpr std::array<Direction, 6> kDirections = {
    Direction{1, 0, 0}, Direction{-1, 0, 0}, Direction{0, 1, 0},
    Direction{0, -1, 0}, Direction{0, 0, 1}, Direction{0, 0, -1}};
}  // namespace

Clutch::Clutch() : Module("Clutch") {
    renders = true;
    usesPlayerCache = true;
}

std::uint64_t Clutch::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

void Clutch::onEnable() {
    originalSlot_ = -1;
    nextPlaceMs_ = 0;
    activePlacement_ = {};
    lastSetYaw_ = 0.0f;
    lastSetPitch_ = 0.0f;
}

void Clutch::restoreSlot() {
    if (settings.restoreSlot && originalSlot_ >= 0 && player.isValid())
        player.setCurrentSlot(originalSlot_);
    originalSlot_ = -1;
}

void Clutch::resetPlacement() {
    activePlacement_ = {};
    nextPlaceMs_ = 0;
    lastSetYaw_ = 0.0f;
    lastSetPitch_ = 0.0f;
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPlacement_ = {};
    }
}

void Clutch::onDisable() {
    resetPlacement();
    restoreSlot();
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPath_.clear();
        cachedPlacement_ = {};
    }
}

bool Clutch::isPlaceableBlock(int id) {
    if (id <= 0 || id > 255)
        return false;
    switch (id) {
        case 6:
        case 8:
        case 9:
        case 10:
        case 11:
        case 26:
        case 27:
        case 28:
        case 30:
        case 31:
        case 32:
        case 37:
        case 38:
        case 39:
        case 40:
        case 50:
        case 51:
        case 55:
        case 59:
        case 63:
        case 64:
        case 65:
        case 66:
        case 68:
        case 69:
        case 70:
        case 71:
        case 72:
        case 75:
        case 76:
        case 77:
        case 78:
        case 83:
        case 90:
        case 93:
        case 94:
        case 96:
        case 104:
        case 105:
        case 106:
        case 111:
        case 115:
        case 119:
        case 127:
        case 131:
        case 132:
        case 140:
        case 141:
        case 142:
        case 143:
        case 147:
        case 148:
        case 149:
        case 150:
        case 157:
        case 171:
        case 175:
            return false;
        default:
            return true;
    }
}

int Clutch::findBlockSlot() const {
    COBBLE_TRACE("Clutch::findBlockSlot enter");
    for (int slot = 0; slot < 9; ++slot) {
        int blockId = player.getItemInSlot(slot);
        if (isPlaceableBlock(blockId)) {
            COBBLE_TRACE("Clutch::findBlockSlot found placeable block");
            return slot;
        }
    }
    COBBLE_TRACE("Clutch::findBlockSlot returns -1");
    return -1;
}

float Clutch::wrapDegrees(float angle) {
    while (angle > 180.0f)
        angle -= 360.0f;
    while (angle < -180.0f)
        angle += 360.0f;
    return angle;
}

float Clutch::approachAngle(float current, float target, float maxStep) {
    const float delta = wrapDegrees(target - current);
    return current + std::clamp(delta, -maxStep, maxStep);
}

bool Clutch::isDangerous(double predictedX, double predictedY, double predictedZ) const {
    if (std::isnan(predictedX) || std::isnan(predictedY) || std::isnan(predictedZ) ||
        std::isinf(predictedX) || std::isinf(predictedY) || std::isinf(predictedZ)) {
        return false;
    }
    if (predictedY <= 1.0)
        return false;

    // Already falling: trigger immediately
    const bool alreadyFalling = !player.isOnGround() && player.getMotionY() < -0.025;
    if (alreadyFalling)
        return true;

    // Key insight: knockback has POSITIVE motionY, so predictedY is above current pos.
    // We must check ground support at predicted X,Z at CURRENT foot height, not predicted Y.
    // If no ground there → player will fall when they arrive horizontally.
    const int footY = static_cast<int>(std::floor(player.getPosY())) - 1;
    if (footY < 0)
        return false;

    const std::array<double, 3> offsets = {-0.28, 0.0, 0.28};
    bool hasSupportAtLandingXZ = false;

    for (double offsetX : offsets) {
        for (double offsetZ : offsets) {
            const int x = static_cast<int>(std::floor(predictedX + offsetX));
            const int z = static_cast<int>(std::floor(predictedZ + offsetZ));
            // Scan 6 blocks down from current foot level
            const int minY = std::max(0, footY - 6);
            for (int y = footY; y >= minY; --y) {
                if (mc.getBlockId(x, y, z) != 0) {
                    hasSupportAtLandingXZ = true;
                    break;
                }
            }
            if (hasSupportAtLandingXZ) break;
        }
        if (hasSupportAtLandingXZ) break;
    }

    // If there's ground at the predicted horizontal landing → not dangerous
    if (hasSupportAtLandingXZ)
        return false;

    // No support at predicted X,Z: dangerous only if actually moving horizontally
    const double projectedX = predictedX - player.getPosX();
    const double projectedZ = predictedZ - player.getPosZ();
    return projectedX * projectedX + projectedZ * projectedZ > 0.04;
}


bool Clutch::isPlacementUsable(const Placement& placement) const {
    if (!placement.valid || mc.getBlockId(placement.targetX, placement.targetY, placement.targetZ) != 0)
        return false;
    if (mc.getBlockId(placement.supportX, placement.supportY, placement.supportZ) == 0)
        return false;

    const double dx = placement.hitX - player.getPosX();
    const double dy = placement.hitY - (player.getPosY() + 1.62);
    const double dz = placement.hitZ - player.getPosZ();
    const double reach = static_cast<double>(settings.reach) + 0.15;
    return dx * dx + dy * dy + dz * dz <= reach * reach;
}

Clutch::Placement Clutch::findPlacement(double predictedX, double predictedY, double predictedZ) const {
    Placement best;
    best.score = std::numeric_limits<double>::max();

    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const int centerX = static_cast<int>(std::floor(predictedX));
    const int centerY = static_cast<int>(std::floor(predictedY)) - 1;
    const int centerZ = static_cast<int>(std::floor(predictedZ));
    const int radius = std::clamp(settings.searchRadius, 2, 8);
    const double maxReachSq = static_cast<double>(settings.reach) * settings.reach;

    for (int ring = 0; ring <= radius; ++ring) {
        for (int x = centerX - ring; x <= centerX + ring; ++x) {
            for (int z = centerZ - ring; z <= centerZ + ring; ++z) {
                if (std::max(std::abs(x - centerX), std::abs(z - centerZ)) != ring)
                    continue;
                for (int y = centerY - 2; y <= centerY + 1; ++y) {
                    if (mc.getBlockId(x, y, z) != 0)
                        continue;
                    for (const Direction& direction : kDirections) {
                        const int supportX = x - direction.x;
                        const int supportY = y - direction.y;
                        const int supportZ = z - direction.z;
                        if (mc.getBlockId(supportX, supportY, supportZ) == 0)
                            continue;

                        const double hitX = supportX + 0.5 + direction.x * 0.49;
                        const double hitY = supportY + 0.5 + direction.y * 0.49;
                        const double hitZ = supportZ + 0.5 + direction.z * 0.49;
                        const double dx = hitX - eyeX;
                        const double dy = hitY - eyeY;
                        const double dz = hitZ - eyeZ;
                        const double reachSq = dx * dx + dy * dy + dz * dz;
                        if (reachSq > maxReachSq)
                            continue;

                        const double landingDx = (x + 0.5) - predictedX;
                        const double landingDz = (z + 0.5) - predictedZ;
                        
                        double cameraPenalty = 0.0;
                        if (settings.cameraWeight) {
                            const double dxLook = hitX - eyeX;
                            const double dyLook = hitY - eyeY;
                            const double dzLook = hitZ - eyeZ;
                            const double horizontalLook = std::sqrt(dxLook * dxLook + dzLook * dzLook);
                            const float targetYaw = static_cast<float>(std::atan2(dzLook, dxLook) * 180.0 / kPi - 90.0);
                            const float targetPitch = std::clamp(static_cast<float>(-std::atan2(dyLook, horizontalLook) * 180.0 / kPi), -90.0f, 90.0f);
                            const float yawDiff = std::abs(wrapDegrees(targetYaw - player.getYaw()));
                            const float pitchDiff = std::abs(wrapDegrees(targetPitch - player.getPitch()));
                            cameraPenalty = (yawDiff * yawDiff + pitchDiff * pitchDiff) * 0.0005;
                        }

                        const double score = landingDx * landingDx + landingDz * landingDz +
                                             reachSq * 0.04 + std::abs(y - centerY) * 0.8 + cameraPenalty;
                        if (score < best.score) {
                            best = {x, y, z, supportX, supportY, supportZ, hitX, hitY, hitZ, score, true};
                        }
                    }
                }
            }
        }
        if (best.valid)
            break;
    }
    return best;
}

bool Clutch::place(const Placement& placement, int slot) {
    if (!isPlacementUsable(placement))
        return false;

    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const double dx = placement.hitX - eyeX;
    const double dy = placement.hitY - eyeY;
    const double dz = placement.hitZ - eyeZ;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    const float targetYaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
    const float targetPitch =
        std::clamp(static_cast<float>(-std::atan2(dy, horizontal) * 180.0 / kPi), -90.0f, 90.0f);
    const float currentYaw = player.getYaw();
    const float currentPitch = player.getPitch();
    const std::uint64_t now = nowMs();
    const float variation = static_cast<float>((now / 17u) % 7u) * 0.18f;

    float nextYaw = currentYaw;
    float nextPitch = currentPitch;

    if (settings.smoothRotation) {
        float mouseDeltaYaw = 0.0f;
        float mouseDeltaPitch = 0.0f;
        if (lastSetYaw_ != 0.0f || lastSetPitch_ != 0.0f) {
            mouseDeltaYaw = wrapDegrees(currentYaw - lastSetYaw_);
            mouseDeltaPitch = wrapDegrees(currentPitch - lastSetPitch_);
        }

        float startYaw = currentYaw;
        float startPitch = currentPitch;

        float stepYaw = settings.rotationSpeed + variation;
        float stepPitch = (settings.rotationSpeed * 0.7f) + (variation * 0.65f);

        nextYaw = approachAngle(startYaw, targetYaw, stepYaw);
        nextPitch = approachAngle(startPitch, targetPitch, stepPitch);

        float sensitivity = mc.getMouseSensitivity();
        float f = sensitivity * 0.6f + 0.2f;
        float gcd = f * f * f * 1.2f;

        if (gcd > 0.0001f) {
            float diffYaw = wrapDegrees(nextYaw - startYaw);
            float diffPitch = wrapDegrees(nextPitch - startPitch);

            float quantizedDiffYaw = std::round(diffYaw / gcd) * gcd;
            float quantizedDiffPitch = std::round(diffPitch / gcd) * gcd;

            nextYaw = startYaw + quantizedDiffYaw;
            nextPitch = startPitch + quantizedDiffPitch;
        }

        lastSetYaw_ = nextYaw;
        lastSetPitch_ = nextPitch;
    } else {
        nextYaw = approachAngle(currentYaw, targetYaw, 9.5f + variation);
        nextPitch = approachAngle(currentPitch, targetPitch, 7.0f + variation * 0.65f);
        lastSetYaw_ = nextYaw;
        lastSetPitch_ = nextPitch;
    }

    player.setPrevYaw(currentYaw);
    player.setPrevPitch(currentPitch);
    player.setYaw(nextYaw);
    player.setPitch(std::clamp(nextPitch, -90.0f, 90.0f));

    const float remainingYaw = std::abs(wrapDegrees(targetYaw - nextYaw));
    const float remainingPitch = std::abs(wrapDegrees(targetPitch - nextPitch));

    float toleranceYaw = settings.smoothRotation ? 4.5f : 3.25f;
    float tolerancePitch = settings.smoothRotation ? 3.5f : 2.75f;

    if (remainingYaw > toleranceYaw || remainingPitch > tolerancePitch)
        return false;

    if (originalSlot_ < 0)
        originalSlot_ = player.getCurrentSlot();
    if (player.getCurrentSlot() != slot)
        player.setCurrentSlot(slot);

    COBBLE_TRACE("Clutch::place checking mouse over");
    const Minecraft::Vec3 targeted = mc.getTargetedBlockPos();
    if (static_cast<int>(std::floor(targeted.x)) != placement.supportX ||
        static_cast<int>(std::floor(targeted.y)) != placement.supportY ||
        static_cast<int>(std::floor(targeted.z)) != placement.supportZ) {
        COBBLE_TRACE("Clutch::place target block mismatch");
        return false;
    }

    COBBLE_TRACE("Clutch::place sending right click");
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        mc.setRightClickDelay(0);
        PostMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
        PostMessage(hwnd, WM_RBUTTONUP, 0, 0);
    }
    COBBLE_TRACE("Clutch::place right click sent");
    return true;
}

void Clutch::onUpdate() {
    lastUpdateTick_ = GetTickCount64();

    if (!enabled)
        return;
    COBBLE_TRACE("Clutch::onUpdate enabled");

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        COBBLE_TRACE("Clutch::onUpdate inactive menu/gui/player invalid");
        resetPlacement();
        restoreSlot();
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPath_.clear();
        return;
    }

    const auto snap = VisualCache::GetSnapshot();
    if (snap.valid) {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedLocalX_ = snap.localX;
        cachedLocalY_ = snap.localY;
        cachedLocalZ_ = snap.localZ;
        cachedLocalLastX_ = snap.localLastX;
        cachedLocalLastY_ = snap.localLastY;
        cachedLocalLastZ_ = snap.localLastZ;
        cachedRenderX_ = snap.renderPos.x;
        cachedRenderY_ = snap.renderPos.y;
        cachedRenderZ_ = snap.renderPos.z;
        cachedPartialTicks_ = snap.partialTicks;

        cachedPlayerValid_ = player.isValid();
        if (cachedPlayerValid_) {
            cachedMotionX_ = player.getMotionX();
            cachedMotionY_ = player.getMotionY();
            cachedMotionZ_ = player.getMotionZ();
        } else {
            cachedMotionX_ = 0.0;
            cachedMotionY_ = 0.0;
            cachedMotionZ_ = 0.0;
        }
    }

    // Simulate visual prediction path
    if (settings.showPath) {
        std::vector<PathPoint> path;
        double currentX = player.getPosX();
        double currentY = player.getPosY();
        double currentZ = player.getPosZ();
        double currentMotionX = player.getMotionX();
        double currentMotionY = player.getMotionY();
        double currentMotionZ = player.getMotionZ();

        double relX = 0.0;
        double relY = 0.0;
        double relZ = 0.0;

        PathPoint startPt;
        startPt.relX = 0.0;
        startPt.relY = 0.0;
        startPt.relZ = 0.0;
        startPt.hitBlock = false;
        path.push_back(startPt);

        for (int step = 0; step < 40; ++step) {
            const int subSteps = 4;
            for (int sub = 0; sub < subSteps; ++sub) {
                relX += currentMotionX / subSteps;
                relY += currentMotionY / subSteps;
                relZ += currentMotionZ / subSteps;

                PathPoint pt;
                pt.relX = relX;
                pt.relY = relY;
                pt.relZ = relZ;
                pt.hitBlock = false;
                path.push_back(pt);
            }

            currentMotionX *= 0.91;
            currentMotionZ *= 0.91;
            currentMotionY = (currentMotionY - 0.08) * 0.98;

            int checkX = static_cast<int>(std::floor(currentX + relX));
            int checkY = static_cast<int>(std::floor(currentY + relY));
            int checkZ = static_cast<int>(std::floor(currentZ + relZ));

            if (checkY < 0) {
                break;
            }
            if (mc.getBlockId(checkX, checkY, checkZ) != 0) {
                if (!path.empty()) {
                    path.back().hitBlock = true;
                }
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(pathMutex_);
            cachedPath_ = std::move(path);
        }
    } else {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPath_.clear();
    }

    COBBLE_TRACE("Clutch::onUpdate before findBlockSlot");
    const int slot = findBlockSlot();
    if (slot < 0) {
        COBBLE_TRACE("Clutch::onUpdate block slot invalid");
        resetPlacement();
        restoreSlot();
        return;
    }

    COBBLE_TRACE("Clutch::onUpdate getting coordinates");
    double x = player.getPosX();
    double y = player.getPosY();
    double z = player.getPosZ();
    double motionX = player.getMotionX();
    double motionY = player.getMotionY();
    double motionZ = player.getMotionZ();

    if (std::isnan(x) || std::isnan(y) || std::isnan(z) ||
        std::isnan(motionX) || std::isnan(motionY) || std::isnan(motionZ) ||
        std::isinf(x) || std::isinf(y) || std::isinf(z) ||
        std::isinf(motionX) || std::isinf(motionY) || std::isinf(motionZ)) {
        COBBLE_TRACE("Clutch::onUpdate nan/inf detected");
        resetPlacement();
        restoreSlot();
        return;
    }
    COBBLE_TRACE("Clutch::onUpdate computing prediction");
    const int ticks = std::clamp((settings.predictionMs + 49) / 50, 2, 12);
    for (int tick = 0; tick < ticks; ++tick) {
        x += motionX;
        y += motionY;
        z += motionZ;
        motionX *= 0.91;
        motionZ *= 0.91;
        motionY = (motionY - 0.08) * 0.98;
    }

    COBBLE_TRACE("Clutch::onUpdate before isDangerous");
    if (!isDangerous(x, y, z)) {
        COBBLE_TRACE("Clutch::onUpdate not dangerous");
        resetPlacement();
        restoreSlot();
        return;
    }

    COBBLE_TRACE("Clutch::onUpdate before findPlacement");
    if (!isPlacementUsable(activePlacement_))
        activePlacement_ = findPlacement(x, y, z);
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        cachedPlacement_ = activePlacement_;
    }
    if (!activePlacement_.valid) {
        COBBLE_TRACE("Clutch::onUpdate placement invalid");
        return;
    }

    COBBLE_TRACE("Clutch::onUpdate checking nextPlaceMs_");
    const std::uint64_t now = nowMs();
    if (nextPlaceMs_ == 0) {
        const int baseDelay = std::clamp(settings.placeDelayMs, 40, 200);
        const int jitter = static_cast<int>((now ^ (now >> 7u)) % 31u) - 8;
        nextPlaceMs_ = now + static_cast<std::uint64_t>(std::max(30, baseDelay + jitter));
    }

    if (now < nextPlaceMs_) {
        COBBLE_TRACE("Clutch::onUpdate waiting nextPlaceMs");
        return;
    }

    COBBLE_TRACE("Clutch::onUpdate calling place");
    if (place(activePlacement_, slot)) {
        COBBLE_TRACE("Clutch::onUpdate place success");
        activePlacement_ = {};
        {
            std::lock_guard<std::mutex> lock(pathMutex_);
            cachedPlacement_ = {};
        }
        const int baseDelay = std::clamp(settings.placeDelayMs, 40, 200);
        const int jitter = static_cast<int>(((now >> 3u) ^ now) % 37u) - 10;
        nextPlaceMs_ = now + static_cast<std::uint64_t>(std::max(30, baseDelay + jitter));
    } else {
        COBBLE_TRACE("Clutch::onUpdate place failure");
    }
}

void Clutch::onRender() {
    if (!enabled)
        return;
    if (!RenderData::updated)
        return;
    if (Menu::isOpen)
        return;

    if (GetTickCount64() - lastUpdateTick_ > 300)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    std::vector<PathPoint> pathPoints;
    double interpX, interpY, interpZ;
    double motionX = 0.0, motionY = 0.0, motionZ = 0.0;
    bool playerValid = false;
    Placement placement;
    {
        std::lock_guard<std::mutex> lock(pathMutex_);
        pathPoints = cachedPath_;
        interpX = cachedLocalLastX_ + (cachedLocalX_ - cachedLocalLastX_) * cachedPartialTicks_;
        interpY = cachedLocalLastY_ + (cachedLocalY_ - cachedLocalLastY_) * cachedPartialTicks_;
        interpZ = cachedLocalLastZ_ + (cachedLocalZ_ - cachedLocalLastZ_) * cachedPartialTicks_;
        motionX = cachedMotionX_;
        motionY = cachedMotionY_;
        motionZ = cachedMotionZ_;
        playerValid = cachedPlayerValid_;
        placement = cachedPlacement_;
    }

    if (settings.showBps && playerValid) {
        double horizontalSpeed = std::sqrt(motionX * motionX + motionZ * motionZ) * 20.0;
        double verticalSpeed = motionY * 20.0;

        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Speed: %.2f BPS  (Y: %.2f BPS)", horizontalSpeed, verticalSpeed);

        ImVec2 textSize = ImGui::CalcTextSize(buffer);
        float x = (static_cast<float>(RenderData::screenWidth) - textSize.x) / 2.0f;
        float y = static_cast<float>(RenderData::screenHeight) / 2.0f + 60.0f;

        draw->AddRectFilled({x - 12.0f, y - 6.0f}, {x + textSize.x + 12.0f, y + textSize.y + 6.0f}, IM_COL32(20, 20, 20, 160), 6.0f);
        draw->AddRect({x - 12.0f, y - 6.0f}, {x + textSize.x + 12.0f, y + textSize.y + 6.0f}, IM_COL32(255, 255, 255, 30), 6.0f, ImDrawFlags_None, 1.0f);

        ImU32 textColor = IM_COL32(255, 255, 255, 240);
        if (verticalSpeed < -5.0) {
            textColor = IM_COL32(240, 70, 70, 240);
        } else if (horizontalSpeed > 8.0) {
            textColor = IM_COL32(70, 240, 200, 240);
        }
        draw->AddText({x, y}, textColor, buffer);
    }

    if (settings.showPath && pathPoints.size() >= 2) {
        std::vector<ImVec2> screenPoints;
        screenPoints.reserve(pathPoints.size());

        for (const auto& point : pathPoints) {
            double worldX = interpX + point.relX;
            double worldY = interpY + point.relY;
            double worldZ = interpZ + point.relZ;

            Vector3 cameraRelPos{
                static_cast<float>(worldX - cachedRenderX_),
                static_cast<float>(worldY - cachedRenderY_),
                static_cast<float>(worldZ - cachedRenderZ_)
            };

            Vector2 screenPos;
            bool visible = CWorldToScreen::WorldToScreen(cameraRelPos, RenderData::modelView, RenderData::projection,
                                                         RenderData::screenWidth, RenderData::screenHeight, screenPos);
            if (visible) {
                screenPoints.push_back({screenPos.x, screenPos.y});
            }
        }

        if (screenPoints.size() >= 2) {
            draw->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), IM_COL32(0, 0, 0, 180), ImDrawFlags_None, 3.5f);
            draw->AddPolyline(screenPoints.data(), static_cast<int>(screenPoints.size()), IM_COL32(0, 191, 255, 220), ImDrawFlags_None, 1.5f);
        }
    }

    if (settings.showPlacement && placement.valid) {
        int bx = placement.targetX;
        int by = placement.targetY;
        int bz = placement.targetZ;

        float minX = static_cast<float>(bx - cachedRenderX_);
        float minY = static_cast<float>(by - cachedRenderY_);
        float minZ = static_cast<float>(bz - cachedRenderZ_);
        float maxX = minX + 1.0f;
        float maxY = minY + 1.0f;
        float maxZ = minZ + 1.0f;

        Vector3 corners[8] = {
            {minX, minY, minZ}, {maxX, minY, minZ}, {maxX, minY, maxZ}, {minX, minY, maxZ},
            {minX, maxY, minZ}, {maxX, maxY, minZ}, {maxX, maxY, maxZ}, {minX, maxY, maxZ}
        };

        Vector2 screenCorners[8];
        bool visible[8];
        int visibleCount = 0;
        for (int i = 0; i < 8; ++i) {
            visible[i] = CWorldToScreen::WorldToScreen(corners[i], RenderData::modelView, RenderData::projection,
                                                       RenderData::screenWidth, RenderData::screenHeight, screenCorners[i]);
            if (visible[i]) visibleCount++;
        }

        if (visibleCount >= 3) {
            ImU32 faceColor = IM_COL32(0, 191, 255, 35);
            ImU32 outlineColor = IM_COL32(0, 191, 255, 220);

            if (visible[0] && visible[1] && visible[2] && visible[3]) {
                ImVec2 pts[4] = {{screenCorners[0].x, screenCorners[0].y}, {screenCorners[1].x, screenCorners[1].y}, {screenCorners[2].x, screenCorners[2].y}, {screenCorners[3].x, screenCorners[3].y}};
                draw->AddConvexPolyFilled(pts, 4, faceColor);
            }
            if (visible[4] && visible[5] && visible[6] && visible[7]) {
                ImVec2 pts[4] = {{screenCorners[4].x, screenCorners[4].y}, {screenCorners[5].x, screenCorners[5].y}, {screenCorners[6].x, screenCorners[6].y}, {screenCorners[7].x, screenCorners[7].y}};
                draw->AddConvexPolyFilled(pts, 4, faceColor);
            }
            if (visible[2] && visible[3] && visible[7] && visible[6]) {
                ImVec2 pts[4] = {{screenCorners[2].x, screenCorners[2].y}, {screenCorners[3].x, screenCorners[3].y}, {screenCorners[7].x, screenCorners[7].y}, {screenCorners[6].x, screenCorners[6].y}};
                draw->AddConvexPolyFilled(pts, 4, faceColor);
            }
            if (visible[0] && visible[1] && visible[5] && visible[4]) {
                ImVec2 pts[4] = {{screenCorners[0].x, screenCorners[0].y}, {screenCorners[1].x, screenCorners[1].y}, {screenCorners[5].x, screenCorners[5].y}, {screenCorners[4].x, screenCorners[4].y}};
                draw->AddConvexPolyFilled(pts, 4, faceColor);
            }
            if (visible[0] && visible[3] && visible[7] && visible[4]) {
                ImVec2 pts[4] = {{screenCorners[0].x, screenCorners[0].y}, {screenCorners[3].x, screenCorners[3].y}, {screenCorners[7].x, screenCorners[7].y}, {screenCorners[4].x, screenCorners[4].y}};
                draw->AddConvexPolyFilled(pts, 4, faceColor);
            }
            if (visible[1] && visible[2] && visible[6] && visible[5]) {
                ImVec2 pts[4] = {{screenCorners[1].x, screenCorners[1].y}, {screenCorners[2].x, screenCorners[2].y}, {screenCorners[6].x, screenCorners[6].y}, {screenCorners[5].x, screenCorners[5].y}};
                draw->AddConvexPolyFilled(pts, 4, faceColor);
            }

            static constexpr int edges[12][2] = {
                {0, 1}, {1, 2}, {2, 3}, {3, 0},
                {4, 5}, {5, 6}, {6, 7}, {7, 4},
                {0, 4}, {1, 5}, {2, 6}, {3, 7}
            };
            for (const auto& edge : edges) {
                if (visible[edge[0]] && visible[edge[1]]) {
                    draw->AddLine({screenCorners[edge[0]].x, screenCorners[edge[0]].y},
                                  {screenCorners[edge[1]].x, screenCorners[edge[1]].y}, outlineColor, 1.5f);
                }
            }
        }
    }
}

void Clutch::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Prediction & Camera",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ui::SliderInt("Prediction Time", &settings.predictionMs, 100, 600, "%d ms");
            ui::SliderInt("Search Radius", &settings.searchRadius, 2, 8, "%d blocks");
            ui::Checkbox("Camera-Weighted Search", &settings.cameraWeight);
            ui::Checkbox("Smooth Rotations", &settings.smoothRotation);
            if (settings.smoothRotation) {
                ui::SliderFloat("Rotation Speed", &settings.rotationSpeed, 5.0f, 45.0f, "%.1f deg");
            }
        },
        "Placement & HUD",
        [this]() {
            ui::SliderFloat("Reach", &settings.reach, 3.0f, 4.5f, "%.1f blocks");
            ui::SliderInt("Place Delay", &settings.placeDelayMs, 40, 200, "%d ms");
            ui::Checkbox("Switch Back", &settings.restoreSlot);
            ui::Checkbox("Show Speed HUD", &settings.showBps);
            ui::Checkbox("Show Visual Path", &settings.showPath);
            ui::Checkbox("Show Block Preview", &settings.showPlacement);
        });
}
