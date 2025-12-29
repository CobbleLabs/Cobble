#include "menu.h"
#include "../module/module_manager.h"
#include "../module/combat/autoclicker.h"
#include "../module/combat/aimassist.h"
#include "../module/combat/autorefill.h"
#include "../module/visual/nametags.h"
#include "../sdk/render_data.h"
#include "../cobble.hpp"
#include <string>

// Colors
#define COL_ACCENT ImVec4(0.8f, 0.2f, 0.3f, 1.0f) // Red/Pink accent
#define COL_BG ImVec4(0.12f, 0.12f, 0.14f, 1.0f)  // Dark Grey
#define COL_BG_CHILD ImVec4(0.18f, 0.18f, 0.20f, 1.0f)

void Menu::ApplyStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	
	style.WindowRounding = 8.0f;
	style.ChildRounding = 6.0f;
	style.FrameRounding = 4.0f;
	style.GrabRounding = 4.0f;
	style.PopupRounding = 6.0f;
	style.ScrollbarRounding = 6.0f;
	
	style.FramePadding = ImVec2(8, 6);
	style.ItemSpacing = ImVec2(8, 8);
	
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = COL_BG;
	colors[ImGuiCol_ChildBg] = COL_BG_CHILD;
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	
	colors[ImGuiCol_Border] = ImVec4(0.25f, 0.25f, 0.25f, 0.50f);
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	
	colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
	
	colors[ImGuiCol_TitleBg] = COL_BG;
	colors[ImGuiCol_TitleBgActive] = COL_BG;
	colors[ImGuiCol_TitleBgCollapsed] = COL_BG;
	
	colors[ImGuiCol_CheckMark] = COL_ACCENT;
	
	colors[ImGuiCol_SliderGrab] = COL_ACCENT;
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.9f, 0.3f, 0.4f, 1.0f);
	
	colors[ImGuiCol_Button] = ImVec4(0.20f, 0.20f, 0.22f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
	
	colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
	colors[ImGuiCol_HeaderHovered] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
	colors[ImGuiCol_HeaderActive] = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);
	
	colors[ImGuiCol_Separator] = colors[ImGuiCol_Border];
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.59f, 0.98f, 0.25f);
	colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.59f, 0.98f, 0.67f);
	colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.59f, 0.98f, 0.95f);
	
	colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
	colors[ImGuiCol_TabHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
	colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
	colors[ImGuiCol_TabUnfocused] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
	colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
}

