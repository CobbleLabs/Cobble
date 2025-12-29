#include "custom_widgets.hpp"
#include "imgui_tricks.hpp"
#include <string>
#include <cmath>
#include <algorithm>
#include <map>

using namespace ImGui;

// Pink Minecraft color (#FF55FF)
// Pink Minecraft color (#FF55FF) -> Dynamic
static ImU32 GetSchemeColor(float alpha = 1.0f) {
	return ImGui::ColorConvertFloat4ToU32(ImVec4(
		custom::AccentColor[0], 
		custom::AccentColor[1], 
		custom::AccentColor[2], 
		alpha * custom::AccentColor[3]
	));
}

static int rotation_start_index;

void custom::ImRotateStart() {
	rotation_start_index = ImGui::GetWindowDrawList()->VtxBuffer.Size;
}

ImVec2 custom::ImRotationCenter() {
	ImVec2 l(FLT_MAX, FLT_MAX), u(-FLT_MAX, -FLT_MAX);
	const auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
	for (int i = rotation_start_index; i < buf.Size; i++)
		l = ImMin(l, buf[i].pos), u = ImMax(u, buf[i].pos);
	return ImVec2((l.x + u.x) / 2, (l.y + u.y) / 2);
}

void custom::ImRotateEnd(float rad, ImVec2 center) {
	float s = sin(rad), c = cos(rad);
	center = ImRotate(center, s, c) - center;
	auto& buf = ImGui::GetWindowDrawList()->VtxBuffer;
	for (int i = rotation_start_index; i < buf.Size; i++)
		buf[i].pos = ImRotate(buf[i].pos, s, c) - center;
}

// Icon drawing helper
static void DrawIcon(ImDrawList* dl, ImVec2 p, float s, custom::IconType type, ImU32 col) {
	float half = s / 2;
	ImVec2 center = p + ImVec2(half, half);
	
	switch(type) {
		case custom::Icon_Combat: { // Sword style
			ImVec2 start = center + ImVec2(-4, 4);
			ImVec2 end = center + ImVec2(4, -4);
			dl->AddLine(start, end, col, 2.0f);
			dl->AddLine(start + ImVec2(-2, -2), start + ImVec2(2, 2), col, 2.0f); // guard
			dl->AddLine(start + ImVec2(-1, 1), start + ImVec2(-3, 3), col, 2.0f); // handle
			break;
		}
		case custom::Icon_Visual: { // Eye style
			dl->AddCircle(center, 7.0f, col, 0, 1.5f);
			dl->AddCircleFilled(center, 3.0f, col);
			break;
		}
		case custom::Icon_Settings: { // Gear simplistic
			dl->AddCircle(center, 6.0f, col, 0, 2.0f);
			dl->AddCircleFilled(center, 2.5f, col);
			// Add nice notches if possible or keep simple
			for(int i = 0; i < 8; i++) {
				float ang = i * (3.14159f * 2 / 8);
				ImVec2 off(cos(ang) * 8, sin(ang) * 8);
				dl->AddLine(center + off * 0.7f, center + off, col, 2.0f);
			}
			break;
		}
	}
}

bool custom::tab(IconType icon, const char* label, bool selected) {
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiID id = window->GetID(label);
	
	ImVec2 p = window->DC.CursorPos;
	ImVec2 size({ window->Size.x, 45 }); // Even taller
	ImRect bb(p, p + size);
	
	ItemSize(bb);
	ItemAdd(bb, id);
	
	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held);
	
	float anim = ImTricks::Animations::FastFloatLerp(std::string(label).append("tab.anim"), selected, 0.f, 1.f, 0.05f);
	auto col = ImTricks::Animations::FastColorLerp(GetColorU32(ImGuiCol_TextDisabled), GetColorU32(ImGuiCol_Text), anim);
	auto iconCol = ImTricks::Animations::FastColorLerp(GetColorU32(ImGuiCol_TextDisabled), GetSchemeColor(), anim);
	
	if (pressed)
		contentAnim = 0.f;
	
	// Indicator (Right line)
	static float line_pos = 0.f;
	if (selected) line_pos = ImLerp(line_pos, bb.Min.y - window->Pos.y + 10, 0.1f);
	
	if (selected || anim > 0.01f) {
		window->DrawList->AddRectFilled(
			{ bb.Max.x - 4, window->Pos.y + line_pos },
			{ bb.Max.x, window->Pos.y + line_pos + 25 },
			GetSchemeColor(anim), 2, ImDrawFlags_RoundCornersLeft
		);
	}
	
	// Layout: FIXED GRID (not centered)
	// All tabs share the same baseX for consistent vertical alignment
	// Use bigger font for tab labels (Index 1: 30px Bold)
	ImGui::PushFont(GetIO().Fonts->Fonts[1]);
	
	// SCALE EFFECT for selected tab (15%)
	if (selected) ImGui::SetWindowFontScale(1.15f); 
	
	float iconSize = 24.0f;
	float spacing = 14.0f;
	
	// FIXED left margin - all tabs start here
	float baseX = 25.0f;
	
	// Icon at fixed X
	DrawIcon(window->DrawList, { bb.Min.x + baseX, bb.GetCenter().y - iconSize / 2 }, iconSize, icon, iconCol);
	
	// Text at fixed X + icon + spacing
	PushStyleColor(ImGuiCol_Text, col.Value);
	RenderText({ bb.Min.x + baseX + iconSize + spacing, bb.GetCenter().y - CalcTextSize(label).y / 2 }, label);
	PopStyleColor();
	
	if (selected) ImGui::SetWindowFontScale(1.0f); // Reset scale
	ImGui::PopFont();
	
	return pressed;
}

