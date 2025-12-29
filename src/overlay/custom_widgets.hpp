#pragma once
/*
 * Custom ImGui Widgets for Cobble Client
 * Animated tabs, subtabs, and styled children
 */

#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_internal.h"
#include "imgui_tricks.hpp"

// Menu state
inline int currentTab = 0;
inline int currentSubtab = 0;
inline float contentAnim = 0.f;

// Custom color index for scheme (pink)
#define ImGuiCol_Scheme (ImGuiCol_COUNT)

namespace custom {
	// Global Accent Color (Default: Pink)
	inline float AccentColor[4] = { 1.0f, 0.33f, 1.0f, 1.0f }; // Pink #FF55FF approx

	enum IconType {
		Icon_Combat,
		Icon_Visual,
		Icon_Settings
	};

	// Sidebar tab with vector icon and label
	bool tab(IconType icon, const char* label, bool selected);
	
	// Custom modern slider
	bool slider_float(const char* label, float* v, float v_min, float v_max, const char* format = "%.1f");
	bool slider_int(const char* label, int* v, int v_min, int v_max, const char* format = "%d");
	
	// Checkbox with scaled text emphasis
	bool checkbox(const char* label, bool* v);
	
	// Horizontal subtab with underline animation
	bool subtab(const char* label, bool selected);
	
	// Styled child window with title
	void begin_child(const char* name, ImVec2 size);
	void end_child();
	
	// Collapse/expand button with rotation animation
	bool collapse_button(bool collapsed);
	
	// Rotation helpers for animated elements
	void ImRotateStart();
	ImVec2 ImRotationCenter();
	void ImRotateEnd(float rad, ImVec2 center = ImRotationCenter());
}
