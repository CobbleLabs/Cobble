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
#include <random>

Criticals* criticals = nullptr;

static std::mt19937 rng(std::random_device{}());

Criticals::Criticals() : Module("Criticals") {
    keybind = 0;
}

void Criticals::onEnable() {
    lastCriticalTime = 0;
    wasAttacking = false;
}

void Criticals::onDisable() {
    wasAttacking = false;
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

void Criticals::onUpdate() {
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        wasAttacking = false;
        return;
    }

    const bool isAttacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    if (isAttacking && !wasAttacking && canDoCritical()) {
        // A small real movement is safer than constructing version-specific
        // packets from the module worker thread.
        player.setMotionY(0.12);
        player.setOnGround(false);
        lastCriticalTime = GetTickCount();
    }

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
    node["maxDelayMs"] = settings.maxDelayMs;
    node["chance"] = settings.chance;
    node["onlyWhileAttacking"] = settings.onlyWhileAttacking;
    node["lookingAtPlayer"] = settings.lookingAtPlayer;
    node["onlyWithSword"] = settings.onlyWithSword;
}

void Criticals::loadSettings(const json::Value& node) {
    settings.maxDelayMs = node["maxDelayMs"].as_int(settings.maxDelayMs);
    settings.chance = node["chance"].as_int(settings.chance);
    settings.onlyWhileAttacking = node["onlyWhileAttacking"].as_bool(settings.onlyWhileAttacking);
    settings.lookingAtPlayer = node["lookingAtPlayer"].as_bool(settings.lookingAtPlayer);
    settings.onlyWithSword = node["onlyWithSword"].as_bool(settings.onlyWithSword);
}
