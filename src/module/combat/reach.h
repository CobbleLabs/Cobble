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
		float distance = 3.1f;        // Reach distance (vanilla is 3.0)
		float minDistance = 3.0f;     // Min reach for randomization
		float maxDistance = 3.1f;     // Max reach for randomization
		bool randomize = false;       // Use random reach within range
		bool weaponsOnly = false;     // Only apply when holding weapon
		bool sprintingOnly = false;   // Only apply while sprinting
		bool movingOnly = false;      // Only apply while moving forward
		
		// Anti-detection
		int hitChance = 100;          // Chance (%) to apply reach (randomization)
		int maxConsecutiveHits = 3;   // Max consecutive extended hits
		bool onlyMovingTargets = true;// Only extend on targets moving toward us
	} settings;

private:
	float currentReach = 3.0f;
	int consecutiveExtendedHits = 0;
	ULONGLONG lastHitTime = 0;
	
	struct OriginalHitbox {
		double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
		jobject entityRef = nullptr;
		bool valid = false;
	} originalHitbox;
	
	bool isHoldingWeapon();
	void updateReach();
	bool shouldApplyThisSwing();
	void restoreHitbox(JNIEnv* env);
};

extern Reach* reach;