bool custom::subtab(const char* label, bool selected) {
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiID id = window->GetID(label);
	
	ImVec2 p = window->DC.CursorPos;
	ImVec2 size({ CalcTextSize(label).x + 16, 28 });
	ImRect bb(p, p + size);
	
	ItemSize(bb);
	ItemAdd(bb, id);
	
	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held);
	
	float anim = ImTricks::Animations::FastFloatLerp(std::string(label).append("subtab.anim"), selected, 0.f, 1.f, 0.05f);
	auto col = ImTricks::Animations::FastColorLerp(GetColorU32(ImGuiCol_TextDisabled), GetSchemeColor(), anim);
	
	// SCALE EFFECT (Stronger focus: 1.12f)
	if (selected) ImGui::SetWindowFontScale(1.12f);
	
	// Underline indicator
	if (selected || anim > 0.01f)
		window->DrawList->AddRectFilled(
			{ bb.Min.x, bb.Max.y - 2 }, bb.Max,
			GetSchemeColor(anim), 2, ImDrawFlags_RoundCornersTop
		);
	
	PushStyleColor(ImGuiCol_Text, col.Value);
	RenderText(bb.GetCenter() - CalcTextSize(label) / 2, label);
	PopStyleColor();
	
	if (selected) ImGui::SetWindowFontScale(1.0f);
	
	return pressed;
}

// Custom Slider Helper matching reference design
static bool SliderLogic(const char* label, float* v, float v_min, float v_max, const char* format) {
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiID id = window->GetID(label);

	ImVec2 p = window->DC.CursorPos;
	ImVec2 size({ ImGui::GetContentRegionAvail().x, 45 }); // Increased height (40 -> 45) for better centering
	ImRect bb(p, p + size);
	
	ItemSize(bb);
	ItemAdd(bb, id);
	
	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held);
	
	// Value calculation
	if (held) {
		float mousePos = ImGui::GetIO().MousePos.x;
		// Click bounds check: allow clicking on the bar area primarily? 
		// Actually let's just use the whole rect width for interaction
		float v_fraction = (mousePos - bb.Min.x) / (bb.Max.x - bb.Min.x);
		v_fraction = std::clamp(v_fraction, 0.f, 1.f);
		*v = v_min + (v_max - v_min) * v_fraction;
	}
	
	// Visuals - "Focus on Value"
	// Label: Grey, smaller visual weight
	// Value: White, brighter, right aligned
	// Bar: Thin, subtle
	// Grabber: Minimal, expands on interaction
	
	char val_buf[64];
	sprintf_s(val_buf, format, *v);
	
	// Apply Scale 1.03f for "Precision/Emphasis"
	ImGui::SetWindowFontScale(1.03f);
	
	// Label (Disabled color for subtle look)
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
	RenderText({ bb.Min.x, bb.Min.y }, label);
	ImGui::PopStyleColor();
	
	// Value (White/Bright)
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
	float valWidth = CalcTextSize(val_buf).x;
	RenderText({ bb.Max.x - valWidth, bb.Min.y }, val_buf);
	ImGui::PopStyleColor();
	
	ImGui::SetWindowFontScale(1.0f); // Reset scale
	
	// Slider bar background (Thicker: 7px for "Heavy/Solid" feel)
	// MORE VERTICAL SPACE: lower bar, centered in remaining space
	float barY = bb.Min.y + 32; // Increased separation (28 -> 32) for better centering
	float barHeight = 7.0f;
	
	ImRect slider_bb({ bb.Min.x, barY }, { bb.Max.x, barY + barHeight });

	// Handle clipping fix:
	// The slider track needs to be slightly shorter than the interaction box
	// so the grabber (radius 6px) doesn't get clipped at 0% and 100%.
	float grabRadius = 6.0f;
	float trackPadding = grabRadius + 1.0f;
	
	ImRect track_bb = slider_bb;
	track_bb.Min.x += trackPadding;
	track_bb.Max.x -= trackPadding;
	
	window->DrawList->AddRectFilled(track_bb.Min, track_bb.Max, GetColorU32(ImGuiCol_FrameBg), 4.0f); // Rounderness matches thickness
	
	// Filled part
	float fraction = (*v - v_min) / (v_max - v_min);
	fraction = std::clamp(fraction, 0.f, 1.f);
	
	if (fraction > 0.0f) {
		window->DrawList->AddRectFilled(
			track_bb.Min, 
			{ track_bb.Min.x + track_bb.GetWidth() * fraction, track_bb.Max.y }, 
			GetSchemeColor(), 
			4.0f
		);
	}
	
	// Grabber Logic (Reactive Color + Scale Animation)
	// Center based on TRACK width, not full bb width
	ImVec2 grabCenter = { track_bb.Min.x + track_bb.GetWidth() * fraction, slider_bb.GetCenter().y };
	
	// Animate grabber scale: 1.0 -> 1.15 when dragging
	float targetScale = held ? 1.15f : 1.0f;
	std::string k_grab = "grab_scale_" + std::to_string((unsigned)id);
	float animScale = ImTricks::Animations::FastFloatLerp(k_grab, held, 1.0f, 1.15f, 0.05f);
	float animatedRadius = grabRadius * animScale;
	
	ImU32 grabColor = IM_COL32(255, 255, 255, 255); // Default: White
	
	if (held) {
		// Active: Pink Solid + Glow
		grabColor = GetSchemeColor();
		window->DrawList->AddCircleFilled(grabCenter, animatedRadius + 4.0f, GetSchemeColor(0.3f)); // Active glow
	} else if (hovered) {
		// Hover: White with Pink Glow
		grabColor = IM_COL32(255, 255, 255, 255);
		window->DrawList->AddCircleFilled(grabCenter, animatedRadius + 3.0f, GetSchemeColor(0.4f)); // Hover glow
	}
	
	// Draw grabber with animated size
	window->DrawList->AddCircleFilled(grabCenter, animatedRadius, grabColor);

	return pressed || held;
}

