#include "criticals.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
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
#include <random>

Criticals* criticals = nullptr;

static std::mt19937 rng(std::random_device{}());

Criticals::Criticals() : Module("Criticals") {
    keybind = 0;
    locked = true;  // hidden/grayed until experiments are unlocked
}

void Criticals::onEnable() {
    lastCriticalTime = 0;
    wasAttacking = false;
    groundSpoofTicks = 0;
    smoothCancel = false;
}

void Criticals::onDisable() {
    wasAttacking = false;
    groundSpoofTicks = 0;
    smoothCancel = false;
}

bool Criticals::isHoldingSword() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283);
}

bool Criticals::isLookingAtPlayer() {
    return mc.isTargetingEntity();
}

bool Criticals::checkChance() {
    if (settings.chance >= 100)
        return true;
    if (settings.chance <= 0)
        return false;
    std::uniform_int_distribution<int> dist(1, 100);
    return dist(rng) <= settings.chance;
}

bool Criticals::canDoCritical() {
    if (!player.isValid())
        return false;

    // Must be on ground
    if (!player.isOnGround())
        return false;

    // Check delay
    unsigned long now = GetTickCount();
    if (now - lastCriticalTime < (unsigned long)settings.maxDelayMs)
        return false;

    // Check conditions
    if (settings.onlyWhileAttacking) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            return false;
    }

    if (settings.lookingAtPlayer) {
        if (!isLookingAtPlayer())
            return false;
    }

    if (settings.onlyWithSword) {
        if (!isHoldingSword())
            return false;
    }

    // Don't do criticals while sneaking
    if (player.isSneaking())
        return false;

    return checkChance();
}

void Criticals::doPacketCritical() {
    // Packet mode: don't perform a real jump. Spoof the airborne state so the
    // outgoing movement packet reports onGround = false; the server then treats
    // the hit as a critical without any visible client-side movement.
    player.setOnGround(false);
}

void Criticals::doJumpCritical(double height) {
    // Real movement: give the player a small upward velocity so the client
    // actually leaves the ground for a frame. Visible, but works everywhere.
    player.setMotionY(height);
    player.setOnGround(false);
}

void Criticals::startCritical() {
    // Fire the technique for the selected mode. Modes that span more than one
    // tick seed sequence state that advanceSequence() drains on later ticks.
    switch (settings.mode) {
    case Mode::Packet:
        doPacketCritical();
        break;
    case Mode::MiniJump:
        doJumpCritical(0.12);
        break;
    case Mode::Jump:
        doJumpCritical(0.42);
        break;
    case Mode::NoGround:
        // Hold the airborne spoof for a few ticks so lenient/legacy checks
        // register the fall over several movement packets.
        player.setOnGround(false);
        groundSpoofTicks = 3;
        break;
    case Mode::Hop:
        // Lowest real hop that still leaves the ground; minimal displacement.
        doJumpCritical(0.0625);
        break;
    case Mode::Smooth:
        // Hop up now, cancel the velocity next tick so net movement is ~zero.
        player.setMotionY(0.11);
        player.setOnGround(false);
        smoothCancel = true;
        break;
    case Mode::NCP:
        // Legit jump-crit shape: only jump when strictly grounded so the
        // movement matches a real player jump.
        if (player.isOnGround())
            doJumpCritical(0.42);
        break;
    }
    lastCriticalTime = GetTickCount();
}

void Criticals::advanceSequence() {
    // Continue any in-progress multi-tick technique.
    if (groundSpoofTicks > 0) {
        player.setOnGround(false);
        groundSpoofTicks--;
    }
    if (smoothCancel) {
        player.setMotionY(-0.11);
        smoothCancel = false;
    }
}

void Criticals::onUpdate() {
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        wasAttacking = false;
        groundSpoofTicks = 0;
        smoothCancel = false;
        return;
    }

    advanceSequence();

    const bool isAttacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (isAttacking && !wasAttacking && canDoCritical())
        startCritical();

    wasAttacking = isAttacking;
}

void Criticals::onRenderMenu() {
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

            const char* modeItems[] = { "Packet", "MiniJump", "Jump",
                                        "NoGround", "Hop", "Smooth", "NCP" };
            int mode = static_cast<int>(settings.mode);
            if (ui::Combo("Mode", &mode, modeItems, IM_ARRAYSIZE(modeItems)))
                settings.mode = static_cast<Mode>(mode);

            int delay = settings.maxDelayMs;
            if (ui::SliderInt("Delay", &delay, 50, 500, "%dms"))
                settings.maxDelayMs = delay;

            int chance = settings.chance;
            if (ui::SliderInt("Chance", &chance, 0, 100, "%d%%"))
                settings.chance = chance;
        },
        "Conditions",
        [this]() {
            ui::Checkbox("Only Attacking", &settings.onlyWhileAttacking);
            ui::Checkbox("Looking At Player", &settings.lookingAtPlayer);
            ui::Checkbox("Only With Sword", &settings.onlyWithSword);
        });
}



void Criticals::saveSettings(json::Value& node) const {
    node["mode"] = static_cast<int>(settings.mode);
    node["maxDelayMs"] = settings.maxDelayMs;
    node["chance"] = settings.chance;
    node["onlyWhileAttacking"] = settings.onlyWhileAttacking;
    node["lookingAtPlayer"] = settings.lookingAtPlayer;
    node["onlyWithSword"] = settings.onlyWithSword;
}

void Criticals::loadSettings(const json::Value& node) {
    int rawMode = node["mode"].as_int(static_cast<int>(settings.mode));
    // Migration: "Grim" (old index 6) was removed — it was just a real jump, which
    // the other jump modes already cover. Old configs storing it fall back to Jump;
    // the old NCP (index 7) shifts down to its new value 6.
    if (rawMode == 6)
        rawMode = static_cast<int>(Mode::Jump);
    else if (rawMode == 7)
        rawMode = static_cast<int>(Mode::NCP);
    settings.mode = static_cast<Mode>(std::clamp(rawMode, 0, static_cast<int>(Mode::NCP)));
    settings.maxDelayMs = node["maxDelayMs"].as_int(settings.maxDelayMs);
    settings.chance = node["chance"].as_int(settings.chance);
    settings.onlyWhileAttacking = node["onlyWhileAttacking"].as_bool(settings.onlyWhileAttacking);
    settings.lookingAtPlayer = node["lookingAtPlayer"].as_bool(settings.lookingAtPlayer);
    settings.onlyWithSword = node["onlyWithSword"].as_bool(settings.onlyWithSword);
}
