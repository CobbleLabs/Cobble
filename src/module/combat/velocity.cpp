#include "velocity.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <random>

Velocity* velocity = nullptr;

static std::mt19937 rng(std::random_device{}());

Velocity::Velocity() : Module("Velocity") {
    keybind = 0;
}

void Velocity::onEnable() {
    lastHurtTime = 0;
    queuedHit.active = false;
    pending.active = false;
}

void Velocity::onDisable() {
    queuedHit.active = false;
    pending.active = false;
}

bool Velocity::checkChance(float chance) {
    if (chance >= 100.0f)
        return true;
    if (chance <= 0.0f)
        return false;
    std::uniform_int_distribution<int> dist(1, 100);
    return dist(rng) <= static_cast<int>(std::round(chance));
}

bool Velocity::checkFOV() {
    // 360 FOV means accept all angles
    if (settings.fov >= 360.0f)
        return true;

    // Get angle to attacker - simplified: check if we're facing the direction knockback came from
    // Since we don't have direct attacker info, we use motion direction as approximation
    double motionX = player.getMotionX();
    double motionZ = player.getMotionZ();

    if (motionX == 0 && motionZ == 0)
        return true;

    // Calculate knockback direction angle
    float kbAngle = (float)(atan2(-motionX, motionZ) * 180.0 / 3.14159265);
    float playerYaw = player.getYaw();

    // Normalize angles
    while (kbAngle < 0)
        kbAngle += 360;
    while (playerYaw < 0)
        playerYaw += 360;

    // Calculate angle difference (knockback from front = 180 degrees from motion)
    float angleDiff = abs(kbAngle - playerYaw);
    if (angleDiff > 180)
        angleDiff = 360 - angleDiff;

    // Check if within FOV (knockback from front = ~180 diff, from back = ~0 diff)
    // FOV 180 = only reduce from front (angleDiff > 90)
    float halfFov = settings.fov / 2.0f;
    return angleDiff >= (180.0f - halfFov);
}

bool Velocity::isLookingAtPlayer() {
    return mc.isTargetingEntity();
}

bool Velocity::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    // Axes: 271, 275, 258, 279, 286
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 || itemId == 271 ||
            itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

bool Velocity::checkConditions() {
    // Check all enabled conditions
    if (settings.onlyLookingAtPlayer && !isLookingAtPlayer())
        return false;

    if (settings.onlyMousePressed) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            return false;
    }

    if (settings.onlyMovingForward) {
        if (!(GetAsyncKeyState('W') & 0x8000))
            return false;
    }

    if (settings.onlyHoldingWeapon && !isHoldingWeapon())
        return false;

    if (settings.onlySprinting && !player.isSprinting())
        return false;

    return true;
}

void Velocity::queueHit(int hurtTime) {
    if (!checkFOV() || !checkConditions())
        return;

    const float modeChance = settings.mode == Mode::Jump ? settings.jumpChance : settings.chance;
    if (!checkChance(modeChance))
        return;

    std::uniform_real_distribution<float> variance(-settings.percentageVariance, settings.percentageVariance);
    queuedHit.active = true;
    queuedHit.mode = settings.mode;
    queuedHit.applyAtHurtTime = (std::max)(0, hurtTime - settings.reduceDelay);
    queuedHit.horizontal = (std::clamp)(settings.horizontal + variance(rng), 0.0f, 100.0f);
    queuedHit.vertical = (std::clamp)(settings.vertical + variance(rng), 0.0f, 100.0f);

    int microDelay = 0;
    if (settings.microDelayMs > 0) {
        std::uniform_int_distribution<int> delay(0, settings.microDelayMs);
        microDelay = delay(rng);
    }
    queuedHit.applyAtMs = GetTickCount() + static_cast<unsigned long>(microDelay);
}