bool custom::slider_float(const char* label, float* v, float v_min, float v_max, const char* format) {
	return SliderLogic(label, v, v_min, v_max, format);
}

bool custom::slider_int(const char* label, int* v, int v_min, int v_max, const char* format) {
	float f_v = (float)*v;
	bool res = SliderLogic(label, &f_v, (float)v_min, (float)v_max, format);
	*v = (int)f_v;
	return res;
}

void custom::begin_child(const char* name, ImVec2 size) {
	// 1. Outer Container: Draws border and background, adapts height to content
	ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
	BeginChild(std::string(name).append("main").c_str(), { size.x, 0 }, childFlags, ImGuiWindowFlags_NoScrollbar);
	
	// Unified horizontal padding for ALL content (Title + Controls)
	float paddingX = 15.0f;
	
	// Title - SAME X as content for perfect alignment
	ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text));
	ImGui::SetCursorPos({ paddingX, 12 });
	ImGui::Text("%s", name);
	ImGui::PopStyleColor();
	
	// 2. Inner Content Container: Handles Padding and Width Constraint
	// This ensures text, checkboxes, and everything respect the right margin.
	float contentWidth = size.x - (paddingX * 2);
	
	ImGui::SetCursorPosY(42); // Top Title Padding
	ImGui::SetCursorPosX(paddingX); // Left Padding (same as title)
	
	// Inner child strictly limits the content width
	ImGuiChildFlags innerFlags = ImGuiChildFlags_AutoResizeY;
	ImGui::BeginChild("##inner_content", { contentWidth, 0 }, innerFlags, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
	
	ImGui::PushItemWidth(contentWidth); // Help sliders fill the space
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 10, 14 });
}

