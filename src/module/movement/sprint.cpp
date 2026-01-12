#include "sprint.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

Sprint* sprint = nullptr;

Sprint::Sprint() : Module("Sprint") {
    keybind = 0;
}

void Sprint::onEnable() {}

void Sprint::onDisable() {
    if (player.isValid()) {
        player.setSprinting(false);
    }
}

void Sprint::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Only sprint when actually moving forward
    bool isMovingForward = (GetAsyncKeyState('W') & 0x8000) != 0;

    if (isMovingForward && !player.isSprinting()) {
        player.setSprinting(true);
    }
}

void Sprint::onRenderMenu() {
    ui::two_column(
        "General", [this]() { ui::CheckboxBind("Enabled", &enabled, &keybind); }, "Info",
        [this]() { ImGui::TextWrapped("Automatically sprints without holding the vanilla sprint bind."); });
}
