#include "arraylist.h"
#include "overlay.h"
#include "menu.h"
#include "../sdk/render_data.h"
#include "../module/module_manager.h"
#include "../../ext/imgui/imgui.h"
#include "weave_ui.hpp"

#include <algorithm>

void ArrayListRenderer::render()
{
	if (!enabled) return;
	if (Menu::isOpen) return;  // Hide when menu is open
	
	// Get enabled modules
	std::vector<Module*> enabledModules;
	for (auto* mod : moduleManager->getModules()) {
		if (mod && mod->enabled) {
			enabledModules.push_back(mod);
		}
	}
	
	if (enabledModules.empty()) return;
	
	// Sort by name length (longest first) for aesthetic
	if (sortByLength) {
		std::sort(enabledModules.begin(), enabledModules.end(), [](Module* a, Module* b) {
			return strlen(a->name) > strlen(b->name);
		});
	}
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	// Use notification font if available
	if (Overlay::notificationFont) {
		ImGui::PushFont(Overlay::notificationFont);
	}
	
	float screenWidth = static_cast<float>(RenderData::screenWidth);
	float padding = 8.0f;
	float itemHeight = 18.0f;
	float margin = 5.0f;
	
	float y = yOffset;
	
	// Get accent color from UI (g_SchemeColor is float[4])
	ImVec4 accentColor(g_SchemeColor[0], g_SchemeColor[1], g_SchemeColor[2], g_SchemeColor[3]);
	
	for (size_t i = 0; i < enabledModules.size(); i++) {
		Module* mod = enabledModules[i];
		std::string name = mod->name;
		
		ImVec2 textSize = ImGui::CalcTextSize(name.c_str());
		float itemWidth = textSize.x + padding * 2;
		
		float x = screenWidth - itemWidth - margin;
		
		// Background
		if (showBackground) {
			ImU32 bgColor = IM_COL32(0, 0, 0, 180);
			draw->AddRectFilled(
				ImVec2(x, y),
				ImVec2(screenWidth - margin, y + itemHeight),
				bgColor
			);
		}
		
		// Accent bar on right
		ImU32 barColor = IM_COL32(
			(int)(accentColor.x * 255),
			(int)(accentColor.y * 255),
			(int)(accentColor.z * 255),
			255
		);
		draw->AddRectFilled(
			ImVec2(screenWidth - margin - 2, y),
			ImVec2(screenWidth - margin, y + itemHeight),
			barColor
		);
		
		// Text
		float textY = y + (itemHeight - textSize.y) / 2.0f;
		draw->AddText(ImVec2(x + padding, textY), IM_COL32(255, 255, 255, 255), name.c_str());
		
		y += itemHeight;
	}
	
	if (Overlay::notificationFont) {
		ImGui::PopFont();
	}
}
