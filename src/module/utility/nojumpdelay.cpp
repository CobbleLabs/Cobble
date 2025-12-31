#include "nojumpdelay.h"
#include "../../sdk/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

NoJumpDelay* noJumpDelay = nullptr;

NoJumpDelay::NoJumpDelay() : Module("NoJumpDelay")
{
	keybind = 0;
}

void NoJumpDelay::onEnable()
{
}

void NoJumpDelay::onDisable()
{
}

void NoJumpDelay::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;

	// Reset jump ticks when on ground to remove jump delay
	// This allows instant re-jump when landing (bhop)
	if (player.isOnGround() && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
		player.setJumpTicks(0);
	}
}

void NoJumpDelay::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
		},
		"Info", [this]() {
			ImGui::TextWrapped("Removes jump delay for bhop.");
		}
	);
}
