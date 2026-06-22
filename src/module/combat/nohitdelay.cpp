#include "nohitdelay.h"
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

NoHitDelay* noHitDelay = nullptr;

NoHitDelay::NoHitDelay() : Module("NoHitDelay") {
    keybind = 0;
}

void NoHitDelay::onEnable() {
    // Nothing to initialize
}

void NoHitDelay::onDisable() {
    // Nothing to cleanup
}

void NoHitDelay::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Check left mouse button
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        return;

    // Weapon only check
    if (settings.weaponOnly) {
        int itemId = player.getHeldItemId();
        // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
        // Axes: 271, 275, 258, 279, 286
        bool isWeapon = (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
                         itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
        if (!isWeapon)
            return;
    }

    // Remove hit delay by setting leftClickCounter to 0
    // This counter normally prevents clicking for 10 ticks after a hit
    mc.setLeftClickCounter(0);
}

void NoHitDelay::onRenderMenu() {
    bool enabledValue = enabled;
    ui::section("Settings", [this, &enabledValue]() {
        if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
            if (enabledValue != enabled) {
                setEnabled(enabledValue);
            }
        }
        ImGui::Spacing();
        ui::Checkbox("Weapon Only", &settings.weaponOnly);
    });
}

void NoHitDelay::saveSettings(json::Value& node) const {
    node["weaponOnly"] = settings.weaponOnly;
}

void NoHitDelay::loadSettings(const json::Value& node) {
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
}
