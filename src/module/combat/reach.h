#pragma once

#include "../module.h"
#include "../../jni/java.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

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
		float distance = 3.1f;        // Reach distance (fixed mode)
		float minDistance = 3.0f;     // Min reach (interval mode)
		float maxDistance = 3.2f;     // Max reach (interval mode)
		bool intervalMode = false;    // Use random reach between min/max
		bool weaponsOnly = false;     // Only apply when holding weapon
		bool sprintingOnly = false;   // Only apply while sprinting
		bool movingOnly = false;      // Only apply while moving forward
		
		// Anti-detection
		int hitChance = 100;          // Chance (%) to apply reach (0=never, 100=always)
		int maxConsecutiveHits = 0;   // Max consecutive extended hits (0=disabled)
		bool onlyMovingTargets = false;// Only extend on targets moving toward us
	} settings;

private:
	float currentReach = 3.0f;
	int consecutiveExtendedHits = 0;
	ULONGLONG lastHitTime = 0;
	
	// Click state tracking (to evaluate hit chance once per click, not per frame)
	bool wasClickHeld = false;
	bool currentClickAllowed = false;
	
	// Entity override state
	struct OverrideState {
		jobject originalEntityHit = nullptr;
		jobject originalTypeOfHit = nullptr;
		bool needsRestore = false;
	} overrideState;
	
	bool isHoldingWeapon();
	void updateReach();
	bool shouldApplyThisSwing();
	void restoreEntityHit(JNIEnv* env);
};

extern Reach* reach;
