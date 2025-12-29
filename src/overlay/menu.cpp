#include "menu.h"
#include "custom_widgets.hpp"
#include "../module/module_manager.h"
#include "../module/combat/autoclicker.h"
#include "../module/combat/aimassist.h"
#include "../module/combat/autorefill.h"
#include "../module/visual/nametags.h"
#include "../sdk/render_data.h"
#include "../cobble.hpp"
#include <string>

// Pink Minecraft color scheme
// Pink Minecraft color scheme (Dynamic)
#define COL_SCHEME      ImVec4(custom::AccentColor[0], custom::AccentColor[1], custom::AccentColor[2], custom::AccentColor[3])
#define COL_BG          ImVec4(0.08f, 0.08f, 0.10f, 1.0f)
#define COL_BG_CHILD    ImVec4(0.12f, 0.12f, 0.14f, 1.0f)
#define COL_BORDER      ImVec4(0.20f, 0.20f, 0.22f, 0.50f)

void Menu::ApplyStyle()
{
	ImGuiStyle& style = ImGui::GetStyle();
	
	style.WindowRounding = 12.0f;
	style.ChildRounding = 10.0f;
	style.FrameRounding = 6.0f;
	style.GrabRounding = 6.0f;
	style.PopupRounding = 8.0f;
	style.ScrollbarRounding = 8.0f;
	
	style.FramePadding = ImVec2(12, 10); // +20%
	style.ItemSpacing = ImVec2(12, 12); // +20%
	style.ItemInnerSpacing = ImVec2(10, 6); // More gap between Checkbox and Text
	style.WindowPadding = ImVec2(0, 0);
	
	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
	colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
	colors[ImGuiCol_WindowBg] = COL_BG;
	colors[ImGuiCol_ChildBg] = COL_BG_CHILD;
	colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
	
	colors[ImGuiCol_Border] = COL_BORDER;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	
	colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
	colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
	colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
	
	colors[ImGuiCol_TitleBg] = COL_BG;
	colors[ImGuiCol_TitleBgActive] = COL_BG;
	colors[ImGuiCol_TitleBgCollapsed] = COL_BG; // Redundant but safe
	
	colors[ImGuiCol_CheckMark] = COL_SCHEME;
	
	colors[ImGuiCol_Button] = ImVec4(0.18f, 0.18f, 0.20f, 1.00f);
	colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.25f, 0.27f, 1.00f);
	colors[ImGuiCol_ButtonActive] = ImVec4(0.15f, 0.15f, 0.17f, 1.00f);
	
	colors[ImGuiCol_Header] = ImVec4(0.22f, 0.22f, 0.24f, 1.00f); // Used for combo etc
	
	colors[ImGuiCol_ResizeGrip] = COL_SCHEME;
}

// Helper to get key name
static std::string GetKeyName(int key) {
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
	
	float avail = ImGui::GetContentRegionAvail().x;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 80);
	
	std::string text = (moduleToBind == module) ? "..." : GetKeyName(module->keybind);
	if (text.empty()) text = "?";
	
	std::string btnText = "[ " + text + " ]";
	
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.17f, 1.0f));
	if (ImGui::Button(btnText.c_str(), ImVec2(70, 0))) {
		moduleToBind = module;
	}
	ImGui::PopStyleColor();
	
	ImGui::PopID();
}

