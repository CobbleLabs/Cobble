#include "menu.h"
#include "../widgets/widgets.hpp"
#include "../widgets/theme.hpp"
#include "../hud/arraylist.h"
#include "../hud/notifications.h"
#include "../../module/module_manager.h"
#include "../../module/combat/autoclicker.h"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/autorefill.h"
#include "../../module/combat/velocity.h"
#include "../../module/combat/reach.h"
#include "../../module/utility/nojumpdelay.h"
#include "../../module/visual/nametags.h"
#include "../../module/visual/esp.h"
#include "../../module/visual/testmodule.h"
#include "../../cobble.hpp"
#include "../../sdk/minecraft.h"

#include <string>

// Module lookup by tab and subtab index
// Returns the module that corresponds to the current subtab
static Module* getModuleForSubtab(int tabIdx, int subtabIdx)
{
	// Combat Tab (0)
	if (tabIdx == 0) {
		switch (subtabIdx) {
			case 0: return autoClicker;
			case 1: return aimAssist;
			case 2: return autoRefill;
			case 3: return velocity;
			case 4: return reach;
		}
	}
	// Visuals Tab (1)
	else if (tabIdx == 1) {
		switch (subtabIdx) {
			case 0: return nameTags;
			case 1: return esp;
			case 2: return testModule;
		}
	}
	// Movement Tab (2)
	else if (tabIdx == 2) {
		switch (subtabIdx) {
			case 0: return noJumpDelay;
		}
	}
	// Settings Tab (3) - no module, handled specially
	
	return nullptr;
}

// Settings tab content - special case (not a module)
static void renderSettingsPage()
{
	float halfWidth = (ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2) / 2 - GImGui->Style.ItemSpacing.x / 2;
	float bottomRowHeight = ui::s(100);
	float topRowHeight = ImGui::GetContentRegionAvail().y - bottomRowHeight - ui::s(10);
	
	ImGui::BeginGroup();
	{
		ui::begin_child("Theme", { halfWidth, topRowHeight });
		{
			ui::color("Accent Color", g_SchemeColor);
		}
		ui::end_child();
	}
	ImGui::EndGroup();
	
	ImGui::SameLine();
	
	ImGui::BeginGroup();
	{
		ui::begin_child("HUD", { halfWidth, topRowHeight });
		{
			ui::Checkbox("ArrayList", &ArrayListRenderer::enabled);
			if (ArrayListRenderer::enabled) {
				std::string preview = "None";
				std::vector<std::string> parts;
				if (ArrayListRenderer::showBackground) parts.push_back("BG");
				if (ArrayListRenderer::sortByLength) parts.push_back("Sort");
				
				if (!parts.empty()) {
					preview = "";
					for (size_t i = 0; i < parts.size(); ++i) {
						if (i > 0) preview += ", ";
						preview += parts[i];
					}
				}
				
				ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
				if (ImGui::BeginCombo("Options", preview.c_str())) {
					bool bg = ArrayListRenderer::showBackground;
					if (ImGui::Selectable("Background", &bg, ImGuiSelectableFlags_DontClosePopups)) {
						ArrayListRenderer::showBackground = bg;
					}
					
					bool sort = ArrayListRenderer::sortByLength;
					if (ImGui::Selectable("Sort by Length", &sort, ImGuiSelectableFlags_DontClosePopups)) {
						ArrayListRenderer::sortByLength = sort;
					}
					
					ImGui::EndCombo();
				}
			}
			ui::Checkbox("Notifications", &NotificationManager::enabled);
		}
		ui::end_child();
	}
	ImGui::EndGroup();
	
	// Pin to bottom
	ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - bottomRowHeight);
	
	ImGui::BeginGroup();
	{
		ui::begin_child("Actions", { ImGui::GetWindowWidth() - GImGui->Style.WindowPadding.x * 2, bottomRowHeight });
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.25f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.08f, 0.15f, 1.0f));
			
			ImGui::SetCursorPosY((ImGui::GetWindowHeight() - 32) * 0.5f);
			
			if (ImGui::Button("EJECT CLIENT", ImVec2(ImGui::GetContentRegionAvail().x, 32))) {
				cobble::context::is_running = false;
				Menu::isOpen = false;
			}
			
			ImGui::PopStyleColor(3);
		}
		ui::end_child();
	}
	ImGui::EndGroup();
}

