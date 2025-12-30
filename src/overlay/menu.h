#pragma once

#include "../../ext/imgui/imgui.h"
#include "custom_widgets.hpp"

struct Module;

class Menu
{
public:
	static inline bool isOpen = false;
	static inline float openAnim = 0.f; // Animation for smooth open/close
	
	// Main render loop - call inside ImGui frame
	static void Render();
	
	// Apply pink Minecraft theme and rounded styles
	static void ApplyStyle();

	// Keybinding state
	static inline Module* moduleToBind = nullptr;
	
private:
	// Render tabs
	static void RenderCombatTab(float alpha);
	static void RenderVisualTab(float alpha);
	static void RenderSettingsTab(float alpha);
	
	// Helpers
	static void Hotkey(const char* label, Module* module);
};