void Menu::Render()
{
	if (!isOpen) return;
	
	ImGuiIO& io = ImGui::GetIO();
	
	// ------ MENU OPEN ANIMATION ------
	// Scale: 0.97 -> 1.0, Alpha: 0 -> 1 (easeOutQuart)
	static bool wasOpen = false;
	static float openAnim = 0.f;
	
	if (!wasOpen && isOpen) {
		openAnim = 0.f; // Reset on open
	}
	wasOpen = isOpen;
	
	openAnim = ImTricks::Animations::FastFloatLerp("menu_open", isOpen, 0.f, 1.f, 0.06f);
	
	// EaseOutQuart: 1 - (1 - x)^4
	float eased = 1.0f - powf(1.0f - openAnim, 4.0f);
	float scale = 0.97f + 0.03f * eased;
	float alpha = eased;
	
	ImVec2 windowSize(1050 * scale, 750 * scale); // Scaled size
	ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
	
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
	ImGui::Begin("Cobble Client", &isOpen, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
	{
		ImGui::PopStyleVar(); // WindowPadding
		
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 p = ImGui::GetWindowPos();
		
	// Navbar fixed width - Increased by ~15% (185 -> 215)
		float currentNavWidth = 215.0f;

	// Main content area - Increased margin 25->35
		float startContentX = currentNavWidth + 35;
		
		// Align Title with Card Text (Shift +15px)
		ImGui::SetCursorPos({ startContentX + 15.0f, 35 }); 
		
		ImGui::BeginGroup();
		
		// Tab title
		const char* tabTitles[] = { "Combat", "Visual", "Settings" };
		ImGui::PushFont(io.Fonts->Fonts[1]); // Header Font (Bold 26px)
		ImGui::PushStyleColor(ImGuiCol_Text, COL_SCHEME);
		ImGui::Text("%s", tabTitles[currentTab]);
		ImGui::PopStyleColor();
		ImGui::PopFont();
		
		// Main content child - Reset X to original start
		ImGui::SetCursorPosX(startContentX);
		
		// Width = WindowSize - Sidebar - LeftMargin(35) - RightGutter(35) = WindowSize - Sidebar - 70
		ImGui::BeginChild("main_content", { window->Size.x - currentNavWidth - 70.0f, window->Size.y - ImGui::GetCursorPosY() + 5 }, 0, ImGuiWindowFlags_NoBackground);
		{
			// ------ SECTION TRANSITION (FADE ONLY) ------
			static int lastTab = -1;
			static float fade = 1.0f;
			
			if (currentTab != lastTab) {
				lastTab = currentTab;
				fade = 0.0f; // reset real
			}
			
			// ultra smooth fade (dt-safe)
			float speed = 6.5f; // Slower for premium feel
			fade = ImMin(1.0f, fade + ImGui::GetIO().DeltaTime * speed);
			
			// EaseOutSine (smoother than cubic)
			float eased = sinf((fade * 3.14159f) * 0.5f);
			
			ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);
			
			switch (currentTab) {
				case 0: RenderCombatTab(); break;
				case 1: RenderVisualTab(); break;
				case 2: RenderSettingsTab(); break;
			}
			
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
		
		ImGui::EndGroup();
		
		// Left Navbar
		ImGui::SetCursorPos({ 0, 0 });
		ImGui::BeginChild("navbar", { currentNavWidth, window->Size.y }, 0, ImGuiWindowFlags_NoBackground);
		{
			// Navbar background
			ImGui::GetWindowDrawList()->AddRectFilled(p, p + ImGui::GetWindowSize(), ImGui::GetColorU32(ImGuiCol_ChildBg), ImGui::GetStyle().WindowRounding, ImDrawFlags_RoundCornersLeft);
			ImGui::GetWindowDrawList()->AddRectFilled({ p.x + ImGui::GetWindowWidth() - 1, p.y }, p + ImGui::GetWindowSize(), ImGui::GetColorU32(ImGuiCol_Border));
			
			// ------ HEADER BLOCK: "Cobble" ------
			// Brand anchor - EXCLUSIVE larger font, low position, optical center
			ImGui::SetCursorPosY(68); // Final position for brand block
			
			ImGui::PushFont(io.Fonts->Fonts[3]); // 46px Bold - BRAND EXCLUSIVE
			const char* title = "Cobble";
			float titleWidth = ImGui::CalcTextSize(title).x;
			ImGui::SetCursorPosX((currentNavWidth - titleWidth) * 0.5f + 1.0f); // Optical center +1px
			ImGui::PushStyleColor(ImGuiCol_Text, COL_SCHEME);
			ImGui::Text(title);
			ImGui::PopStyleColor();
			ImGui::PopFont();
			
			// ------ TABS ------
			// Start lower to maintain hierarchy rhythm
			ImGui::SetCursorPosY(165);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 0, 15 });
			
			if (custom::tab(custom::Icon_Combat, "Combat", currentTab == 0)) currentTab = 0;
			if (custom::tab(custom::Icon_Visual, "Visual", currentTab == 1)) currentTab = 1;
			if (custom::tab(custom::Icon_Settings, "Settings", currentTab == 2)) currentTab = 2;
			
			ImGui::PopStyleVar();
		}
		ImGui::EndChild();
	}
	ImGui::End();
	ImGui::PopStyleVar(); // Pop Alpha from open animation
}

