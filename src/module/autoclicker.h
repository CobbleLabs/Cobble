#pragma once

#include "module.h"
#include <chrono>
#include <random>

// Anti-detection AutoClicker with Dream-Advanced techniques:
// - PostMessage-based clicking (async, more natural)
// - Dynamic delay ranges that change every 30-70 clicks
// - Attacking only mode (only click when player in crosshair)
// - Break blocks mode (hold click when targeting block)
class AutoClicker : public Module
{
public:
	AutoClicker();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;

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
		
		// Anti-detection (new)
		bool attackingOnly = false;  // Only click when player in crosshair
		bool breakBlocks = false;    // Hold click when targeting block
	} settings;

private:
	std::chrono::steady_clock::time_point lastClick;
	int nextDelayMs = 100;
	std::mt19937 rng;
	
	// Dynamic delay anti-detection system
	int clicksSinceRangeChange = 0;
	int clicksUntilRangeChange = 50;
	float currentDelayMin = 25.0f;  // Base delay range (ms)
	float currentDelayMax = 70.0f;
	
	// Break blocks state
	bool holdingMouseDown = false;

public:
	// For AimAssist integration
	std::chrono::steady_clock::time_point lastAutoClick;

	void calculateNextDelay();
	void regenerateDelayRange();
	void doClick();
	void releaseClick();
	
	// Checks
	bool isHoldingWeapon();
	bool isTargetingBlock();
	bool isTargetingPlayer();
	int getHeldItemId();
};

extern AutoClicker autoClicker;
