#pragma once

#include "../module.h"
#include "../../jni/java.h"  // For jobject

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>  // For ULONGLONG

class Reach : public Module
{
public:
	Reach();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;
	std::string getDisplayInfo() override;

	struct Settings {
		float distance = 3.5f;        // Reach distance (vanilla is 3.0)
		float minDistance = 3.0f;     // Min reach for randomization
		float maxDistance = 3.5f;     // Max reach for randomization
		bool randomize = false;       // Use random reach within range
		bool weaponsOnly = false;     // Only apply when holding weapon
		bool sprintingOnly = false;   // Only apply while sprinting
		bool movingOnly = false;      // Only apply while moving forward
		
		// Anti-detection settings
		int hitChance = 100;          // Chance (%) to apply reach on each swing (breaks patterns)
		bool onlyMovingTargets = false;// Only extend reach on targets moving toward player
		int maxConsecutiveHits = 10;  // Max consecutive extended hits before forcing vanilla
	} settings;

private:
	float currentReach = 3.0f;
	int consecutiveExtendedHits = 0;  // Track consecutive extended reach hits
	ULONGLONG lastHitTime = 0;        // Timing for hit tracking
	ULONGLONG lastSwingTime = 0;      // Debounce rapid swings
	
	// Original hitbox values for restoration
	struct OriginalHitbox {
		double minX = 0, maxX = 0, minZ = 0, maxZ = 0;
		jobject entityRef = nullptr;  // Weak ref to track which entity
		bool valid = false;
	} originalHitbox;
	
	bool isHoldingWeapon();
	void updateReach();
	bool shouldApplyThisSwing();       // Chance-based application
	void restoreHitbox(JNIEnv* env);   // Restore original hitbox after hit
};

extern Reach* reach;
