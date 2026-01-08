#include "sprint.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

Sprint* sprint = nullptr;

Sprint::Sprint() : Module("Sprint")
{
	keybind = 0;
}

void Sprint::onEnable()
{
}

void Sprint::onDisable()
{
	if (player.isValid()) {
		player.setSprinting(false);
	}
}

void Sprint::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;

	// Auto-sprint when moving forward and not already sprinting
	if (!player.isSprinting()) {
		player.setSprinting(true);
	}
}

void Sprint::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
		},
		"Info", [this]() {
			ImGui::TextWrapped("Automatically sprints without holding the vanilla sprint bind.");
		}
	);
}