void Menu::Render()
{
	if (!isOpen) return;
	
	// Center window
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 windowSize(600, 450);
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
	
	// Main Window
	if (ImGui::Begin("Cobble Client", &isOpen, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove)) {
		
		if (ImGui::BeginTabBar("Tabs")) {
			
			if (ImGui::BeginTabItem("Combat")) {
				RenderCombatTab();
				ImGui::EndTabItem();
			}
			
			if (ImGui::BeginTabItem("Visual")) {
				RenderVisualTab();
				ImGui::EndTabItem();
			}
			
			if (ImGui::BeginTabItem("Settings")) {
				RenderSettingsTab();
				ImGui::EndTabItem();
			}
			
			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}


// Helper to get key name
std::string GetKeyName(int key) {
	if (key == 0) return "NONE";
	char name[128] = {0};
	int scanCode = MapVirtualKeyA(key, MAPVK_VK_TO_VSC);
	int lParam = (scanCode << 16);
	switch (key) {
		case VK_LEFT: strcpy_s(name, "LEFT"); break;
		case VK_RIGHT: strcpy_s(name, "RIGHT"); break;
		case VK_UP: strcpy_s(name, "UP"); break;
		case VK_DOWN: strcpy_s(name, "DOWN"); break;
		case VK_DELETE: strcpy_s(name, "DEL"); break;
		case VK_END: strcpy_s(name, "END"); break;
		case VK_HOME: strcpy_s(name, "HOME"); break;
		case VK_INSERT: strcpy_s(name, "INS"); break;
		case VK_NEXT: strcpy_s(name, "PGDN"); break;
		case VK_PRIOR: strcpy_s(name, "PGUP"); break;
		default: GetKeyNameTextA(lParam, name, 128);
	}
	return std::string(name);
}

void Menu::Hotkey(const char* label, Module* module)
{
	ImGui::SameLine();
	ImGui::PushID(label);
	
	// Right align - fixed width 80
	float avail = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 90);
	
	std::string text = (moduleToBind == module) ? "..." : GetKeyName(module->keybind);
	if (text.empty()) text = "?";
	
	std::string btnText = "[ " + text + " ]";
	
	// Use 0 for height to let ImGui calculate it based on font (centers vertically)
	if (ImGui::Button(btnText.c_str(), ImVec2(80, 0))) {
		moduleToBind = module;
	}
	
	ImGui::PopID();
}

void Menu::RenderCombatTab()
{
	// AutoClicker
	if (ImGui::CollapsingHeader("AutoClicker", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enabled ##AC", &autoClicker.enabled);
		Menu::Hotkey("AC_Key", &autoClicker);
		
		if (autoClicker.enabled) {
			ImGui::Indent();
			
			ImGui::Text("Min CPS: %.1f", autoClicker.settings.minCPS);
			ImGui::SliderFloat("##MinCPS", &autoClicker.settings.minCPS, 1.0f, 20.0f, "");
			
			ImGui::Text("Max CPS: %.1f", autoClicker.settings.maxCPS);
			ImGui::SliderFloat("##MaxCPS", &autoClicker.settings.maxCPS, 1.0f, 20.0f, "");
			
			if (autoClicker.settings.minCPS > autoClicker.settings.maxCPS) {
				autoClicker.settings.minCPS = autoClicker.settings.maxCPS;
			}
			
			ImGui::Checkbox("Weapon Only", &autoClicker.settings.weaponOnly);
			ImGui::Checkbox("Break Blocks", &autoClicker.settings.breakBlocks);
			ImGui::Checkbox("Work in Inventory", &autoClicker.settings.workInInventory);
			ImGui::Unindent();
		}
	}
	
	// AimAssist
	if (ImGui::CollapsingHeader("AimAssist", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enabled ##AA", &aimAssist.enabled);
		Menu::Hotkey("AA_Key", &aimAssist);
		
		if (aimAssist.enabled) {
			ImGui::Indent();
			
			ImGui::Text("Horizontal Speed: %.1f", aimAssist.settings.horizontalSpeed);
			ImGui::SliderFloat("##HSpd", &aimAssist.settings.horizontalSpeed, 1.0f, 100.0f, "");
			
			ImGui::Text("Vertical Speed: %.1f", aimAssist.settings.verticalSpeed);
			ImGui::SliderFloat("##VSpd", &aimAssist.settings.verticalSpeed, 1.0f, 100.0f, "");
			
			ImGui::Text("Field of View: %.0f", aimAssist.settings.maxFov);
			ImGui::SliderFloat("##FOV", &aimAssist.settings.maxFov, 10.0f, 360.0f, "");
			
			ImGui::Text("Distance: %.1f m", aimAssist.settings.maxDistance);
			ImGui::SliderFloat("##Dist", &aimAssist.settings.maxDistance, 3.0f, 8.0f, "");
			
			ImGui::Checkbox("Clicking Only", &aimAssist.settings.onlyClicking);
			ImGui::Checkbox("Vertical Aim", &aimAssist.settings.verticalAim);
			ImGui::Checkbox("Target Lock", &aimAssist.settings.targetLock);
			ImGui::Checkbox("Visibility Check", &aimAssist.settings.visibilityCheck);
			ImGui::Unindent();
		}
	}
	
	// AutoRefill
	if (ImGui::CollapsingHeader("AutoRefill", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("AutoRefill (Use Keybind)");
		Menu::Hotkey("AR_Key", &autoRefill);
		
		ImGui::Indent();
		ImGui::Checkbox("Refill Soup", &autoRefill.settings.refillSoup);
		ImGui::Checkbox("Refill Potions", &autoRefill.settings.refillPotions);
		
		ImGui::Text("Refill Speed: %d ms", autoRefill.settings.speed);
		ImGui::SliderInt("##Speed", &autoRefill.settings.speed, 20, 200, "");
		
		ImGui::Checkbox("Smart Speed", &autoRefill.settings.smartSpeed);
		ImGui::Unindent();
	}
}

void Menu::RenderVisualTab()
{
	// NameTags
	if (ImGui::CollapsingHeader("NameTags", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Enabled ##NT", &nameTags.enabled);
		Menu::Hotkey("NT_Key", &nameTags);
		
		if (nameTags.enabled) {
			ImGui::Indent();
			
			ImGui::Text("Scale: %.2f x", nameTags.settings.scale);
			ImGui::SliderFloat("##Scale", &nameTags.settings.scale, 0.5f, 2.0f, "");
			
			ImGui::Checkbox("Background", &nameTags.settings.background);
			ImGui::Checkbox("Show Health", &nameTags.settings.showHealth);
			ImGui::Checkbox("Show Distance", &nameTags.settings.showDistance);
			ImGui::Unindent();
		}
	}
}

void Menu::RenderSettingsTab()
{
	ImGui::Text("Client Settings");
	ImGui::Separator();
	
	ImGui::TextDisabled("Build: Dec 29 2025");
	ImGui::Spacing();
	
	ImGui::Text("Menu Key: INSERT");
	ImGui::Spacing();
	
	ImGui::Separator();
	ImGui::Spacing();
	
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.1f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
	
	if (ImGui::Button("EJECT CLIENT", ImVec2(150, 40))) {
		// Signal DLL to unload
		cobble::context::is_running = false;
		isOpen = false;
	}
	
	ImGui::PopStyleColor(3);
}
