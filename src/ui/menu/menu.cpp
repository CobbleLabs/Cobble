#include "menu.h"
#include "../widgets/widgets.hpp"
#include "../widgets/theme.hpp"
#include "../widgets/icons.hpp"
#include "../hud/arraylist.h"
#include "../hud/notifications.h"
#include "../../config/config.hpp"
#include "../../util/team/friends_manager.h"
#include "../../util/logger.hpp"
#include "../../util/network/http.hpp"
#include "../../module/module_manager.h"
#include "../../module/combat/autoclicker.h"
#include "../../module/combat/aimassist.h"
#include "../../module/combat/autorefill.h"
#include "../../module/combat/velocity.h"
#include "../../module/combat/reach.h"
#include "../../module/combat/blink.h"
#include "../../module/utility/nojumpdelay.h"
#include "../../module/utility/noitemrelease.h"
#include "../../module/utility/bridgeassist.h"
#include "../../module/utility/sprint.h"
#include "../../module/utility/fastplace.h"
#include "../../module/utility/timer.h"
#include "../../module/visual/nametags.h"
#include "../../module/visual/esp.h"
#include "../../module/visual/tracers.h"
#include "../../module/visual/blockesp.h"
#include "../../cobble.hpp"
#include "../../sdk/core/minecraft.h"

#include <string>
#include <algorithm>