void Velocity::applyQueuedHit(int hurtTime) {
    const PendingHit hit = queuedHit;
    queuedHit.active = false;

    switch (hit.mode) {
            case Mode::Regular: {
                if (settings.tickSpread > 0) {
                    pending.active = true;
                    pending.totalTicks = settings.tickSpread + 1;
                    pending.ticksRemaining = settings.tickSpread;
                    double motionX = player.getMotionX();
                    double motionY = player.getMotionY();
                    double motionZ = player.getMotionZ();
                    pending.targetMotionX = motionX * (hit.horizontal / 100.0);
                    pending.targetMotionY = motionY * (hit.vertical / 100.0);
                    pending.targetMotionZ = motionZ * (hit.horizontal / 100.0);
                    pending.lastHurtTime = hurtTime;
                    double newX = motionX - ((motionX - pending.targetMotionX) / pending.totalTicks);
                    double newY = motionY - ((motionY - pending.targetMotionY) / pending.totalTicks);
                    double newZ = motionZ - ((motionZ - pending.targetMotionZ) / pending.totalTicks);
                    player.setMotionX(newX);
                    player.setMotionY(newY);
                    player.setMotionZ(newZ);
                } else {
                    pending.active = false;
                    player.setMotionX(player.getMotionX() * (hit.horizontal / 100.0));
                    player.setMotionY(player.getMotionY() * (hit.vertical / 100.0));
                    player.setMotionZ(player.getMotionZ() * (hit.horizontal / 100.0));
                }
                break;
            }

            case Mode::Jump: {
                if (player.isOnGround())
                    player.jump();
                break;
            }

            case Mode::Reduce: {
                player.setMotionX(player.getMotionX() * (hit.horizontal / 100.0));
                player.setMotionY(player.getMotionY() * (hit.vertical / 100.0));
                player.setMotionZ(player.getMotionZ() * (hit.horizontal / 100.0));
                break;
            }

            case Mode::Ignore: {
                player.setMotionX(0);
                player.setMotionZ(0);
                switch (settings.verticalMode) {
                    case VerticalMode::Never:
                        break;
                    case VerticalMode::OnlyInAir:
                        if (!player.isOnGround())
                            player.setMotionY(0);
                        break;
                    case VerticalMode::Always:
                        player.setMotionY(0);
                        break;
                }
                break;
            }
    }
}

void Velocity::continuePendingReduction(int hurtTime) {
    if (!pending.active || pending.ticksRemaining <= 0 || pending.lastHurtTime == hurtTime)
        return;

    pending.lastHurtTime = hurtTime;
    if (pending.active && pending.ticksRemaining > 0) {
        double currentX = player.getMotionX();
        double currentY = player.getMotionY();
        double currentZ = player.getMotionZ();

        double xDiff = (currentX - pending.targetMotionX) / (pending.ticksRemaining + 1);
        double yDiff = (currentY - pending.targetMotionY) / (pending.ticksRemaining + 1);
        double zDiff = (currentZ - pending.targetMotionZ) / (pending.ticksRemaining + 1);

        player.setMotionX(currentX - xDiff);
        player.setMotionY(currentY - yDiff);
        player.setMotionZ(currentZ - zDiff);

        pending.ticksRemaining--;

        if (pending.ticksRemaining == 0) {
            pending.active = false;
        }
    }
}

void Velocity::onUpdate() {
    if (!enabled || Menu::isOpen || mc.isInGui() || !player.isValid())
        return;

    const int currentHurtTime = player.getHurtTime();
    const bool justGotHit = currentHurtTime > 0 && currentHurtTime > lastHurtTime;

    if (justGotHit)
        queueHit(currentHurtTime);

    const bool tickDelayElapsed = queuedHit.active && currentHurtTime <= queuedHit.applyAtHurtTime;
    const bool microDelayElapsed = queuedHit.active &&
                                   static_cast<long>(GetTickCount() - queuedHit.applyAtMs) >= 0;
    if (tickDelayElapsed && microDelayElapsed)
        applyQueuedHit(currentHurtTime);

    continuePendingReduction(currentHurtTime);

    lastHurtTime = currentHurtTime;
}

float Velocity::getAttackerAngle() {
    // This would require tracking the last attacker position
    // For now, return 0 (approximated in checkFOV using motion)
    return 0.0f;
}

