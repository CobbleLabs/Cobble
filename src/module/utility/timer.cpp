#include "timer.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

TimerMod* timerModule = nullptr;

TimerMod::TimerMod() : Module("Timer")
{
	keybind = 0;
}

void TimerMod::onEnable()
{
}

void TimerMod::onDisable()
{
	// Reset timer speed to normal when disabled
	mc.setTimerSpeed(1.0f);
}

// Helper: Check if holding a weapon (sword, axe)
static bool isHoldingWeapon()
{
	int itemId = player.getHeldItemId();
	// Swords: 267 (iron), 268 (wood), 272 (stone), 276 (diamond), 283 (gold)
	// Axes: 258 (iron), 271 (wood), 275 (stone), 279 (diamond), 286 (gold)
	switch (itemId) {
		case 267: case 268: case 272: case 276: case 283: // Swords
		case 258: case 271: case 275: case 279: case 286: // Axes
			return true;
		default:
			return false;
	}
}

void TimerMod::onUpdate()
{
	// Always reset to normal first if disabled
	if (!enabled) {
		return;
	}
	
	if (Menu::isOpen) {
		mc.setTimerSpeed(1.0f);
		return;
	}
	
	if (!player.isValid()) {
		mc.setTimerSpeed(1.0f);
		return;
	}
	
	// Check conditions
	bool shouldApply = true;
	
	// Mouse Pressed check - only apply when left clicking
	if (settings.mousePressed) {
		if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
			shouldApply = false;
		}
	}
	
	// Weapon Only check
	if (settings.weaponOnly) {
		if (!isHoldingWeapon()) {
			shouldApply = false;
		}
	}
	
	// Apply timer speed or reset to normal
	if (shouldApply) {
		mc.setTimerSpeed(settings.speed);
	} else {
		mc.setTimerSpeed(1.0f);
	}
}

void TimerMod::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			
			// Store old value to detect slider drag vs manual input
			double oldSpeed = settings.speed;
			
			// Use float for slider, then convert back
			float speedFloat = (float)settings.speed;
			
			// Dynamic format based on precision needed
			// Check if value has 3rd decimal precision
			double scaled = settings.speed * 1000.0;
			bool needs3Dec = std::abs(scaled - std::round(scaled / 10.0) * 10.0) > 0.5;
			const char* fmt = needs3Dec ? "%.3fx" : "%.2fx";
			
			// Slider for dragging, allows Ctrl+Click for manual input
			bool changed = ui::SliderFloat("Speed", &speedFloat, 1.0f, 3.0f, fmt, true);
			
			if (changed) {
				// Keep exact value - no rounding, allows 1.01, 1.02, etc.
				settings.speed = (double)speedFloat;
			}
			
			// Clamp to valid range (1.00 to 3.00)
			if (settings.speed < 1.0) settings.speed = 1.0;
			if (settings.speed > 3.0) settings.speed = 3.0;
		},
		"Conditions", [this]() {
			ui::Checkbox("Mouse Pressed", &settings.mousePressed);
			ui::Checkbox("Weapon Only", &settings.weaponOnly);
		}
	);
}

std::string TimerMod::getDisplayInfo()
{
	char buf[32];
	// Show 3 decimals if value needs it, otherwise 2
	double scaled = settings.speed * 1000.0;
	bool needs3Dec = std::abs(scaled - std::round(scaled / 10.0) * 10.0) > 0.5;
	snprintf(buf, sizeof(buf), needs3Dec ? "%.3fx" : "%.2fx", settings.speed);
	return buf;
}