bool custom::checkbox(const char* label, bool* v) {
	ImGuiWindow* window = GetCurrentWindow();
	if (window->SkipItems) return false;

	ImGuiContext& g = *GImGui;
	const ImGuiStyle& style = g.Style;
	ImGuiID id = window->GetID(label);
	ImVec2 label_size = CalcTextSize(label, NULL, true);

	float square_sz = GetFrameHeight();
	ImVec2 pos = window->DC.CursorPos;
	ImRect total_bb(pos, pos + ImVec2(square_sz + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), label_size.y + style.FramePadding.y * 2.0f));
	ItemSize(total_bb, style.FramePadding.y);
	if (!ItemAdd(total_bb, id)) {
		return false;
	}

	bool hovered, held;
	bool pressed = ButtonBehavior(total_bb, id, &hovered, &held);
	if (pressed) {
		*v = !(*v);
		MarkItemEdited(id);
	}

	// ANIMATIONS
	// 1. Checkmark state (0 -> 1)
	std::string k_state = "chk_state_" + std::to_string((unsigned)id);
	float checkAnim = ImTricks::Animations::FastFloatLerp(k_state, *v, 0.f, 1.f, 0.06f);
	
	// 2. Hover state
	std::string k_hover = "chk_hover_" + std::to_string((unsigned)id);
	float hoverAnim = ImTricks::Animations::FastFloatLerp(k_hover, hovered, 0.f, 1.f, 0.1f);

	// RENDER
	ImRect check_bb(pos, pos + ImVec2(square_sz, square_sz));
	
	// Draw Background (Dark Grey)
	ImU32 bg_col = GetColorU32(ImGuiCol_FrameBg);
	// Optional: lighter on hover
	if (hovered) bg_col = GetColorU32(ImGuiCol_FrameBgHovered);
	
	window->DrawList->AddRectFilled(check_bb.Min, check_bb.Max, bg_col, style.FrameRounding);
	
	// Draw Border (Pink when hovered?) - User likes clean, maybe skip or subtle
	if (hoverAnim > 0.01f) {
		window->DrawList->AddRect(check_bb.Min, check_bb.Max, GetSchemeColor(hoverAnim), style.FrameRounding);
	}

	// Draw Checkmark
	if (checkAnim > 0.01f) {
		ImVec2 center = check_bb.GetCenter();
		
		// Manual EaseOutCubic: 1 - (1 - t)^3
		float t = checkAnim;
		float eased = 1.0f - powf(1.0f - t, 3.0f);
		float scale = 0.5f + 0.5f * eased;
		
		// Tick shape points
		ImVec2 p1 = { center.x - 5.f * scale, center.y + 1.f * scale };
		ImVec2 p2 = { center.x - 2.f * scale, center.y + 4.f * scale };
		ImVec2 p3 = { center.x + 5.f * scale, center.y - 4.f * scale };
		
		ImU32 checkCol = GetSchemeColor(checkAnim); // Alpha fade
		
		window->DrawList->AddLine(p1, p2, checkCol, 2.5f);
		window->DrawList->AddLine(p2, p3, checkCol, 2.5f);
	}

	// Draw Text
	if (label_size.x > 0.0f) {
		ImGui::SetWindowFontScale(1.03f); // Subtle emphasis
		
		ImVec2 label_pos = ImVec2(check_bb.Max.x + style.ItemInnerSpacing.x, check_bb.Min.y + style.FramePadding.y);
		ImU32 text_col = GetColorU32(ImGuiCol_Text);
		
		RenderText(label_pos, label);
		
		ImGui::SetWindowFontScale(1.0f);
	}

	return pressed;
}

void custom::end_child() {
	ImGui::PopStyleVar();
	ImGui::PopItemWidth();
	ImGui::EndChild(); // End Inner Content
	
	// Bottom padding for the Outer Container
	ImGui::Dummy({ 0, 18 });
	
	ImGui::EndChild(); // End Outer Container
}

bool custom::collapse_button(bool collapsed) {
	BeginChild("##collapse_container", { 24, 24 });
	
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiID id = window->GetID("collapse_button");
	
	ImVec2 p = window->DC.CursorPos;
	ImVec2 size(GetWindowSize());
	ImRect bb(p, p + size);
	
	ItemSize(bb);
	ItemAdd(bb, id);
	
	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held);
	
	// Circle background
	window->DrawList->AddCircleFilled(bb.GetCenter(), bb.GetWidth() / 2, GetColorU32(ImGuiCol_FrameBg), 12);
	if (hovered)
		window->DrawList->AddCircle(bb.GetCenter(), bb.GetWidth() / 2, GetSchemeColor(), 12, 1.5f);
	
	// Animated rotation
	float anim = ImTricks::Animations::FastFloatLerp("collapse_button.anim", collapsed, 0.f, 1.f, 0.06f);
	
	ImRotateStart();
	// Draw arrow geometry manually for crisper look
	ImVec2 c = bb.GetCenter();
	ImU32 col = GetColorU32(ImGuiCol_Text);
	window->DrawList->AddTriangleFilled(c + ImVec2(-4, -2), c + ImVec2(4, -2), c + ImVec2(0, 4), col);
	
	ImRotateEnd(3.14159f * (-0.5f + anim)); // Rotates -90 to +90 deg roughly
	
	EndChild();
	
	return pressed;
}
