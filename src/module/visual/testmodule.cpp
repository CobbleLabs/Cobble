#include "testmodule.h"
#include "../../sdk/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"

TestModule* testModule = nullptr;

TestModule::TestModule() : Module("TestModule")
{
	keybind = 0;  // No default keybind
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
			
			// Mode selector using ui::Combo for proper animation sync
			const char* modes[] = { "Mode A", "Mode B", "Mode C" };
			ui::Combo("Mode", &settings.testMode, modes, 3);
		},
		"Options", [this]() {
			ui::Checkbox("Test Toggle", &settings.testToggle);
			ImGui::Spacing();
			ImGui::TextWrapped("This is a test module to demonstrate the new modular UI system.");
		}
	);
}
