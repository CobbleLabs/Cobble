#include "custom_widgets.hpp"
#include "imgui_tricks.hpp"
#include <string>
#include <cmath>
#include <algorithm>
#include <map>

using namespace ImGui;

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

bool custom::tab(const char* icon, const char* label, bool selected) {
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiID id = window->GetID(label);
	
	ImVec2 p = window->DC.CursorPos;
	ImVec2 size({ window->Size.x, 38 });
	ImRect bb(p, p + size);
	
	ItemSize(bb);
	ItemAdd(bb, id);
	
	bool hovered, held;
	bool pressed = ButtonBehavior(bb, id, &hovered, &held);
	
	if (pressed && !selected) {
		contentAnim = 0.f;
	}
	
	struct TabAnimState {
		float cur;
		float hover;
		ImColor col;
	};
	auto& obj = anim_obj(label, 0, TabAnimState{ 0.f, 0.f, ImColor(0, 0, 0, 0) });
	
	obj.cur = animate(obj.cur, 0.f, 1.f, selected);
	obj.hover = animate(obj.hover, 0.f, 1.f, hovered);
	obj.col = col_anim(
		col_anim(GetColorU32(ImGuiCol_TextDisabled), GetColorU32(ImGuiCol_Text, 0.75f), obj.hover),
		GetColorU32(ImGuiCol_Text),
		obj.cur
	);
	
	// Icon color animates to scheme when selected
	ImColor iconCol = col_anim(GetColorU32(ImGuiCol_TextDisabled), GetSchemeColor(), obj.cur);
	
	// Right indicator (accent line)
	if (selected || obj.cur > 0.01f) {
		window->DrawList->AddRectFilled(
			{ bb.Max.x - 3, bb.Min.y + 8 },
			{ bb.Max.x, bb.Max.y - 8 },
			GetSchemeColor(obj.cur), 2, ImDrawFlags_RoundCornersLeft
		);
	}
	
	// FontAwesome icon (rendered as text)
	ImVec2 iconSize = CalcTextSize(icon, 0, 1);
	window->DrawList->AddText(
		{ bb.Min.x + 15, bb.GetCenter().y - iconSize.y / 2 },
		iconCol, icon, FindRenderedTextEnd(icon)
	);
	
	// Label - use bold font for selected (font index 1 = 30px bold)
	ImFont* labelFont = selected ? GetIO().Fonts->Fonts[1] : GetIO().Fonts->Fonts[0];
	ImVec2 nameSize = labelFont->CalcTextSizeA(labelFont->FontSize, FLT_MAX, 0.f, label, FindRenderedTextEnd(label));
	window->DrawList->AddText(
		labelFont, labelFont->FontSize,
		{ bb.Min.x + 40, bb.GetCenter().y - nameSize.y / 2 },
		obj.col, label, FindRenderedTextEnd(label)
	);
	
	// Bottom separator line
	window->DrawList->AddLine(
		{ bb.Min.x + 40, window->DC.CursorPos.y },
		{ bb.Max.x, window->DC.CursorPos.y },
		GetColorU32(ImGuiCol_Border, 0.5f)
	);
	
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

void custom::begin_child(const char* name, ImVec2 size, float alpha) {
	// Calculate size if auto
	ImVec2 calcSize = CalcItemSize(size, 
		(GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2, 
		0);
	
	// Outer container (decoration)
	BeginChild(std::string(name).append("decor").c_str(), calcSize, 0, 
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground);
	
	// Background fill with alpha
	GetWindowDrawList()->AddRectFilled(
		GetWindowPos(), 
		GetWindowPos() + GetWindowSize(), 
		GetColorU32(ImGuiCol_ChildBg, alpha), 
		GImGui->Style.ChildRounding
	);
	
	// Header with title
	if (CalcTextSize(name, 0, 1).x > 0) {
		// Header background
		GetWindowDrawList()->AddRectFilled(
			GetWindowPos(), 
			{ GetWindowPos().x + GetWindowWidth(), GetWindowPos().y + 33 },
			GetColorU32(ImGuiCol_Header, alpha),
			GImGui->Style.ChildRounding, ImDrawFlags_RoundCornersTop
		);
		// Separator line below header
		GetWindowDrawList()->AddLine(
			{ GetWindowPos().x, GetWindowPos().y + 33 },
			{ GetWindowPos().x + GetWindowWidth(), GetWindowPos().y + 33 },
			GetColorU32(ImGuiCol_BorderShadow)
		);
		// Title text (font index 4 = medium 17px)
		ImFont* titleFont = GetIO().Fonts->Fonts.Size > 4 ? GetIO().Fonts->Fonts[4] : GetIO().Fonts->Fonts[0];
		ImVec2 titleSize = titleFont->CalcTextSizeA(titleFont->FontSize, FLT_MAX, 0.f, name, FindRenderedTextEnd(name));
		GetWindowDrawList()->AddText(
			titleFont, titleFont->FontSize,
			{ GetWindowPos().x + 13, GetWindowPos().y + 33 / 2 - titleSize.y / 2 },
			GetColorU32(ImGuiCol_Text, alpha), name, FindRenderedTextEnd(name)
		);
		SetCursorPosY(34);
	}
	
	// Inner content padding
	PushStyleVar(ImGuiStyleVar_WindowPadding, { 13, 15 });
	PushStyleVar(ImGuiStyleVar_ItemSpacing, { 8, 14 });
	BeginChild(name, { -1, size.y == 0 ? size.y : -1 }, 0, 
		ImGuiWindowFlags_AlwaysUseWindowPadding | ImGuiWindowFlags_NoBackground);
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
	EndChild(); // End inner content
	PopStyleVar(2); // WindowPadding + ItemSpacing
	EndChild(); // End outer decoration
}

bool custom::color(const char* name, float col[4], bool alpha) {
	ImGuiWindow* window = GetCurrentWindow();
	ImGuiID id = window->GetID(name);
	
	ImDrawList* draw = window->DrawList;
	ImVec2 pos = window->DC.CursorPos;
	
	ImVec2 size{ 16, 16 };
	ImVec2 labelSize = CalcTextSize(name, 0, 1);
	
	ImRect bb(pos, { pos.x + CalcItemWidth() - size.x, pos.y + size.y });
	ItemAdd(bb, id);
	ItemSize(bb);
	
	// Label on left
	draw->AddText(
		{ bb.Min.x, bb.GetCenter().y - labelSize.y / 2 },
		GetColorU32(ImGuiCol_Text), name, FindRenderedTextEnd(name)
	);
	
	bool valueChanged = false;
	if (col) {
		ImVec2 savedPos = window->DC.CursorPos;
		window->DC.CursorPos = ImVec2{ bb.Max.x, bb.Min.y };
		
		ImGuiColorEditFlags flags = alpha ? COBBLE_COLOR_ALPHA : COBBLE_COLOR_NO_ALPHA;
		valueChanged = ColorEdit4(std::string(name).append("##col").c_str(), col, flags);
		
		window->DC.CursorPos = savedPos;
	}
	
	return valueChanged;
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
