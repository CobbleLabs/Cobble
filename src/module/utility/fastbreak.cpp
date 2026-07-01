#include "fastbreak.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>

FastBreak* fastBreak = nullptr;

FastBreak::FastBreak() : Module("FastBreak") {
    keybind = 0;
}

void FastBreak::onEnable() {
    lastDamageMs_ = 0;
}

void FastBreak::onDisable() {
    lastDamageMs_ = 0;
}

void FastBreak::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Only while left mouse button is held down (mining)
    if ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0)
        return;

    // Only when actually looking at a block
    if (!mc.isTargetingBlock())
        return;

    // Optionally skip when the hand is empty
    if (settings.blocksOnly && player.getHeldItemId() <= 0)
        return;

    // Throttle: 0 = pulse every tick (fastest mining)
    const std::uint64_t now = static_cast<std::uint64_t>(GetTickCount64());
    if (settings.delayMs > 0.0f && now - lastDamageMs_ < static_cast<std::uint64_t>(settings.delayMs))
        return;

    // Keep vanilla's click cooldown from fighting us, then add an extra damage
    // pulse on the crosshair block. damageTargetedBlock() uses the game's own
    // block position AND face, so our pulse matches the START packet vanilla
    // already sent — the server accepts the break instead of reverting it
    // (no more ghost block / having to mine twice).
    mc.setLeftClickCounter(0);
    mc.damageTargetedBlock();
    lastDamageMs_ = now;
}

void FastBreak::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ui::SliderFloat("Delay", &settings.delayMs, 0.0f, 200.0f, "%.0f ms");
        },
        "Options",
        [this]() { ui::Checkbox("Item In Hand Only", &settings.blocksOnly); });
}

void FastBreak::saveSettings(json::Value& node) const {
    node["delayMs"] = settings.delayMs;
    node["blocksOnly"] = settings.blocksOnly;
}

void FastBreak::loadSettings(const json::Value& node) {
    settings.delayMs = std::clamp(node["delayMs"].as_float(settings.delayMs), 0.0f, 200.0f);
    settings.blocksOnly = node["blocksOnly"].as_bool(settings.blocksOnly);
}
