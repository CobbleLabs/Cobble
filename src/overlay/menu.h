#pragma once

#include "../../ext/imgui/imgui.h"

struct Module;

class Menu
{
public:
	static inline bool isOpen = false;
	
	// Main render loop - call inside ImGui frame
	static void Render();
	
	// Apply dark theme and rounded styles
	static void ApplyStyle();

	// Keybinding state
	static inline Module* moduleToBind = nullptr;
	
private:
	// Render tabs
	static void RenderCombatTab();
	static void RenderVisualTab();
	static void RenderSettingsTab();
	
	// Helpers
	static void ToggleButton(const char* label, bool* v);
	static void Hotkey(const char* label, Module* module);
};
