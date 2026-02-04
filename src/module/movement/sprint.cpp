#include "sprint.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cmath>

Sprint* sprint = nullptr;

Sprint::Sprint() : Module("Sprint") {
    keybind = 0;
}

void Sprint::onEnable() {
    // Cache the sprint key code when enabled
    if (mc.isValid()) {
        sprintKeyCode = mc.getSprintKey();
    }
}

void Sprint::onDisable() {
    // Release sprint key when disabled
    if (mc.isValid() && sprintKeyCode != 0) {
        mc.setKeyBindState(sprintKeyCode, false);
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

    // Toggle Sprint: simulate sprint key being held down
    // This is how Lunar Client does it - it's legit because
    // the game processes it as a normal key press
    if (sprintKeyCode == 0) {
        sprintKeyCode = mc.getSprintKey();
    }

    // Set the sprint key as pressed
    // The game will handle all the normal sprint logic (food check, blindness, etc)
    mc.setKeyBindState(sprintKeyCode, true);
}

void Sprint::onRenderMenu() {
    ui::two_column(
        "General", [this]() { 
            ui::CheckboxBind("Enabled", &enabled, &keybind); 
        }, 
        "Info",
        [this]() { 
            ImGui::TextWrapped("Toggle Sprint - automatically holds the sprint key");
        });
}