// Render current page - calls module's onRenderMenu() dynamically
static void renderPage()
{
	if (ui::cur_tab >= ui::tabs.size()) return;
	auto& tab = ui::tabs[ui::cur_tab];
	
	// Settings tab (no subtabs)
	if (ui::cur_tab == 3) {
		renderSettingsPage();
		return;
	}
	
	// Get module for current subtab
	if (tab.cur_subtab >= tab.m_subtabs.size()) return;
	
	Module* mod = getModuleForSubtab(ui::cur_tab, tab.cur_subtab);
	if (mod) {
		mod->onRenderMenu();
	}
}

void Menu::ApplyStyle() {
	ui::apply_style();
}

void Menu::Render() {
	// Animate open/close
	float targetAnim = isOpen ? 1.f : 0.f;
	openAnim = ImLerp(openAnim, targetAnim, 15.f * ImGui::GetIO().DeltaTime);
	
	// Don't render if fully closed
	if (openAnim < 0.01f && !isOpen) {
		openAnim = 0.f;
		return;
	}
	
	// Release movement keys while menu is open
	// Handled in WndProc on open, no need to call every frame
	/*
	if (isOpen) {
		mc.releaseMovementKeys();
	}
	*/
	
	ImGuiIO& io = ImGui::GetIO();
	
	ui::updateScale();
	ui::alpha_mod = openAnim;
	
	// Dark overlay behind menu
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0),
		io.DisplaySize,
		IM_COL32(0, 0, 0, (int)(115 * openAnim))
	);
	
	ui::size = ui::s(ui::baseSize);
	
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ui::size);
	
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, openAnim);
	
	ImGui::Begin("Cobble", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
	{
		// ===== LEFT NAV =====
		ImGui::BeginChild("nav", { ui::s(176), ImGui::GetWindowHeight() }, 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
		{
			// Brand area
			ImGui::BeginChild("brand", { ImGui::GetWindowWidth(), ui::s(42) }, 0, ImGuiWindowFlags_NoBackground);
			{
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImGui::GetWindowPos(), 
					{ ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
					ui::GetColor(ImGuiCol_ChildBg), 
					GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersTop
				);
				ImGui::GetWindowDrawList()->AddRectFilled(
					{ ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
					{ ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() }, 
					GetSchemeColor()
				);
				
				const char* brand = "Cobble";
				ImVec2 brandSize = font(3)->CalcTextSizeA(font(3)->FontSize, FLT_MAX, 0.f, brand);
				ImGui::GetWindowDrawList()->AddText(
					font(3), font(3)->FontSize,
					{ ImGui::GetWindowPos().x + ImGui::GetWindowWidth() / 2 - brandSize.x / 2, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() / 2 - brandSize.y / 2 - 2 },
					GetSchemeColor(), brand
				);
			}
			ImGui::EndChild();
			
			// Tabs area
			ImGui::SetCursorPosY(ui::s(50));
			ImGui::BeginChild("tabs", { ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - ui::s(50) }, 0, ImGuiWindowFlags_NoBackground);
			{
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImGui::GetWindowPos(), 
					ImGui::GetWindowPos() + ImGui::GetWindowSize(), 
					ui::GetColor(ImGuiCol_WindowBg, 1.0f), 
					GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom
				);
				
				ImGui::Dummy({ 0, ui::s(6) });
				
				ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 1, 1 });
				ImGui::BeginGroup();
				{
					for (size_t i = 0; i < ui::tabs.size(); ++i) {
						ui::tab((int)i);
					}
				}
				ImGui::EndGroup();
				ImGui::PopStyleVar();
			}
			ImGui::EndChild();
		}
		ImGui::EndChild();
		
		// ===== MAIN CONTENT =====
		ImGui::SetCursorPos({ ui::s(185), 0 });
		ImGui::BeginChild("main", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_NoBackground);
		{
			// Header
			ImGui::BeginChild("main header", { ImGui::GetWindowWidth(), ui::s(42) }, 0, ImGuiWindowFlags_NoBackground);
			{
				ImGui::GetWindowDrawList()->AddRectFilled(
					ImGui::GetWindowPos(), 
					{ ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
					ui::GetColor(ImGuiCol_ChildBg), 
					GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersTop
				);
				ImGui::GetWindowDrawList()->AddRectFilled(
					{ ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ImGui::GetWindowHeight() - 2 }, 
					{ ImGui::GetWindowPos().x + ImGui::GetWindowWidth(), ImGui::GetWindowPos().y + ImGui::GetWindowHeight() }, 
					GetSchemeColor()
				);
				
				ImFont* titleFont = font(3);
				ImFont* subFont = font(2);
				
				float centerY = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * 0.5f;
				float titleHeight = titleFont->FontSize; 
				float baselineY = centerY - titleHeight * 0.5f;
				
				ImGui::GetWindowDrawList()->AddText(
					titleFont, titleFont->FontSize,
					{ ImGui::GetWindowPos().x + ui::s(20), baselineY }, 
					ImGui::GetColorU32(ImGuiCol_TextDisabled, ui::anim), 
					ui::tabs[ui::cur_tab].m_name
				);
				
				if (ui::tabs[ui::cur_tab].m_subtabs.size() > 0) {
					float nameWidth = titleFont->CalcTextSizeA(titleFont->FontSize, FLT_MAX, 0.f, ui::tabs[ui::cur_tab].m_name).x;
					ImGui::RenderArrow(ImGui::GetWindowDrawList(), 
						{ ImGui::GetWindowPos().x + ui::s(27) + nameWidth, centerY - ui::s(5) }, 
						ImGui::GetColorU32(ImGuiCol_Text, ui::anim * ui::content_anim), 
						ImGuiDir_Right, ui::s(12)
					);
					
					const char* subtabName = ui::tabs[ui::cur_tab].m_subtabs[ui::tabs[ui::cur_tab].cur_subtab];
					
					ImGui::GetWindowDrawList()->AddText(
						subFont, subFont->FontSize,
						{ ImGui::GetWindowPos().x + ui::s(40) + nameWidth, baselineY }, 
						GetSchemeColor(ui::anim * ui::content_anim), 
						subtabName
					);
				}
			}
			ImGui::EndChild();
			
			// Content background
			ImGui::GetWindowDrawList()->AddRectFilled(
				{ ImGui::GetWindowPos().x, ImGui::GetWindowPos().y + ui::s(42) }, 
				ImGui::GetWindowPos() + ImGui::GetWindowSize(), 
				ui::GetColor(ImGuiCol_WindowBg), 
				GImGui->Style.WindowRounding, ImDrawFlags_RoundCornersBottom
			);
			
			// Content
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ui::s(10), ui::s(10) });
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { ui::s(15), ui::s(15) });
			
			// Sync alpha_mod for custom widgets BEFORE pushing style
			// This ensures both ImGui native widgets AND custom ui:: widgets fade together
			float contentAlpha = openAnim * ui::anim * ui::content_anim;
			ui::alpha_mod = contentAlpha;
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, contentAlpha);
			
			ImGui::SetCursorPosY(ui::s(42));
			ImGui::BeginChild("content", ImGui::GetContentRegionAvail(), 0, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysUseWindowPadding);
			{
				renderPage();
			}
			ImGui::EndChild();
			ImGui::PopStyleVar(3);
			
			// Restore alpha_mod to menu-level value
			ui::alpha_mod = openAnim;
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleVar();
	
	ui::handle_anims();
}

void Menu::Hotkey(const char* label, Module* module) {
	ImGui::SameLine();
	ImGui::PushID(label);
	
	std::string text = (moduleToBind == module) ? "..." : "[?]";
	
	if (ImGui::SmallButton(text.c_str())) {
		moduleToBind = module;
	}
	
	ImGui::PopID();
}
