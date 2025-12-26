#pragma once

#include "module.h"
#include <chrono>
#include <random>

// Features:
// - SendMessage-based clicking (registers on CPS counters)
// - Weapon-only mode (swords, axes)
// - Ignore blocks option
// - Advanced randomization (drop, spike, kurtosis)
class AutoClicker : public Module
{
public:
	AutoClicker();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;

	// Configuration - public for future GUI/config integration
	struct Settings {
		float minCPS = 10.0f;
		float maxCPS = 16.0f;
		bool weaponOnly = false;
		bool ignoreBlocks = true;
		bool workInInventory = false;
		
		// Advanced randomization
		bool advancedMode = true;
		float dropChance = 5.0f;    // % chance to skip a click
		float spikeChance = 10.0f;  // % chance for faster click
		float spikeMultiplier = 1.3f;
		float kurtosis = 2.0f;      // Higher = more consistent CPS
	} settings;

private:
	std::chrono::steady_clock::time_point lastClick;
	int nextDelayMs = 100;
	std::mt19937 rng;
	float normalCps = 0.0f;

	void calculateNextDelay();
	bool shouldClick();
	void doClick();
	
	// Item checks
	bool isHoldingWeapon();
	bool isTargetingBlock();
	int getHeldItemId();
};

extern AutoClicker autoClicker;
