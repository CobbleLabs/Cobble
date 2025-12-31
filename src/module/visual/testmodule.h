#pragma once

#include "../module.h"

// Test module - demonstrates the new modular UI system
class TestModule : public Module
{
public:
	TestModule();
	
	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;
	
	struct Settings {
		float testValue = 5.0f;
		bool testToggle = true;
		int testMode = 0;
	} settings;
};

extern TestModule* testModule;