void Menu::RenderCombatTab()
{
	// Subtabs logic
	// Removed SetCursorPosX(0) so child window inherits cursor position from parent animation
	ImGui::BeginChild("combat_subtabs", { ImGui::GetWindowWidth(), 45 }, true);
	{
		ImGui::SetCursorPos({ 0, 8 });
		static int combatSubtab = 0;
		static int prevSubtab = 0;
		const char* subtabNames[] = { "AutoClicker", "AimAssist", "AutoRefill" };
		
		// Calculate total width of subtabs group
		float totalWidth = 0.f;
		for (int i = 0; i < 3; i++) {
			totalWidth += ImGui::CalcTextSize(subtabNames[i]).x + 16.0f; // Padding from widget
			if (i < 2) totalWidth += 20.0f; // Spacing
		}
		
		// Set Cursor X to center relative to CONTENT AVAIL
		float availWidth = ImGui::GetContentRegionAvail().x;
		float startX = (availWidth - totalWidth) / 2.0f;
		ImGui::SetCursorPosX(startX);
		
		int newSubtab = combatSubtab;
		if (custom::subtab(subtabNames[0], combatSubtab == 0)) newSubtab = 0; ImGui::SameLine(0, 20);
		if (custom::subtab(subtabNames[1], combatSubtab == 1)) newSubtab = 1; ImGui::SameLine(0, 20);
		if (custom::subtab(subtabNames[2], combatSubtab == 2)) newSubtab = 2;
		
		// Detect tab change for animation
		if (newSubtab != combatSubtab) {
			prevSubtab = combatSubtab;
			contentAnim = 0.f; // Reset animation
		}
		combatSubtab = newSubtab;
		currentSubtab = combatSubtab;
	}
	ImGui::EndChild();
	
	ImGui::Spacing();
	
	// ------ CONTENT TRANSITION ANIMATION (Manual Logic) ------
	static int lastSubtab = -1;
	static float t = 1.0f;       // 0 -> 1
	static float dir = 1.0f;     // +1 / -1
	
	if (currentSubtab != lastSubtab) {
		dir = (lastSubtab != -1 && currentSubtab < lastSubtab) ? -1.0f : 1.0f;
		t = 0.0f;                // reset real
		lastSubtab = currentSubtab;
	}
	
	// “premium smooth” (dt-safe)
	float speed = 10.0f; // 8..14 range
	float dt = ImGui::GetIO().DeltaTime;
	t = ImMin(1.0f, t + dt * speed);
	
	// easeOutCubic
	float eased = 1.0f - powf(1.0f - t, 3.0f);
	
	float offsetX = (1.0f - eased) * 12.0f * dir;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased); // Don't forget to POP this later
	
	// Content area
	ImGui::BeginChild("combat_content", { 0, 0 }, 0, ImGuiWindowFlags_NoBackground);
	{
		// Use Columns for Window Layout
		ImGui::Columns(2, "combat_layout", false);
		// Let columns split evenly by default or force 50%
		// ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f); // Optional if default is fine
		
		// Use FULL column width minus separation spacing
		// This creates the gap between cards in different columns
		float colWidth = ImGui::GetColumnWidth() - 10.0f; 
		
		switch (currentSubtab) {
			case 0: // AutoClicker
			{
				// Left Column: Main Settings
				custom::begin_child("General", { colWidth, 0 });
				{
					// Header Row
					custom::checkbox("Enabled", &autoClicker.enabled);
					ImGui::SameLine();
					Hotkey("AC_Key", &autoClicker);
					
					ImGui::Dummy({ 0, 5 });
					ImGui::Separator();
					ImGui::Dummy({ 0, 5 });
					
					// Always show settings regardless of enabled state
					custom::slider_float("Min CPS", &autoClicker.settings.minCPS, 1.0f, 20.0f);
					custom::slider_float("Max CPS", &autoClicker.settings.maxCPS, 1.0f, 20.0f);
					
					if (autoClicker.settings.minCPS > autoClicker.settings.maxCPS) autoClicker.settings.minCPS = autoClicker.settings.maxCPS;
				}
				custom::end_child();
				
				ImGui::NextColumn();
				
				// Right Column: Options
				custom::begin_child("Conditions", { colWidth, 0 });
				{
					custom::checkbox("Weapon Only", &autoClicker.settings.weaponOnly);
					custom::checkbox("Break Blocks", &autoClicker.settings.breakBlocks);
					custom::checkbox("Inventory", &autoClicker.settings.workInInventory);
				}
				custom::end_child();
			}
			break;
			
			case 1: // AimAssist
			{
				custom::begin_child("General", { colWidth, 0 });
				{
					custom::checkbox("Enabled", &aimAssist.enabled);
					ImGui::SameLine();
					Hotkey("AA_Key", &aimAssist);
					
					ImGui::Dummy({ 0, 5 });
					ImGui::Separator();
					ImGui::Dummy({ 0, 5 });
					
					// Always show settings
					custom::slider_float("H Speed", &aimAssist.settings.horizontalSpeed, 1.0f, 100.0f);
					custom::slider_float("V Speed", &aimAssist.settings.verticalSpeed, 1.0f, 100.0f);
					custom::slider_float("FOV", &aimAssist.settings.maxFov, 10.0f, 360.0f, "%.0f");
					custom::slider_float("Distance", &aimAssist.settings.maxDistance, 3.0f, 8.0f);
				}
				custom::end_child();
				
				ImGui::NextColumn();
				
				custom::begin_child("Filters", { colWidth, 0 });
				{
					custom::checkbox("Clicking Only", &aimAssist.settings.onlyClicking);
					custom::checkbox("Vertical Aim", &aimAssist.settings.verticalAim);
					custom::checkbox("Target Lock", &aimAssist.settings.targetLock);
					custom::checkbox("Visibility", &aimAssist.settings.visibilityCheck);
				}
				custom::end_child();
			}
			break;
			
			case 2: // AutoRefill
			{
				custom::begin_child("General", { colWidth, 0 });
				{
					ImGui::Text("Press keybind to refill");
					Hotkey("AR_Key", &autoRefill);
					
					ImGui::Dummy({ 0, 5 });
					ImGui::Separator();
					ImGui::Dummy({ 0, 5 });
					
					custom::slider_int("Speed (ms)", &autoRefill.settings.speed, 20, 200);
				}
				custom::end_child();
				
				ImGui::NextColumn();
				
				custom::begin_child("Items", { colWidth, 0 });
				{
					custom::checkbox("Refill Soup", &autoRefill.settings.refillSoup);
					custom::checkbox("Refill Potions", &autoRefill.settings.refillPotions);
					custom::checkbox("Smart Speed", &autoRefill.settings.smartSpeed);
				}
				custom::end_child();
			}
			break;
		}
		
		ImGui::Columns(1);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(); // Pop Alpha from content transition
}

