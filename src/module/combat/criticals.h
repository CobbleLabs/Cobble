#pragma once

#include "../module.h"

class Criticals : public Module
{
public:
	Criticals();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;

	struct Settings {
		int duration = 100;  // Duration in ms (10-300)
		bool mousePressed = false;
		bool weaponOnly = false;
		bool lookingAtPlayer = false;
		bool movingOnly = false;
	} settings;

private:
	int lastHurtTime = 0;
	unsigned long hitTimeMs = 0;  // When player was hit (ms)
	bool isExtendingAirTime = false;

	// Helper methods
	bool checkConditions();
	bool isHoldingWeapon();
};

extern Criticals* criticals;
