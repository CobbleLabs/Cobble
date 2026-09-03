#include "autorod.h"

#include "autoclicker.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#include <Windows.h>
#include <algorithm>

AutoRod* autoRod = nullptr;

AutoRod::AutoRod() : Module("AutoRod"), rng_(std::random_device{}()) {
    keybind = 0;
}

bool AutoRod::isWeapon(int itemId) {
    switch (itemId) {
        case 267: case 268: case 272: case 276: case 283:
        case 258: case 271: case 275: case 279: case 286:
            return true;
        default:
            return false;
    }
}

int AutoRod::findRodSlot() const {
    for (int slot = 0; slot < 9; ++slot) {
        if (player.getItemInSlot(slot) == 346)
            return slot;
    }
    return -1;
}

// Deterministic delays are the cheapest thing in the module to fingerprint: a
// histogram of the inter-packet gaps collapses to one spike, identical on every
// sequence. Spread every phase length instead.
int AutoRod::jitterTicks(int baseTicks) {
    const int base = std::max(1, baseTicks);
    const int spread = std::clamp(settings.jitterPercent, 0, 90);
    if (spread == 0)
        return base;
    // An integer percentage of a 2-3 tick delay rounds to zero, so guarantee at
    // least +-1 tick of play.
    const int amp = std::max(1, (base * spread) / 100);
    std::uniform_int_distribution<int> dist(-amp, amp);
    return std::max(1, base + dist(rng_));
}

int AutoRod::rollCooldownMs() {
    const int base = std::clamp(settings.cooldownMs, 400, 6000);
    const int spread = std::clamp(settings.jitterPercent, 0, 90);
    if (spread == 0)
        return base;
    const int amp = std::max(1, (base * spread) / 100);
    std::uniform_int_distribution<int> dist(-amp, amp);
    return std::max(300, base + dist(rng_));
}

void AutoRod::abortSequence(bool restoreSlot) {
    if (restoreSlot && rodSlot_ >= 0 && weaponSlot_ >= 0 && player.getCurrentSlot() == rodSlot_)
        player.setCurrentSlot(weaponSlot_);
    phase_ = Phase::Idle;
    waitTicks_ = 0;
    rodSlot_ = weaponSlot_ = -1;
}

void AutoRod::onEnable() {
    phase_ = Phase::Idle;
    waitTicks_ = 0;
    rodSlot_ = weaponSlot_ = -1;
    prevAttackDown_ = false;
    requestPending_.store(false, std::memory_order_relaxed);
    observedAutoClick_ = autoClicker ? autoClicker->lastAutoClick : std::chrono::steady_clock::time_point{};
    cooldownMsThisRound_ = 0;
    lastSequence_ = std::chrono::steady_clock::now() - std::chrono::hours(1);
}

void AutoRod::onDisable() {
    // A bare slot restore is safe off the client thread: it is a single write to
    // inventory.currentItem, exactly what a hotbar keypress does, and there is no
    // pending use-item for it to desync from.
    abortSequence(true);
    requestPending_.store(false, std::memory_order_relaxed);
}

// Module thread (~500 Hz). This must not touch the game: hotbar changes, use-item
// and attacks all belong to onClientTick() so they land tick-aligned and in vanilla
// packet order. All that happens here is latching the attack intent.
void AutoRod::onUpdate() {
    if (!enabled || Menu::isOpen) {
        prevAttackDown_ = false;
        requestPending_.store(false, std::memory_order_relaxed);
        return;
    }

    const bool attackDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool attackPressed = attackDown && !prevAttackDown_;
    prevAttackDown_ = attackDown;

    bool autoClick = false;
    if (autoClicker && autoClicker->lastAutoClick != observedAutoClick_) {
        observedAutoClick_ = autoClicker->lastAutoClick;
        autoClick = true;
    }

    if (attackPressed || autoClick || !settings.onlyWhileAttacking)
        requestPending_.store(true, std::memory_order_release);
}