void Menu::RenderVisualTab()
{
	static int visualSubtab = 0;

	// Subtabs logic
	ImGui::BeginChild("visual_subtabs", { ImGui::GetWindowWidth(), 45 }, true);
	{
		ImGui::SetCursorPos({ 0, 8 });
		const char* subtabNames[] = { "NameTags", "ESP", "Ambience" };
		
		// Center subtabs
		float totalWidth = 0.f;
		for (int i = 0; i < 3; i++) {
			totalWidth += ImGui::CalcTextSize(subtabNames[i]).x + 16.0f + (i < 2 ? 20.0f : 0.0f);
		}
		ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - totalWidth) / 2.0f);
		
		int newSubtab = visualSubtab;
		if (custom::subtab(subtabNames[0], visualSubtab == 0)) newSubtab = 0; ImGui::SameLine(0, 20);
		if (custom::subtab(subtabNames[1], visualSubtab == 1)) newSubtab = 1; ImGui::SameLine(0, 20);
		if (custom::subtab(subtabNames[2], visualSubtab == 2)) newSubtab = 2;
		
		visualSubtab = newSubtab;
	}
	ImGui::EndChild();
	
	ImGui::Spacing();
	
	// ------ CONTENT TRANSITION ANIMATION (Manual Logic) ------
	static int lastSubtab = -1;
	static int currentSubtab = -1; // Local tracker for animation logic
	static float t = 1.0f;
	static float dir = 1.0f;
	
	// Sync with static visualSubtab
	if (visualSubtab != currentSubtab) {
		currentSubtab = visualSubtab;
		dir = (lastSubtab != -1 && currentSubtab < lastSubtab) ? -1.0f : 1.0f;
		t = 0.0f;
		lastSubtab = currentSubtab;
	}
	
	float speed = 10.0f;
	float dt = ImGui::GetIO().DeltaTime;
	t = ImMin(1.0f, t + dt * speed);
	
	float eased = 1.0f - powf(1.0f - t, 3.0f);
	float offsetX = (1.0f - eased) * 12.0f * dir;
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
	ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);
	
	ImGui::BeginChild("visual_content", { 0, 0 }, 0, ImGuiWindowFlags_NoBackground);
	{
		float colWidth = (ImGui::GetContentRegionAvail().x - 15) / 2;
		ImGui::Columns(2, nullptr, false);
		ImGui::SetColumnWidth(0, colWidth + 5);
		
		switch (visualSubtab) {
			case 0: // NameTags
			{
				custom::begin_child("Settings", { colWidth, 0 });
				{
					custom::checkbox("Enabled", &nameTags.enabled);
					Hotkey("NT_Key", &nameTags);
					ImGui::Dummy({ 0, 5 });
					
					// ALWAYS VISIBLE (User request)
					custom::slider_float("Scale", &nameTags.settings.scale, 0.5f, 2.0f);
					custom::checkbox("Background", &nameTags.settings.background);
				}
				custom::end_child();
				
				ImGui::NextColumn();
				
				custom::begin_child("Display", { colWidth, 0 });
				{
					custom::checkbox("Show Health", &nameTags.settings.showHealth);
					custom::checkbox("Show Distance", &nameTags.settings.showDistance);
					// custom::checkbox("Show Armor", &nameTags.settings.showArmor); // TODO: Add to struct
				}
				custom::end_child();
				break;
			}
			case 1: // ESP
			{
				custom::begin_child("Player ESP", { colWidth, 0 });
				{
					static bool espEnabled = false;
					custom::checkbox("Enabled", &espEnabled);
					static bool box = false;
					custom::checkbox("Box 2D", &box);
				}
				custom::end_child();
				break;
			}
			case 2: // Ambience
			{
				custom::begin_child("World", { colWidth, 0 });
				{
					static bool timeChanger = false;
					custom::checkbox("Time Changer", &timeChanger);
					static int time = 12000;
					if(timeChanger) custom::slider_int("Time", &time, 0, 24000);
				}
				custom::end_child();
				break;
			}
		}
		
		ImGui::Columns(1);
	}
	ImGui::EndChild();
	ImGui::PopStyleVar();
}

