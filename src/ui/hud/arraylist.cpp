#include "arraylist.h"
#include "../core/overlay.h"
#include "../menu/menu.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../sdk/player/render_data.h"
#include "../../module/module_manager.h"
#include "../../module/combat/autorefill.h"
#include "../../util/team/friends_manager.h"
#include "../../../infra/ext/imgui/imgui.h"
#include "../widgets/widgets.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

// RGB struct for HSB conversion
struct RGB { float r, g, b; };

// HSB to RGB conversion
static RGB FromHSB(float h, float s, float b) {
	float c = b * s;
	float x = c * (1.0f - std::fabs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
	float m = b - c;
	
	float r, g, bl;
	if (h < 1.0f/6.0f) { r = c; g = x; bl = 0; }
	else if (h < 2.0f/6.0f) { r = x; g = c; bl = 0; }
	else if (h < 3.0f/6.0f) { r = 0; g = c; bl = x; }
	else if (h < 4.0f/6.0f) { r = 0; g = x; bl = c; }
	else if (h < 5.0f/6.0f) { r = x; g = 0; bl = c; }
	else { r = c; g = 0; bl = x; }
	
	return RGB{ (r + m) * 255.0f, (g + m) * 255.0f, (bl + m) * 255.0f };
}

// Rainbow color generator - smooth flowing animation
static ImU32 GetRainbowColor(int moduleIndex, float saturation) {
	// Get current time in milliseconds
	auto now = std::chrono::system_clock::now().time_since_epoch();
	long long millis = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
	
	// Use modulo to keep the number small and avoid precision issues
	// 8000ms = 8 second full rainbow cycle
	float cycleMs = 8000.0f;
	float timeInCycle = std::fmod((float)(millis % 8000), cycleMs);
	
	// Each module offset for flowing effect (100ms offset per module)
	float moduleOffset = (moduleIndex * 100.0f);
	
	// Calculate hue (0-1 range)
	float hue = std::fmod((timeInCycle + moduleOffset) / cycleMs, 1.0f);
	
	// HSB to RGB
	RGB rgb = FromHSB(hue, saturation, 0.9f);
	
	return IM_COL32((int)rgb.r, (int)rgb.g, (int)rgb.b, 255);
}

void ArrayListRenderer::render()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (!player.isValid()) return;  // Hide in main menu
	
	// Build list of display entries (name + info)
	struct DisplayEntry {
		std::string name;
		std::string info;  // Empty if no info
	};
	std::vector<DisplayEntry> entries;
	
	// Add enabled modules with their display info
	for (auto* mod : moduleManager->getModules()) {
		if (mod && mod->enabled) {
			DisplayEntry entry;
			entry.name = mod->name;
			if (showInfo) {
				entry.info = mod->getDisplayInfo();
			}
			entries.push_back(entry);
		}
	}
	
	// Special case: Show AutoRefill when onInventoryOpen is enabled (passive mode)
	if (autoRefill && !autoRefill->enabled && autoRefill->settings.onInventoryOpen) {
		DisplayEntry entry;
		entry.name = autoRefill->name;
		if (showInfo) {
			entry.info = autoRefill->getDisplayInfo();
		}
		entries.push_back(entry);
	}
	
	// Add "Teams" entry if Auto Detect is enabled
	if (friendsManager && friendsManager->teams.enabled) {
		entries.push_back({ "Teams", "" });
	}
	
	if (entries.empty()) return;
	
	// Use Bold 22px font
	ImFont* arrayFont = font(2);
	if (!arrayFont) return;
	
	// Base font size with scale applied (24px base)
	float fontSize = 24.0f * scale;
	
	// Sort by total text width using the actual font
	if (sortByLength) {
		std::sort(entries.begin(), entries.end(), [arrayFont, fontSize](const DisplayEntry& a, const DisplayEntry& b) {
			std::string fullA = a.name + (a.info.empty() ? "" : " " + a.info);
			std::string fullB = b.name + (b.info.empty() ? "" : " " + b.info);
			ImVec2 sizeA = arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fullA.c_str());
			ImVec2 sizeB = arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, fullB.c_str());
			return sizeA.x > sizeB.x;
		});
	}
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	float screenWidth = static_cast<float>(RenderData::screenWidth);
	float screenHeight = static_cast<float>(RenderData::screenHeight);
	
	// Styling values scaled
	float padding = 10.0f * scale;
	float barWidth = 3.0f * scale;
	
	// Determine anchor position
	bool anchorLeft = (anchor == 1 || anchor == 3);   // TopLeft or BottomLeft
	bool anchorBottom = (anchor == 2 || anchor == 3); // BottomRight or BottomLeft
	
	// Calculate total height for bottom anchoring
	float totalHeight = 0.0f;
	if (anchorBottom) {
		for (const auto& entry : entries) {
			ImVec2 nameSize = arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, entry.name.c_str());
			totalHeight += nameSize.y;
		}
	}
	
	// Starting Y position
	float y = anchorBottom ? (screenHeight - yOffset - totalHeight) : yOffset;
	int idx = 0;
	
	// Colors
	ImU32 bgColor = showBackground ? IM_COL32(19, 19, 19, 150) : IM_COL32(0, 0, 0, 0);
	ImU32 infoColor = IM_COL32(180, 180, 180, 255);  // Light gray for info
	
	for (const auto& entry : entries) {
		// Calculate sizes
		ImVec2 nameSize = arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, entry.name.c_str());
		std::string infoText = entry.info.empty() ? "" : " " + entry.info;
		ImVec2 infoSize = entry.info.empty() ? ImVec2(0, 0) : arrayFont->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, infoText.c_str());
		float totalWidth = nameSize.x + infoSize.x;
		float lineHeight = nameSize.y;
		
		// Get color (rainbow or accent)
		ImU32 moduleColor;
		if (rainbow) {
			moduleColor = GetRainbowColor(idx, rainbowSaturation);
		} else {
			// Use custom color if set, otherwise use accent color
			if (color[0] < 0) {
				// Use accent color from theme
				moduleColor = IM_COL32(
					(int)(g_SchemeColor[0] * 255),
					(int)(g_SchemeColor[1] * 255),
					(int)(g_SchemeColor[2] * 255),
					255
				);
			} else {
				moduleColor = IM_COL32(
					(int)(color[0] * 255),
					(int)(color[1] * 255),
					(int)(color[2] * 255),
					255
				);
			}
		}
		
		// Calculate X position based on anchor
		float edgePadding = colorBar ? barWidth + 5.0f * scale : 8.0f * scale;
		float itemWidth = totalWidth + padding + edgePadding;
		float x, rectX1, rectX2, textX, barX1, barX2;
		
		if (anchorLeft) {
			// Left side
			x = 0;
			rectX1 = x;
			rectX2 = x + itemWidth;
			textX = x + (colorBar ? barWidth + padding * 0.5f : padding * 0.5f);
			barX1 = x;
			barX2 = x + barWidth;
		} else {
			// Right side (default)
			x = screenWidth - itemWidth;
			rectX1 = x;
			rectX2 = screenWidth;
			textX = x + padding * 0.5f;
			barX1 = screenWidth - barWidth;
			barX2 = screenWidth;
		}
		
		// Background with rounded corners
		draw->AddRectFilled(
			ImVec2(rectX1, y),
			ImVec2(rectX2, y + lineHeight),
			bgColor,
			4.0f * scale  // Rounded corners
		);
		
		// Draw name with module color
		draw->AddText(arrayFont, fontSize, ImVec2(textX, y), moduleColor, entry.name.c_str());
		
		// Draw info with gray color
		if (!entry.info.empty()) {
			draw->AddText(arrayFont, fontSize, ImVec2(textX + nameSize.x, y), infoColor, infoText.c_str());
		}
		
		// Color bar on edge
		if (colorBar) {
			draw->AddRectFilled(
				ImVec2(barX1, y),
				ImVec2(barX2, y + lineHeight),
				moduleColor
			);
		}
		
		y += lineHeight;
		idx++;
	}
}
