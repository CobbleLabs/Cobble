#pragma once

#include "../module.h"

class Velocity : public Module
{
public:
	Velocity();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;

	// Mode enum
	enum class Mode {
		Regular = 0,  // Simple reduction
		Jump = 1,     // Jump after knockback
		Reduce = 2,   // Attack slowdown abuse
		Ignore = 3    // Cancel velocity completely
	};

	// Vertical mode for Ignore
	enum class VerticalMode {
		Never = 0,      // Always apply vertical velocity
		OnlyInAir = 1,  // Cancel only if hit mid-air
		Always = 2      // Always cancel vertical velocity
	};

	struct Settings {
		// General
		Mode mode = Mode::Regular;
		float horizontal = 100.0f;    // 0-100% (reduceTo)
		float vertical = 100.0f;      // 0-100%
		float chance = 100.0f;        // 0-100% chance to apply
		float fov = 360.0f;           // FOV filter (360 = all angles)
		int reduceDelay = 0;          // Ticks before reduction

		// Jump mode
		float jumpChance = 100.0f;    // Chance to jump

		// Reduce mode
		bool allowDoubleClicks = false;

		// Ignore mode
		VerticalMode verticalMode = VerticalMode::Never;

		// Conditions
		bool onlyLookingAtPlayer = false;
		bool onlyMousePressed = false;
		bool onlyMovingForward = false;
		bool onlyHoldingWeapon = false;
		bool onlySprinting = false;   // Jump mode only
	} settings;

private:
	int lastHurtTime = 0;
	int ticksSinceHit = 0;
	bool wasHitThisTick = false;

	// Helper methods
	bool checkChance(float chance);
	bool checkFOV();
	bool checkConditions();
	bool isLookingAtPlayer();
	bool isHoldingWeapon();
	float getAttackerAngle();
};

extern Velocity velocity;
