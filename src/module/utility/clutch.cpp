#include "clutch.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
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

Clutch::Clutch() : Module("Clutch") {}

std::uint64_t Clutch::nowMs() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

void Clutch::onEnable() {
    originalSlot_ = -1;
    lastPlaceMs_ = 0;
}

void Clutch::restoreSlot() {
    if (settings.restoreSlot && originalSlot_ >= 0 && player.isValid())
        player.setCurrentSlot(originalSlot_);
    originalSlot_ = -1;
}

void Clutch::onDisable() {
    restoreSlot();
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
    for (int slot = 0; slot < 9; ++slot) {
        if (isPlaceableBlock(player.getItemInSlot(slot)))
            return slot;
    }
    return -1;
}

bool Clutch::isDangerous(double predictedX, double predictedY, double predictedZ) const {
    const int y = static_cast<int>(std::floor(predictedY)) - 1;
    const int centerX = static_cast<int>(std::floor(predictedX));
    const int centerZ = static_cast<int>(std::floor(predictedZ));
    for (int x = centerX - 1; x <= centerX + 1; ++x) {
        for (int z = centerZ - 1; z <= centerZ + 1; ++z) {
            if (mc.getBlockId(x, y, z) != 0)
                return false;
        }
    }
    const double projectedX = predictedX - player.getPosX();
    const double projectedZ = predictedZ - player.getPosZ();
    const bool leavingEdge = projectedX * projectedX + projectedZ * projectedZ > 0.12;
    return player.getMotionY() < -0.04 || !player.isOnGround() || leavingEdge;
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
                        const double score = landingDx * landingDx + landingDz * landingDz +
                                             reachSq * 0.04 + std::abs(y - centerY) * 0.8;
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
    if (!placement.valid)
        return false;

    if (originalSlot_ < 0)
        originalSlot_ = player.getCurrentSlot();
    if (player.getCurrentSlot() != slot)
        player.setCurrentSlot(slot);

    const double eyeX = player.getPosX();
    const double eyeY = player.getPosY() + 1.62;
    const double eyeZ = player.getPosZ();
    const double dx = placement.hitX - eyeX;
    const double dy = placement.hitY - eyeY;
    const double dz = placement.hitZ - eyeZ;
    const double horizontal = std::sqrt(dx * dx + dz * dz);
    const float yaw = static_cast<float>(std::atan2(dz, dx) * 180.0 / kPi - 90.0);
    const float pitch = static_cast<float>(-std::atan2(dy, horizontal) * 180.0 / kPi);

    player.setPrevYaw(yaw);
    player.setPrevPitch(pitch);
    player.setYaw(yaw);
    player.setPitch(std::clamp(pitch, -90.0f, 90.0f));
    mc.updateMouseOver();

    const Minecraft::Vec3 targeted = mc.getTargetedBlockPos();
    if (static_cast<int>(std::floor(targeted.x)) != placement.supportX ||
        static_cast<int>(std::floor(targeted.y)) != placement.supportY ||
        static_cast<int>(std::floor(targeted.z)) != placement.supportZ)
        return false;

    mc.setRightClickDelay(0);
    mc.rightClickMouse();
    return true;
}

void Clutch::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        restoreSlot();
        return;
    }

    const int slot = findBlockSlot();
    if (slot < 0) {
        restoreSlot();
        return;
    }

    double x = player.getPosX();
    double y = player.getPosY();
    double z = player.getPosZ();
    double motionX = player.getMotionX();
    double motionY = player.getMotionY();
    double motionZ = player.getMotionZ();
    const int ticks = std::clamp((settings.predictionMs + 49) / 50, 2, 12);
    for (int tick = 0; tick < ticks; ++tick) {
        x += motionX;
        y += motionY;
        z += motionZ;
        motionX *= 0.91;
        motionZ *= 0.91;
        motionY = (motionY - 0.08) * 0.98;
    }

    if (!isDangerous(x, y, z)) {
        restoreSlot();
        return;
    }

    const std::uint64_t now = nowMs();
    if (now - lastPlaceMs_ < static_cast<std::uint64_t>(std::clamp(settings.placeDelayMs, 40, 200)))
        return;

    if (place(findPlacement(x, y, z), slot))
        lastPlaceMs_ = now;
}

void Clutch::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "Prediction",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind) && enabledValue != enabled)
                setEnabled(enabledValue);
            ui::SliderInt("Prediction Time", &settings.predictionMs, 100, 600, "%d ms");
            ui::SliderInt("Search Radius", &settings.searchRadius, 2, 8, "%d blocks");
        },
        "Placement",
        [this]() {
            ui::SliderFloat("Reach", &settings.reach, 3.0f, 4.5f, "%.1f blocks");
            ui::SliderInt("Place Delay", &settings.placeDelayMs, 40, 200, "%d ms");
            ui::Checkbox("Switch Back", &settings.restoreSlot);
        });
}
