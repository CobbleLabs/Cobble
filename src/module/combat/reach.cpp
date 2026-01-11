#include "reach.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <random>

Reach* reach = nullptr;

static std::mt19937 rng(std::random_device{}());

Reach::Reach() : Module("Reach")
{
	keybind = 0;
	locked = true;
}

void Reach::onEnable()
{
	currentReach = settings.distance;
}

void Reach::onDisable()
{
}

bool Reach::isHoldingWeapon()
{
	int itemId = player.getHeldItemId();
	// Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
	// Axes: 271, 275, 258, 279, 286
	return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
			itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

void Reach::updateReach()
{
	if (settings.randomize) {
		std::uniform_real_distribution<float> dist(settings.minDistance, settings.maxDistance);
		currentReach = dist(rng);
	} else {
		currentReach = settings.distance;
	}
}

void Reach::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;
	
	// Check conditions
	if (settings.weaponsOnly && !isHoldingWeapon()) return;
	if (settings.sprintingOnly && !player.isSprinting()) return;
	if (settings.movingOnly && !(GetAsyncKeyState('W') & 0x8000)) return;
	
	// Only process when left clicking
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;
	
	// Update reach value (randomize if enabled)
	updateReach();
	
	// The actual reach modification would require intercepting the raycast
	// For now, we extend the bounding box check distance
	// This is a simplified version - full implementation needs JNI hooks
	
	// Set the reach distance in Minecraft (if method available)
	// mc.setReachDistance(currentReach);
	
	// Note: Full reach implementation requires hooking into:
	// - Entity ray tracing
	// - Or modifying objectMouseOver distance check
	// This basic version stores the reach value for use by other systems
}

void Reach::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::Checkbox("Randomize", &settings.randomize);
			if (settings.randomize) {
				ui::SliderFloat("Min Distance", &settings.minDistance, 3.0f, 6.0f, "%.1f");
				ui::SliderFloat("Max Distance", &settings.maxDistance, 3.0f, 6.0f, "%.1f");
			} else {
				ui::SliderFloat("Distance", &settings.distance, 3.0f, 6.0f, "%.1f");
			}
		},
		"Conditions", [this]() {
			ui::Checkbox("Weapons Only", &settings.weaponsOnly);
			ui::Checkbox("Sprinting Only", &settings.sprintingOnly);
			ui::Checkbox("Moving Only", &settings.movingOnly);
		}
	);
}

std::string Reach::getDisplayInfo() {
	char buf[16];
	if (settings.randomize) {
		snprintf(buf, sizeof(buf), "%.1f-%.1f", settings.minDistance, settings.maxDistance);
	} else {
		snprintf(buf, sizeof(buf), "%.1f", settings.distance);
	}
	return buf;
}

