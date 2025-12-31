#include "testmodule.h"
#include "../../sdk/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"

TestModule* testModule = nullptr;

TestModule::TestModule() : Module("TestModule")
{
	keybind = 'T';  // Default keybind
}

void TestModule::onEnable()
{
	log_ok("TestModule enabled!");
}

void TestModule::onDisable()
{
	log_ok("TestModule disabled!");
}

void TestModule::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (!player.isValid()) return;
	
	// Test module logic would go here
}

void TestModule::onRenderMenu()
{
	ui::two_column(
		"Settings", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::SliderFloat("Test Value", &settings.testValue, 0.0f, 10.0f, "%.1f");
			
			// Mode selector example
			const char* modes[] = { "Mode A", "Mode B", "Mode C" };
			ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
			if (ImGui::BeginCombo("Mode", modes[settings.testMode], ImGuiComboFlags_NoArrowButton)) {
				for (int i = 0; i < 3; i++) {
					bool selected = (settings.testMode == i);
					if (ImGui::Selectable(modes[i], selected))
						settings.testMode = i;
					if (selected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
		},
		"Options", [this]() {
			ui::Checkbox("Test Toggle", &settings.testToggle);
			ImGui::Spacing();
			ImGui::TextWrapped("This is a test module to demonstrate the new modular UI system.");
		}
	);
}