void Menu::RenderSettingsTab()
{
	ImGui::BeginChild("settings_content", { ImGui::GetWindowWidth(), ImGui::GetWindowHeight() - 10 }, 0, ImGuiWindowFlags_NoBackground);
	{
		float childWidth = (ImGui::GetWindowWidth() - 25) / 2;
		
		ImGui::Columns(2, nullptr, false);
		ImGui::SetColumnWidth(0, childWidth + 5);
		
		// Left Column: Client Info (Clean) TODO: CHANGE
		custom::begin_child("Client Info", { childWidth, 0 });
		{
			ImGui::TextDisabled("Version 1.0.0");
			ImGui::TextDisabled("Built: Dec 2025");
			ImGui::Spacing();
			ImGui::Text("User: Admin");
		}
		custom::end_child();
		
		ImGui::NextColumn();
		
		// Right Column: Theme & Actions
		
		// Theme (Color Picker) - Top Right
		custom::begin_child("Theme", { childWidth, 0 });
		{
			ImGui::Text("Accent Color");
			ImGui::Spacing();
			
			// Color Picker
			ImGui::PushItemWidth(childWidth - 30);
			ImGui::ColorPicker4("##picker", custom::AccentColor, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoInputs);
			ImGui::PopItemWidth();
		}
		custom::end_child();
		
		// Actions - Bottom Right
		custom::begin_child("Actions", { childWidth, 120 });
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.2f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.15f, 0.25f, 1.0f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.08f, 0.15f, 1.0f));
			
			if (ImGui::Button("EJECT CLIENT", ImVec2(160, 40))) {
				cobble::context::is_running = false;
				isOpen = false;
			}
			
			ImGui::PopStyleColor(3);
		}
		custom::end_child();
		
		ImGui::Columns(1);
	}
	ImGui::EndChild();
}
