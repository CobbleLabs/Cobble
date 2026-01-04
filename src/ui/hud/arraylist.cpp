#include "arraylist.h"
#include "../core/overlay.h"
#include "../menu/menu.h"
#include "../../sdk/player/render_data.h"
#include "../../module/module_manager.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../widgets/widgets.hpp"

#include <algorithm>

void ArrayListRenderer::render()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	
	std::vector<Module*> enabledModules;
	for (auto* mod : moduleManager->getModules()) {
		if (mod && mod->enabled) {
			enabledModules.push_back(mod);
		}
	}
	
	if (enabledModules.empty()) return;
	
	if (sortByLength) {
		std::sort(enabledModules.begin(), enabledModules.end(), [](Module* a, Module* b) {
			return strlen(a->name) > strlen(b->name);
		});
	}
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	// Use Bold 22px font (index 2) rendered at 20px
	ImFont* arrayFont = font(2);
	
	float screenHeight = static_cast<float>(RenderData::screenHeight);
	float screenWidth = static_cast<float>(RenderData::screenWidth);
	
	// Use global ui::s() for consistent scaling
	float fontSize = 20.0f;
	float padding = ui::s(13.0f);
	float itemHeight = ui::s(25.0f);
	float margin = ui::s(9.0f);
	float barWidth = ui::s(3.5f);
	float rounding = ui::s(4.5f);
	float gap = ui::s(3.0f);
	
	float y = ui::s(yOffset);
	
	// Accent color from theme
	ImU32 accentColor = IM_COL32(
		(int)(g_SchemeColor[0] * 255),
		(int)(g_SchemeColor[1] * 255),
		(int)(g_SchemeColor[2] * 255),
		255
	);
	
	// Background color matching GUI (dark with transparency)
	ImU32 bgColor = IM_COL32(16, 16, 16, 220);
	ImU32 textColor = IM_COL32(255, 255, 255, 255);
	
	for (size_t i = 0; i < enabledModules.size(); i++) {
		Module* mod = enabledModules[i];
		const char* name = mod->name;
		
		// Calculate text size with the actual font
		ImVec2 textSize = arrayFont 
			? arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, name)
			: ImGui::CalcTextSize(name);
		
		float itemWidth = textSize.x + padding * 2 + barWidth;
		float x = screenWidth - itemWidth - margin;
		
		// Background with rounded corners (left side only)
		if (showBackground) {
			draw->AddRectFilled(
				ImVec2(x, y),
				ImVec2(screenWidth - margin, y + itemHeight),
				bgColor,
				rounding,
				ImDrawFlags_RoundCornersLeft
			);
		}
		
		// Accent bar on right edge
		draw->AddRectFilled(
			ImVec2(screenWidth - margin - barWidth, y),
			ImVec2(screenWidth - margin, y + itemHeight),
			accentColor
		);
		
		// Text centered vertically - render with explicit font
		float textY = y + (itemHeight - textSize.y) / 2.0f;
		if (arrayFont) {
			draw->AddText(arrayFont, fontSize, ImVec2(x + padding, textY), textColor, name);
		} else {
			draw->AddText(ImVec2(x + padding, textY), textColor, name);
		}
		
		y += itemHeight + gap;
	}
}


