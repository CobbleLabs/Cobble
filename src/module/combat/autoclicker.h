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

	struct Settings {
		float minCPS = 14.0f;
		float maxCPS = 16.0f;
		bool weaponOnly = false;
		bool breakBlocks = false;     // Skip clicking when targeting block
		bool workInInventory = true;
		
		// Stable CPS mode - no randomization, exact CPS
		bool stableCPS = false;
		
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
	int nextDelayMs = 100;
	std::mt19937 rng;
	
	// Dynamic delay anti-detection system
	int clicksSinceRangeChange = 0;
	int clicksUntilRangeChange = 50;
	float currentDelayMin = 25.0f;  // Base delay range (ms)
	float currentDelayMax = 70.0f;
	
	// Alternating click pattern (more realistic - clicks have duration)
	bool lastClickWasDown = false;  // false = last was UP, next is DOWN
	
	// Block breaking state (edge detection)
	bool wasBreakingBlock = false;  // true = currently holding click on block
	
	// Session fatigue (speed varies over time)
	std::chrono::steady_clock::time_point sessionStart;
	float fatigueMultiplier = 1.0f;      // 1.0 = normal, >1.0 = slower (tired)
	int clicksSinceLastFatigueUpdate = 0;
	
	// Burst mode (accelerate/decelerate in clusters)
	bool inBurst = false;
	int burstClicksRemaining = 0;
	float burstSpeedMultiplier = 1.0f;   // <1.0 = faster, >1.0 = slower
	
	// Non-uniform jitter
	int currentJitterMode = 0;  // 0=none, 1=small, 2=medium, 3=large
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
};

extern AutoClicker* autoClicker;
