#pragma once

#include "../module.h"
#include <chrono>
#include <random>

class AutoClicker : public Module
{
public:
	AutoClicker();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;
	std::string getDisplayInfo() override;

	struct Settings {
		float minCPS = 14.0f;
		float maxCPS = 16.0f;
		bool weaponOnly = false;
		bool breakBlocks = false;     // Skip clicking when targeting block
		bool workInInventory = true;
		
		// Stable CPS mode - no randomization, exact CPS
		bool stableCPS = false;
		float stableCPSValue = 17.0f;  // Exact CPS when stable mode is on
		
		// Advanced randomization (only when stableCPS is false)
		bool advancedMode = true;
		float dropChance = 5.0f;    // % chance to skip a click
		float spikeChance = 10.0f;  // % chance for faster click
		float spikeMultiplier = 1.3f;
		float kurtosis = 2.0f;      // Higher = more consistent CPS
		
		// Anti-detection
		bool attackingOnly = false;  // Only click when player in crosshair
	} settings;

private:
	std::chrono::steady_clock::time_point lastClick;
	std::chrono::steady_clock::time_point nextEventTime;
	std::chrono::steady_clock::time_point nextStableClickTime;
	double stableIntervalMs = 0.0;
	std::chrono::steady_clock::duration stableInterval =
		std::chrono::steady_clock::duration::zero();
	int nextDelayMs = 100;
	std::mt19937 rng;
	
	// Dynamic delay anti-detection system
	int clicksSinceRangeChange = 0;
	int clicksUntilRangeChange = 50;
	float currentDelayMin = 25.0f;
	float currentDelayMax = 70.0f;
	
	// Alternating click pattern
	bool lastClickWasDown = false;
	
	// Block breaking state
	bool wasBreakingBlock = false;
	
	// Session fatigue
	std::chrono::steady_clock::time_point sessionStart;
	float fatigueMultiplier = 1.0f;
	int clicksSinceLastFatigueUpdate = 0;
	
	// Burst mode
	bool inBurst = false;
	int burstClicksRemaining = 0;
	float burstSpeedMultiplier = 1.0f;
	
	// Non-uniform jitter
	int currentJitterMode = 0;
	int jitterModeClicksRemaining = 0;

public:
	// For AimAssist integration
	std::chrono::steady_clock::time_point lastAutoClick;

	void calculateNextDelay();
	void regenerateDelayRange();
	void doClick();
	
	// Humanization
	void updateFatigue();
	void updateBurstMode();
	void updateJitterMode();
	
	// Checks
	bool isHoldingWeapon();
	bool isTargetingBlock();
	bool isTargetingPlayer();
	int getHeldItemId();
	
private:
	float delayAccumulator = 0.0f;
};

extern AutoClicker* autoClicker;
