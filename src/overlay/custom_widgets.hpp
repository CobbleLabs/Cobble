#pragma once
/*
 * Custom ImGui Widgets for Cobble Client
 * Weave-style animated tabs, subtabs, and styled children
 */

#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_internal.h"
#include "imgui_tricks.hpp"
#include "hashes.hpp"

// Menu state
inline int currentTab = 0;
inline int currentSubtab = 0;
inline float contentAnim = 0.f;

// Custom color index for scheme
#define ImGuiCol_Scheme (ImGuiCol_COUNT)

// Color picker flags (weave style)
#define COBBLE_COLOR_ALPHA    (ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder)
#define COBBLE_COLOR_NO_ALPHA (ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoBorder)

namespace custom {
	// Global Accent Color (Default: Blue weave theme)
	inline float AccentColor[4] = { 0.4f, 0.6f, 1.0f, 1.0f }; // Blue #6699FF

	// Tab icon strings (FontAwesome) - kept for backwards compatibility
	constexpr const char* Icon_Combat = ICON_FA_SKULL;
	constexpr const char* Icon_Visual = ICON_FA_EYE;
	constexpr const char* Icon_Settings = ICON_FA_GEAR;

	// Sidebar tab with FontAwesome icon string and label (weave style)
	bool tab(const char* icon, const char* label, bool selected);
	
	// Custom modern slider
	bool slider_float(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f");
	bool slider_int(const char* label, int* v, int v_min, int v_max, const char* format = "%d");
	
	// Checkbox with scaled text emphasis
	bool checkbox(const char* label, bool* v);
	
	// Horizontal subtab with underline animation
	bool subtab(const char* label, bool selected);
	
	// Styled child window with title (weave style header)
	void begin_child(const char* name, ImVec2 size, float alpha = 1.0f);
	void end_child();
	
	// Color picker widget (weave style - label left, picker right)
	bool color(const char* name, float col[4], bool alpha = true);
	
	// Collapse/expand button with rotation animation
	bool collapse_button(bool collapsed);
	
	// Rotation helpers for animated elements
	void ImRotateStart();
	ImVec2 ImRotationCenter();
	void ImRotateEnd(float rad, ImVec2 center = ImRotationCenter());
}
