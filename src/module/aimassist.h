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
		bool verticalAim = true;
		bool hurtTimeBoost = true;
		bool multipoint = false;  // Aim at closest point on hitbox
		
		float horizontalSpeed = 45.0f;
		float verticalSpeed = 25.0f;
		float maxFov = 180.0f;
		float maxDistance = 4.5f;
		float randomization = 0.15f;
		float smoothing = 0.15f;  // Lower = faster snap, higher = smoother
		float hitboxY = 0.5f;     // 0.0 = feet, 0.5 = body, 1.0 = head
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
