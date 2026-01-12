#include "fastplace.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

FastPlace* fastPlace = nullptr;

FastPlace::FastPlace() : Module("FastPlace")
{
	keybind = 0;
}

void FastPlace::onEnable()
{
}

void FastPlace::onDisable()
{
}

void FastPlace::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;

	// Only when right mouse button is held
	if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
		return;

	// Check if holding a block (if blocksOnly is enabled)
	// Block IDs in Minecraft 1.8 are 1-255
	if (settings.blocksOnly) {
		int heldItemId = player.getHeldItemId();
		if (heldItemId <= 0 || heldItemId > 255)
			return;
	}

	// Reset delay to 0 for instant placement
	int currentDelay = mc.getRightClickDelay();
	if (currentDelay > 0) {
		mc.setRightClickDelay(0);
	}
}

void FastPlace::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
		},
		"Options", [this]() {
			ui::Checkbox("Blocks Only", &settings.blocksOnly);
			ImGui::Spacing();
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Removes right-click delay for faster block placement.");
			ImGui::PopStyleColor();
		}
	);
}
