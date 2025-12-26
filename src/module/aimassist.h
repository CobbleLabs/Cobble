#pragma once

#include "module.h"
#include <cmath>
#include <chrono>
#include <random>

// Premium AimAssist with advanced smoothing techniques
// Based on professional ghost client implementations
class AimAssist : public Module
{
public:
	AimAssist();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;

	struct Settings {
		bool visibilityCheck = true;
		bool ignoreInvisible = true;
		bool onlySprinting = false;
		bool onlyClicking = true;
		bool targetLock = true;
		bool verticalAim = false;
		bool hurtTimeBoost = true;
		
		float horizontalSpeed = 25.0f;
		float verticalSpeed = 12.0f;
		float maxFov = 90.0f;
		float maxDistance = 4.5f;
		float randomization = 0.25f;
	} settings;

private:
	static constexpr float PI = 3.14159265358979323846f;
	static constexpr float MC_SENSITIVITY = 0.5f;

	std::mt19937 rng;
	std::chrono::steady_clock::time_point lastUpdate;
	
	// Target lock state
	double lockedTargetX = 0.0;
	double lockedTargetZ = 0.0;
	bool hasLockedTarget = false;

	float getYawTo(double deltaX, double deltaZ);
	float getPitchTo(double deltaX, double deltaY, double deltaZ);
	float wrapAngle(float angle);
	double getHorizontalDistance(double x1, double z1, double x2, double z2);
	float randomFloat(float min, float max);
	
	// Calculates smooth rotation amount using MC sensitivity formula
	float calculateSmoothRotation(float angleDiff, float speed, float deltaMs, int hurtTime);
};

extern AimAssist aimAssist;
