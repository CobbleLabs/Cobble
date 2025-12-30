#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>

#include "../../ext/imgui/imgui.h"
#include "../../ext/imgui/imgui_internal.h"

#ifndef IMGUI_TRICKS_HPP
#define IMGUI_TRICKS_HPP

// Custom color with HEX support
struct ImTrickyColor
{
	float values[4];

	ImTrickyColor() { values[0] = 0.f; values[1] = 0.f; values[2] = 0.f; values[3] = 0.f; }
	ImTrickyColor(float r, float g, float b, float a = 1.f) { values[0] = r; values[1] = g; values[2] = b; values[3] = a; }
	ImTrickyColor(int r, int g, int b, int a = 255) { values[0] = r / 255.f; values[1] = g / 255.f; values[2] = b / 255.f; values[3] = a / 255.f; }
	ImTrickyColor(ImColor col) { values[0] = col.Value.x; values[1] = col.Value.y; values[2] = col.Value.z; values[3] = col.Value.w; }

	inline operator ImU32() const { return ImGui::ColorConvertFloat4ToU32(ImVec4(values[0], values[1], values[2], values[3])); }
	inline operator ImVec4() const { return ImVec4(values[0], values[1], values[2], values[3]); }
	inline operator ImColor() const { return ImColor(values[0], values[1], values[2], values[3]); }
	inline float operator[](std::size_t idx) const { return values[idx]; }
};

namespace ImTricks {

	namespace Animations {
		// Animate int value based on state
		int FastIntLerp(const char* identifier, bool state, int min, int max, int speed);

		// Animate float value based on state
		float FastFloatLerp(std::string identifier, bool state, float min, float max, float speed);

		// Animate ImVec2 value based on state
		ImVec2 FastImVec2Lerp(const char* identifier, bool state, ImVec2 min, ImVec2 max, float speed);

		// Lerp between two colors
		ImColor FastColorLerp(ImColor start, ImColor end, float stage);
		ImTrickyColor FastColorLerp(ImTrickyColor start, ImTrickyColor end, float stage);
	}
}

// Additional ImVec operators
static inline ImVec2 operator+(const ImVec2& lhs, const float rhs) { return ImVec2(lhs.x + rhs, lhs.y + rhs); }
static inline ImVec2 operator-(const ImVec2& lhs, const float rhs) { return ImVec2(lhs.x - rhs, lhs.y - rhs); }

// animation helpers
template <typename T>
inline T& anim_obj(const char* id, int seed, T arg) {
	ImGuiID im_id = ImHashStr(id, 0, seed);
	
	static std::unordered_map<ImGuiID, T> map;
	auto result = map.find(im_id);
	
	if (result == map.end()) {
		map.insert({ im_id, arg });
		result = map.find(im_id);
	}
	
	return result->second;
}

template <typename T>
inline T animate(T v, T min, T max, bool state, float speed = 14.f) {
	return ImLerp(v, state ? max : min, speed * ImGui::GetIO().DeltaTime);
}

inline ImColor col_anim(ImColor inactive, ImColor active, float anim) {
	return ImGui::ColorConvertFloat4ToU32(ImLerp(inactive.Value, active.Value, anim));
}

#endif
