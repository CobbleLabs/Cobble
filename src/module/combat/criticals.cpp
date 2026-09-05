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

// Hits trigger bounds. 1 means "every hit", which is the Chance-100 equivalent.
static constexpr int MIN_HIT_INTERVAL = 1;
static constexpr int MAX_HIT_INTERVAL = 10;

// How long a combo can stall before the next hit counts as a fresh streak rather
// than a continuation. Deliberately well above a 1 CPS rhythm (1000 ms between
// hits) so slow, deliberate combos still build up instead of resetting to 1.
static constexpr unsigned long COMBO_TIMEOUT_MS = 3000;

Criticals::Criticals() : Module("Criticals") {
    keybind = 0;
    locked = false;  // hidden/grayed until experiments are unlocked
}

void Criticals::onEnable() {
    lastCriticalTime = 0;
    wasAttacking = false;
    hitCount = 0;
    lastHitTime = 0;
}

void Criticals::onDisable() {
    wasAttacking = false;
    hitCount = 0;
    lastHitTime = 0;
}

bool Criticals::isRecentlyCritical(unsigned long windowMs) const {
    if (!enabled)
        return false;
    if (lastCriticalTime == 0)
        return false;
    return (GetTickCount() - lastCriticalTime) < windowMs;
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

bool Criticals::isValidAttack() {
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

    return true;
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

    // Don't do criticals while sneaking
    if (player.isSneaking())
        return false;

    return true;
}

void Criticals::handleComboHit() {
    const unsigned long now = GetTickCount();

    // A swing with nothing under the crosshair is a miss, which ends the combo.
    // Checked directly rather than through the "Looking At Player" toggle: the
    // streak has no meaning without knowing whether the hit actually landed.
    if (!isLookingAtPlayer()) {
        hitCount = 0;
        return;
    }

    // Long enough gap since the last landed hit means the previous combo is over
    // and this is the first hit of a new one.
    if (hitCount > 0 && lastHitTime != 0 && now - lastHitTime > COMBO_TIMEOUT_MS)
        hitCount = 0;

    hitCount++;
    lastHitTime = now;

    // Cap so a long streak that never gets to fire cannot overflow; >= still fires.
    if (hitCount > settings.hitInterval)
        hitCount = settings.hitInterval;

    if (hitCount < settings.hitInterval)
        return;

    if (settings.onlyWithSword && !isHoldingSword())
        return;

    // Not ready (airborne, sneaking, still inside the delay): hold the streak at
    // the target so the next landed hit fires instead of losing the beat.
    if (!canDoCritical())
        return;

    startCritical();
    hitCount = 0;
}

void Criticals::doJumpCritical(double height) {
    // Real movement: give the player a small upward velocity so the client
    // actually leaves the ground for a frame. Visible, but works everywhere.
    player.setMotionY(height);
    player.setOnGround(false);
}

void Criticals::startCritical() {
    switch (settings.mode) {
        case Mode::MiniJump:
            doJumpCritical(0.12);
            break;
        case Mode::Jump:
            doJumpCritical(0.42);
            break;
    }
    lastCriticalTime = GetTickCount();
}

void Criticals::onUpdate() {
    if (!enabled)
        return;

    if (Menu::isOpen || mc.isInGui() || !player.isValid()) {
        wasAttacking = false;
        return;
    }

    const bool isAttacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // Rising edge of the attack button.
    if (isAttacking && !wasAttacking) {
        if (settings.trigger == Trigger::Hits) {
            // Streak tracking has to see every swing, including the ones that
            // miss, so it cannot sit behind isValidAttack().
            handleComboHit();
        } else if (isValidAttack() && canDoCritical() && checkChance()) {
            startCritical();
        }
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

            const char* modeItems[] = {"MiniJump", "Jump"};
            int mode = static_cast<int>(settings.mode);
            if (ui::Combo("Mode", &mode, modeItems, IM_ARRAYSIZE(modeItems)))
                settings.mode = static_cast<Mode>(mode);

            const char* triggerItems[] = {"Chance", "Hits"};
            int trigger = static_cast<int>(settings.trigger);
            if (ui::Combo("Trigger", &trigger, triggerItems, IM_ARRAYSIZE(triggerItems)))
                settings.trigger = static_cast<Trigger>(trigger);

            // Only one of the two is meaningful at a time: Hits is a fixed cadence,
            // so rolling a chance on top of it would defeat the point.
            if (settings.trigger == Trigger::Hits) {
                int hits = settings.hitInterval;
                if (ui::SliderInt("Combo Hits", &hits, MIN_HIT_INTERVAL, MAX_HIT_INTERVAL, "%d"))
                    settings.hitInterval = hits;
            } else {
                int chance = settings.chance;
                if (ui::SliderInt("Chance", &chance, 0, 100, "%d%%"))
                    settings.chance = chance;
            }

            int delay = settings.maxDelayMs;
            if (ui::SliderInt("Delay", &delay, 50, 500, "%dms"))
                settings.maxDelayMs = delay;
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
    node["trigger"] = static_cast<int>(settings.trigger);
    node["hitInterval"] = settings.hitInterval;
    node["maxDelayMs"] = settings.maxDelayMs;
    node["chance"] = settings.chance;
    node["onlyWhileAttacking"] = settings.onlyWhileAttacking;
    node["lookingAtPlayer"] = settings.lookingAtPlayer;
    node["onlyWithSword"] = settings.onlyWithSword;
}

void Criticals::loadSettings(const json::Value& node) {
    const int rawMode = node["mode"].as_int(static_cast<int>(settings.mode));
    // Only MiniJump (0) and Jump (1) are left. Older configs could hold any of
    // Packet (0), MiniJump (1), Jump (2), NoGround (3), Hop (4), Smooth (5) or
    // NCP (6): the full-height techniques land on Jump, the rest on MiniJump.
    settings.mode = (rawMode == 1 || rawMode == 2 || rawMode == 6) ? Mode::Jump : Mode::MiniJump;
    // Absent in older configs, so it defaults to Chance and existing setups keep
    // behaving exactly as before.
    const int rawTrigger = node["trigger"].as_int(static_cast<int>(settings.trigger));
    settings.trigger = rawTrigger == 1 ? Trigger::Hits : Trigger::Chance;
    settings.hitInterval =
        (std::clamp)(node["hitInterval"].as_int(settings.hitInterval), MIN_HIT_INTERVAL, MAX_HIT_INTERVAL);
    settings.maxDelayMs = node["maxDelayMs"].as_int(settings.maxDelayMs);
    settings.chance = node["chance"].as_int(settings.chance);
    settings.onlyWhileAttacking = node["onlyWhileAttacking"].as_bool(settings.onlyWhileAttacking);
    settings.lookingAtPlayer = node["lookingAtPlayer"].as_bool(settings.lookingAtPlayer);
    settings.onlyWithSword = node["onlyWithSword"].as_bool(settings.onlyWithSword);
}
