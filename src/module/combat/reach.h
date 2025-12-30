#pragma once

#include "../module.h"

class Reach : public Module
{
public:
	Reach();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;

	struct Settings {
		float distance = 3.0f;        // Reach distance (vanilla is 3.0)
		float minDistance = 3.0f;     // Min reach for randomization
		float maxDistance = 3.5f;     // Max reach for randomization
		bool randomize = false;       // Use random reach within range
		bool weaponsOnly = true;      // Only apply when holding weapon
		bool sprintingOnly = false;   // Only apply while sprinting
		bool movingOnly = false;      // Only apply while moving forward
	} settings;

private:
	float currentReach = 3.0f;
	
	bool isHoldingWeapon();
	void updateReach();
};

extern Reach* reach;