void Velocity::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();

            // Mode selector
            const char* modes[] = {"Regular", "Jump", "Reduce", "Ignore"};
            int modeIdx = static_cast<int>(settings.mode);
            if (ui::Combo("Mode", &modeIdx, modes, IM_ARRAYSIZE(modes)))
                settings.mode = static_cast<Mode>(modeIdx);

            ImGui::Spacing();

            // Mode-specific settings
            if (settings.mode == Mode::Regular || settings.mode == Mode::Reduce) {
                ui::SliderFloat("Horizontal", &settings.horizontal, 0.0f, 100.0f, "%.0f%%");
                ui::SliderFloat("Vertical", &settings.vertical, 0.0f, 100.0f, "%.0f%%");
                ui::SliderFloat("Chance", &settings.chance, 0.0f, 100.0f, "%.0f%%");
                if (settings.mode == Mode::Reduce) {
                    ui::Checkbox("Double Clicks", &settings.allowDoubleClicks);
                }
            } else if (settings.mode == Mode::Jump) {
                ui::SliderFloat("Jump Chance", &settings.jumpChance, 0.0f, 100.0f, "%.0f%%");
            } else if (settings.mode == Mode::Ignore) {
                ui::SliderFloat("Chance", &settings.chance, 0.0f, 100.0f, "%.0f%%");
                const char* vertModes[] = {"Never", "Only Air", "Always"};
                int vertIdx = static_cast<int>(settings.verticalMode);
                if (ui::Combo("Vertical", &vertIdx, vertModes, IM_ARRAYSIZE(vertModes)))
                    settings.verticalMode = static_cast<VerticalMode>(vertIdx);
            }

            ui::SliderFloat("FOV", &settings.fov, 0.0f, 360.0f, "%.0f");

            int delay = settings.reduceDelay;
            if (ui::SliderInt("Delay", &delay, 0, 5, "%d ticks"))
                settings.reduceDelay = delay;
        },
        "Settings",
        [this]() {
            // Anti-detection (only for Regular/Reduce modes)
            if (settings.mode == Mode::Regular || settings.mode == Mode::Reduce) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
                ImGui::Text("Anti-Detection");
                ImGui::PopStyleColor();
                ImGui::Spacing();

                ui::SliderFloat("Variance", &settings.percentageVariance, 0.0f, 10.0f, "%.1f%%");

                int spread = settings.tickSpread;
                if (ui::SliderInt("Tick Spread", &spread, 0, 3, "%d"))
                    settings.tickSpread = spread;

                int microDelay = settings.microDelayMs;
                if (ui::SliderInt("Micro Delay", &microDelay, 0, 50, "%dms"))
                    settings.microDelayMs = microDelay;

                ImGui::Spacing();
            }

            // Conditions
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Conditions");
            ImGui::PopStyleColor();
            ImGui::Spacing();

            ui::Checkbox("Looking at Player", &settings.onlyLookingAtPlayer);
            ui::Checkbox("Mouse Pressed", &settings.onlyMousePressed);
            ui::Checkbox("Moving Forward", &settings.onlyMovingForward);
            ui::Checkbox("Holding Weapon", &settings.onlyHoldingWeapon);
            if (settings.mode == Mode::Jump) {
                ui::Checkbox("Only Sprinting", &settings.onlySprinting);
            }
        });
}

std::string Velocity::getDisplayInfo() {
    switch (settings.mode) {
        case Mode::Regular:
            return "Regular";
        case Mode::Jump:
            return "Jump";
        case Mode::Reduce:
            return "Reduce";
        case Mode::Ignore:
            return "Ignore";
        default:
            return "";
    }
}



void Velocity::saveSettings(json::Value& node) const {
    node["mode"] = static_cast<int>(settings.mode);
    node["horizontal"] = settings.horizontal;
    node["vertical"] = settings.vertical;
    node["chance"] = settings.chance;
    node["fov"] = settings.fov;
    node["reduceDelay"] = settings.reduceDelay;
    node["jumpChance"] = settings.jumpChance;
    node["allowDoubleClicks"] = settings.allowDoubleClicks;
    node["verticalMode"] = static_cast<int>(settings.verticalMode);
    node["onlyLookingAtPlayer"] = settings.onlyLookingAtPlayer;
    node["onlyMousePressed"] = settings.onlyMousePressed;
    node["onlyMovingForward"] = settings.onlyMovingForward;
    node["onlyHoldingWeapon"] = settings.onlyHoldingWeapon;
    node["onlySprinting"] = settings.onlySprinting;
    node["percentageVariance"] = settings.percentageVariance;
    node["tickSpread"] = settings.tickSpread;
    node["microDelayMs"] = settings.microDelayMs;
}

void Velocity::loadSettings(const json::Value& node) {
    settings.mode = static_cast<Mode>(node["mode"].as_int(static_cast<int>(settings.mode)));
    settings.horizontal = node["horizontal"].as_float(settings.horizontal);
    settings.vertical = node["vertical"].as_float(settings.vertical);
    settings.chance = node["chance"].as_float(settings.chance);
    settings.fov = node["fov"].as_float(settings.fov);
    settings.reduceDelay = node["reduceDelay"].as_int(settings.reduceDelay);
    settings.jumpChance = node["jumpChance"].as_float(settings.jumpChance);
    settings.allowDoubleClicks = node["allowDoubleClicks"].as_bool(settings.allowDoubleClicks);
    settings.verticalMode =
        static_cast<VerticalMode>(node["verticalMode"].as_int(static_cast<int>(settings.verticalMode)));
    settings.onlyLookingAtPlayer = node["onlyLookingAtPlayer"].as_bool(settings.onlyLookingAtPlayer);
    settings.onlyMousePressed = node["onlyMousePressed"].as_bool(settings.onlyMousePressed);
    settings.onlyMovingForward = node["onlyMovingForward"].as_bool(settings.onlyMovingForward);
    settings.onlyHoldingWeapon = node["onlyHoldingWeapon"].as_bool(settings.onlyHoldingWeapon);
    settings.onlySprinting = node["onlySprinting"].as_bool(settings.onlySprinting);
    settings.percentageVariance = node["percentageVariance"].as_float(settings.percentageVariance);
    settings.tickSpread = node["tickSpread"].as_int(settings.tickSpread);
    settings.microDelayMs = node["microDelayMs"].as_int(settings.microDelayMs);
}