// Module lookup by tab and subtab index
// Returns the module that corresponds to the current subtab
Module* getModuleForSubtab(int tabIdx, int subtabIdx)
{
	// Combat Tab (0)
	if (tabIdx == 0) {
		switch (subtabIdx) {
			case 0: return autoClicker;
			case 1: return aimAssist;
			case 2: return autoRefill;
			case 3: return velocity;
			case 4: return reach;
			case 5: return blink;
		}
	}
	// Visuals Tab (1)
	else if (tabIdx == 1) {
		switch (subtabIdx) {
			case 0: return nameTags;
			case 1: return esp;
			case 2: return tracers;
			case 3: return blockESP;
			case 4: return nullptr; // ArrayList - special handling
			case 5: return nullptr; // Notifications - special handling
		}
	}
	// Utility Tab (2)
	else if (tabIdx == 2) {
		switch (subtabIdx) {
			case 0: return timerModule;
			case 1: return fastPlace;
			case 2: return sprint;
			case 3: return noJumpDelay;
			case 4: return noItemRelease;
			case 5: return bridgeAssist;
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
	
	// General column (accent color + cloud sync)
	ImGui::BeginGroup();
	{
		ui::begin_child("General", { halfWidth, topRowHeight });
		{
			ui::Binder("Menu Key", &Menu::menuKey);
			
			ImGui::Spacing();
			
			ui::color("Accent Color", g_SchemeColor);
			
			ImGui::Spacing();
			ImGui::Separator();
			ImGui::Spacing();
			
			// Cloud Sync toggle
			bool syncEnabled = ConfigManager::get().isSyncEnabled();
			if (ui::Checkbox("Cloud Sync", &syncEnabled)) {
				ConfigManager::get().setSyncEnabled(syncEnabled);
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("Sync config across devices");
			ImGui::PopStyleColor();
		}
		ui::end_child();
	}
	ImGui::EndGroup();
	
	ImGui::SameLine();
	
	// Friends column
	ImGui::BeginGroup();
	{
		ui::begin_child("Friends", { halfWidth, topRowHeight });
		{
			if (friendsManager) {
				// Add friend input
				static char friendInput[64] = "";
				static std::string lastAddStatus = "";
				ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ui::s(60));
				bool enterPressed = ImGui::InputText("##addfriend", friendInput, sizeof(friendInput), ImGuiInputTextFlags_EnterReturnsTrue);
				ImGui::SameLine();
				if ((ImGui::Button("Add", ImVec2(ui::s(50), 0)) || enterPressed) && friendInput[0] != '\0') {
					std::string inputName = friendInput;
					bool added = false;
					
					// Step 1: Try Mojang API to get real UUID from username
					// This is the CORRECT way - user types real Mojang name
					auto mojangProfile = http::lookupMojangUUID(inputName);
					if (mojangProfile.found) {
						friendsManager->addFriendByUUID(mojangProfile.uuid, mojangProfile.name);
						lastAddStatus = "Added '" + mojangProfile.name + "' (Mojang)";
						log_debug("[Friends] Mojang API: %s -> %s", inputName.c_str(), mojangProfile.uuid.c_str());
						added = true;
					}
					
					// Step 2: If Mojang fails, try to find in current world
					if (!added) {
						Minecraft mc;
						PlayerInfo found = mc.findPlayerByName(inputName);
						if (!found.uuid.empty()) {
							friendsManager->addFriendByUUID(found.uuid, inputName);
							lastAddStatus = "Added '" + inputName + "' (World)";
							log_debug("[Friends] World scan: %s -> %s", inputName.c_str(), found.uuid.c_str());
							added = true;
						}
					}
					
					// Step 3: Fallback to name-only (not recommended)
					if (!added) {
						friendsManager->addFriend(inputName);
						lastAddStatus = "'" + inputName + "' not found (name only)";
						log_debug("[Friends] Fallback to name: %s", inputName.c_str());
					}
					
					friendInput[0] = '\0';
				}
				
				// Friends list - show displayName as user typed it
				auto friends = friendsManager->getFriends();
				if (!friends.empty()) {
					ImGui::Separator();
					std::string toRemove = "";
					for (const auto& f : friends) {
						ImGui::Text("%s", f.displayName.c_str());
						ImGui::SameLine(ImGui::GetContentRegionAvail().x - ui::s(20));
						ImGui::PushID(f.uuid.c_str());
						if (ImGui::SmallButton("X")) {
							toRemove = f.uuid;
						}
						ImGui::PopID();
					}
					if (!toRemove.empty()) {
						friendsManager->removeFriendByUUID(toRemove);
					}
				} else {
					ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
					ImGui::TextWrapped("No friends added");
					ImGui::PopStyleColor();
				}
				
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				
				// Auto Detect - enables all team detection (Apollo, Scoreboard, etc.)
				ui::Checkbox("Auto Detect", &friendsManager->teams.enabled);
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::PopStyleColor();
				
				ImGui::Spacing();
				ImGui::Separator();
				ImGui::Spacing();
				
				// Quick-add friend feature
				ui::CheckboxBind("Quick Add", &friendsManager->quickAddEnabled, &friendsManager->quickAddKey);
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::TextWrapped("Press key while looking at player");
				ImGui::PopStyleColor();
				
				ImGui::Spacing();
				
				ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
				ImGui::PopStyleColor();
			}
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
			
			ImGui::SetCursorPosY((ImGui::GetWindowHeight() - ui::s(32)) * 0.5f);
			
			if (ImGui::Button("EJECT CLIENT", ImVec2(ImGui::GetContentRegionAvail().x, ui::s(32)))) {
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
        // Locked Module UI
        if (!mod->isAccessible()) {
            ImGui::Spacing();
            ImGui::Spacing();
            
            // Center content
            float width = ImGui::GetContentRegionAvail().x;
            
            // Lock Icon
            ImVec2 iconSize = ImGui::CalcTextSize(ICON_FA_LOCK);
            ImGui::SetCursorPosX((width - iconSize.x) / 2);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
            ImGui::Text(ICON_FA_LOCK);
            ImGui::PopStyleColor();
            
            // Text
            const char* text = "This feature is locked.";
            ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::SetCursorPosX((width - textSize.x) / 2);
            ImGui::Text(text);
            
            ImGui::Spacing();
            
            const char* subText = "Enter unlock code:";
            ImVec2 subSize = ImGui::CalcTextSize(subText);
            ImGui::SetCursorPosX((width - subSize.x) / 2);
            ImGui::Text(subText);
            
            // Input
            static char buf[64] = "";
            static bool incorrect = false;
            
            ImGui::SetCursorPosX((width - ui::s(200)) / 2);
            ImGui::SetNextItemWidth(ui::s(200));
            
            // Auto focus if just selected? Maybe too intrusive.
            
            bool enterPressed = ImGui::InputText("##pass", buf, sizeof(buf), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
            
            if (incorrect) {
                const char* err = "Incorrect code!";
                ImVec2 errSize = ImGui::CalcTextSize(err);
                ImGui::SetCursorPosX((width - errSize.x) / 2);
                ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, err);
            }
            
            ImGui::Spacing();
            
            ImGui::SetCursorPosX((width - ui::s(100)) / 2);
            if (ImGui::Button("Unlock", {ui::s(100), ui::s(30)}) || enterPressed) {
                if (strcmp(buf, Module::unlockCode) == 0) {
                    Module::experimentalUnlocked = true;
                    incorrect = false;
                    memset(buf, 0, sizeof(buf));
                } else {
                    incorrect = true;
                    memset(buf, 0, sizeof(buf));
                }
            }
            
        } else {
            // Normal Module UI
            mod->onRenderMenu();
        }
	}
	// Special pages (ArrayList, Notifications) - no Module, render inline
	else if (ui::cur_tab == 1) {
		// Visuals tab
		if (tab.cur_subtab == 4) {
			// ArrayList settings
			ui::two_column(
				"Settings", []() {
					ui::Checkbox("Enabled", &ArrayListRenderer::enabled);
					ui::Checkbox("Rainbow", &ArrayListRenderer::rainbow);
					ui::Checkbox("Color Bar", &ArrayListRenderer::colorBar);
					ui::Checkbox("Background", &ArrayListRenderer::showBackground);
				},
				"Options", []() {
					ui::Checkbox("Sort by Length", &ArrayListRenderer::sortByLength);
					ui::Checkbox("Show Info", &ArrayListRenderer::showInfo);
					ui::SliderFloat("Size", &ArrayListRenderer::scale, 0.5f, 2.0f, "%.1fx");
					
					// Position/Anchor selector
					const char* anchorItems[] = { "Top Right", "Top Left", "Bottom Right", "Bottom Left" };
					ui::Combo("Position", &ArrayListRenderer::anchor, anchorItems, 4);
					
					if (ArrayListRenderer::rainbow) {
						bool highSat = ArrayListRenderer::rainbowSaturation > 0.6f;
						if (ui::Checkbox("High Saturation", &highSat)) {
							ArrayListRenderer::rainbowSaturation = highSat ? 0.8f : 0.4f;
						}
					}
					if (!ArrayListRenderer::rainbow) {
						ui::color("Color", ArrayListRenderer::color);
					}
				}
			);
		}
		else if (tab.cur_subtab == 5) {
			// Notifications settings
			ui::section("Settings", []() {
				ui::Checkbox("Enabled", &NotificationManager::enabled);
			});
		}
	}
}

void Menu::ApplyStyle() {
	ui::apply_style();
}

void Menu::Render() {
	// Animate open/close
	float targetAnim = isOpen ? 1.f : 0.f;
	openAnim = ImLerp(openAnim, targetAnim, 15.f * ImGui::GetIO().DeltaTime);
	
	if (openAnim < 0.01f && !isOpen) return;
	
	// Apply style properly (set alpha etc)
	ApplyStyle();
	
    ImGuiIO& io = ImGui::GetIO();
    ui::updateScale();
    
	// Dark overlay behind menu
	ImGui::GetBackgroundDrawList()->AddRectFilled(
		ImVec2(0, 0),
		io.DisplaySize,
		IM_COL32(0, 0, 0, (int)(115 * openAnim))
	);
	
	// Menu size with resolution scaling + open animation
    ui::size = ui::s(ui::baseSize) * openAnim;
	
    // Center window
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
				
				// Main tabs (Combat, Visuals, Movement) - all except Settings
				ImGui::BeginGroup();
				{
					for (size_t i = 0; i < ui::tabs.size() - 1; ++i) {
						ui::tab((int)i);
					}
				}
				ImGui::EndGroup();
				
				// Settings tab pinned to bottom
				float settingsHeight = ui::s(38);
				float availableSpace = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - settingsHeight - ui::s(10);
				
				// Push to bottom
				if (availableSpace > 0) {
					ImGui::Dummy({ 0, availableSpace });
				}
				
				// Subtle separator above Settings
				ImGui::GetWindowDrawList()->AddLine(
					{ ImGui::GetWindowPos().x + ui::s(15), ImGui::GetWindowPos().y + ImGui::GetCursorPosY() },
					{ ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - ui::s(15), ImGui::GetWindowPos().y + ImGui::GetCursorPosY() },
					ui::GetColor(ImGuiCol_Border, 0.5f)
				);
				
				ImGui::Dummy({ 0, ui::s(4) });
				
				// Render Settings tab
				ui::tab((int)(ui::tabs.size() - 1));
				
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
				
				// Header Title (Tab Name)
				const char* name = ui::tabs[ui::cur_tab].m_name;
				ImFont* titleFont = font(3);
				ImFont* subFont = font(2);
				
				float centerY = ImGui::GetWindowPos().y + ImGui::GetWindowHeight() * 0.5f;
				float titleHeight = titleFont->FontSize;
				float baselineY = centerY - titleHeight * 0.5f;
				
				ImGui::GetWindowDrawList()->AddText(
					titleFont, titleFont->FontSize,
					{ ImGui::GetWindowPos().x + ui::s(20), baselineY }, 
					ImGui::GetColorU32(ImGuiCol_TextDisabled, ui::anim), 
					name
				);
				
				// Breadcrumb / Subtab Name
				if (ui::tabs[ui::cur_tab].m_subtabs.size() > 0) {
					float nameWidth = titleFont->CalcTextSizeA(titleFont->FontSize, FLT_MAX, 0.f, name).x;
					
					ImGui::RenderArrow(
						ImGui::GetWindowDrawList(),
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
