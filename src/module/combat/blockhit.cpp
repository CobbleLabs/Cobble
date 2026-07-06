#include "blockhit.h"

#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/core/overlay.h"
#include "../../ui/hud/notifications.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"
#include "autoclicker.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

BlockHit* blockHit = nullptr;

BlockHit::BlockHit() : Module("BlockHit") {
    keybind = 0;
}

void BlockHit::onEnable() {}

void BlockHit::onDisable() {
    setBlocking(false);
}

void BlockHit::setBlocking(bool block) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd || hwnd != Overlay::gameWindow) {
        if (blocking_ && hwnd) {
            PostMessage(hwnd, WM_RBUTTONUP, 0, 0);
            blocking_ = false;
        }
        return;
    }
    if (block && !blocking_) {
        PostMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
        blocking_ = true;
    } else if (!block && blocking_) {
        PostMessage(hwnd, WM_RBUTTONUP, 0, 0);
        blocking_ = false;
    }
}

bool BlockHit::isHoldingSword() {
    int id = player.getHeldItemId();
    return id == 268 || id == 272 || id == 267 || id == 276 || id == 283;  // wood/stone/iron/diamond/gold
}

void BlockHit::onUpdate() {
    if (!enabled) {
        setBlocking(false);
        return;
    }

    // Mode switch keybind (edge-triggered).
    if (settings.modeKeybind != 0) {
        bool k = (GetAsyncKeyState(settings.modeKeybind) & 0x8000) != 0;
        if (k && !prevModeKey_) {
            settings.mode = settings.mode == (int)Mode::Perfect ? (int)Mode::Adaptive : (int)Mode::Perfect;
            NotificationManager::addSystem(settings.mode == (int)Mode::Perfect ? "BlockHit: Perfect"
                                                                               : "BlockHit: Adaptive");
        }
        prevModeKey_ = k;
    }

    if (!player.isValid() || mc.isInGui()) {
        setBlocking(false);
        return;
    }

    const bool holdingLeft = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    if (holdingLeft && !prevLeftDown_)
        lastPhysAttack_ = std::chrono::steady_clock::now();
    prevLeftDown_ = holdingLeft;

    if (!holdingLeft || (settings.weaponOnly && !isHoldingSword())) {
        setBlocking(false);
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    // Most recent attack, from the AutoClicker's attacks or a manual click edge.
    auto lastAttack = lastPhysAttack_;
    if (autoClicker && autoClicker->lastAutoClick > lastAttack)
        lastAttack = autoClicker->lastAutoClick;
    const double msSince = std::chrono::duration<double, std::milli>(now - lastAttack).count();
    const bool inAttackWindow = msSince < settings.unblockWindowMs;

    // Perfect: block whenever fighting. Adaptive: block only while taking hits.
    const bool shouldBlockBase =
        (settings.mode == (int)Mode::Perfect) ? true : (player.getHurtTime() > 0);

    // Drop the block during the attack window so the hit lands clean.
    setBlocking(shouldBlockBase && !inAttackWindow);
}

std::string BlockHit::getDisplayInfo() {
    return settings.mode == (int)Mode::Perfect ? "Perfect" : "Adaptive";
}

void BlockHit::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::Binder("Keybind", &keybind);
            ImGui::Spacing();
            const char* modes[] = {"Adaptive", "Perfect"};
            ui::Combo("Mode", &settings.mode, modes, IM_ARRAYSIZE(modes));
            ui::Binder("Switch Mode Key", &settings.modeKeybind);
        },
        "Tuning",
        [this]() {
            ui::SliderInt("Unblock Window (ms)", &settings.unblockWindowMs, 0, 200, "%d");
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
        });
}

void BlockHit::saveSettings(json::Value& node) const {
    node["mode"] = settings.mode;
    node["modeKeybind"] = settings.modeKeybind;
    node["unblockWindowMs"] = settings.unblockWindowMs;
    node["weaponOnly"] = settings.weaponOnly;
}

void BlockHit::loadSettings(const json::Value& node) {
    settings.mode = node["mode"].as_int(settings.mode);
    settings.modeKeybind = node["modeKeybind"].as_int(settings.modeKeybind);
    settings.unblockWindowMs = node["unblockWindowMs"].as_int(settings.unblockWindowMs);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
}