void AutoRod::onClientTick(JNIEnv* env) {
    (void)env;
    if (!enabled || !player.isValid() || mc.isInGui()) {
        abortSequence(true);
        requestPending_.store(false, std::memory_order_relaxed);
        return;
    }

    // One phase transition per tick. waitTicks_ counts the EXTRA ticks a phase
    // holds, so a phase of length N sets waitTicks_ = N - 1 and fires N ticks later.
    if (waitTicks_ > 0) {
        --waitTicks_;
        return;
    }

    switch (phase_) {
        case Phase::RodSelected:
            // syncCurrentPlayItem() ran at the top of this tick, so C09(rod) is on
            // the wire and the rod really is the held item server-side.
            if (player.getCurrentSlot() != rodSlot_ || mc.getRightClickDelay() > 0) {
                abortSequence(true);
                return;
            }
            // Minecraft.rightClickMouse() sets rightClickDelayTimer = 4 itself, and
            // that is what keeps the use-item cadence vanilla. The old code zeroed
            // the timer first and never restored it, so every right-click for the
            // rest of the session bypassed the 4-tick cooldown.
            mc.rightClickMouse();
            phase_ = Phase::RodCast;
            waitTicks_ = jitterTicks(std::clamp(settings.rodHoldTicks, 1, 20)) - 1;
            return;

        case Phase::RodCast:
            if (player.getCurrentSlot() == rodSlot_ && weaponSlot_ >= 0)
                player.setCurrentSlot(weaponSlot_);
            phase_ = Phase::WeaponSelected;
            waitTicks_ = jitterTicks(std::clamp(settings.hitDelayTicks, 1, 20)) - 1;
            return;

        case Phase::WeaponSelected:
            if (settings.autoHit && weaponSlot_ >= 0 && player.getCurrentSlot() == weaponSlot_ &&
                mc.isTargetingEntity()) {
                mc.clickMouse();
            }
            lastSequence_ = std::chrono::steady_clock::now();
            cooldownMsThisRound_ = 0;
            abortSequence(false);
            return;

        case Phase::Idle:
            break;
    }

    // ---- Idle: consider starting a sequence. ----
    if (!requestPending_.exchange(false, std::memory_order_acq_rel))
        return;

    if (cooldownMsThisRound_ == 0)
        cooldownMsThisRound_ = rollCooldownMs();
    if (std::chrono::steady_clock::now() - lastSequence_ < std::chrono::milliseconds(cooldownMsThisRound_))
        return;

    // objectMouseOver is this frame's raycast and is already fresh on this thread.
    // The old code called EntityRenderer.getMouseOver() from the module loop,
    // racing the client thread that owns it.
    if (!mc.isTargetingEntity())
        return;
    // A human could not have right-clicked during vanilla's own cooldown either.
    if (mc.getRightClickDelay() > 0)
        return;

    const int currentSlot = player.getCurrentSlot();
    if (settings.requireWeapon && !isWeapon(player.getHeldItemId()))
        return;
    const int rodSlot = findRodSlot();
    if (rodSlot < 0 || rodSlot == currentSlot)
        return;

    weaponSlot_ = currentSlot;
    rodSlot_ = rodSlot;
    player.setCurrentSlot(rodSlot_);
    phase_ = Phase::RodSelected;
    waitTicks_ = 0;  // cast on the next tick, once C09(rod) has gone out
    lastSequence_ = std::chrono::steady_clock::now();
    cooldownMsThisRound_ = 0;
}

std::string AutoRod::getDisplayInfo() {
    return phase_ == Phase::Idle ? "Ready" : "Rod > Hit";
}

void AutoRod::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::Checkbox("Only While Attacking", &settings.onlyWhileAttacking);
            ui::Checkbox("Require Weapon", &settings.requireWeapon);
            ui::Checkbox("Auto Hit", &settings.autoHit);
        },
        "Timing",
        [this]() {
            ui::SliderInt("Rod Hold (ticks)", &settings.rodHoldTicks, 1, 20, "%d");
            ui::SliderInt("Hit Delay (ticks)", &settings.hitDelayTicks, 1, 20, "%d");
            ui::SliderInt("Cooldown (ms)", &settings.cooldownMs, 400, 6000, "%d");
            ui::SliderInt("Timing Jitter (%)", &settings.jitterPercent, 0, 90, "%d");
        });
}

void AutoRod::saveSettings(json::Value& node) const {
    node["onlyWhileAttacking"] = settings.onlyWhileAttacking;
    node["requireWeapon"] = settings.requireWeapon;
    node["autoHit"] = settings.autoHit;
    node["rodHoldTicks"] = settings.rodHoldTicks;
    node["hitDelayTicks"] = settings.hitDelayTicks;
    node["cooldownMs"] = settings.cooldownMs;
    node["jitterPercent"] = settings.jitterPercent;
}

void AutoRod::loadSettings(const json::Value& node) {
    settings.onlyWhileAttacking = node["onlyWhileAttacking"].as_bool(settings.onlyWhileAttacking);
    settings.requireWeapon = node["requireWeapon"].as_bool(settings.requireWeapon);
    settings.autoHit = node["autoHit"].as_bool(settings.autoHit);
    settings.rodHoldTicks = node["rodHoldTicks"].as_int(settings.rodHoldTicks);
    settings.hitDelayTicks = node["hitDelayTicks"].as_int(settings.hitDelayTicks);
    settings.cooldownMs = node["cooldownMs"].as_int(settings.cooldownMs);
    settings.jitterPercent = node["jitterPercent"].as_int(settings.jitterPercent);
}
