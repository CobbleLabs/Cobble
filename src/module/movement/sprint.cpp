#include "sprint.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

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
    if (!player.isValid())
        return;

    // Sprint = not sneaking (reference implementation)
    // This is simpler and more reliable than checking keyboard state
    player.setSprinting(!player.isSneaking());
}

void Sprint::onRenderMenu() {
    ui::two_column(
        "General", [this]() { ui::CheckboxBind("Enabled", &enabled, &keybind); }, "Info",
        [this]() { ImGui::TextWrapped("Automatically sprints when not sneaking."); });
}
