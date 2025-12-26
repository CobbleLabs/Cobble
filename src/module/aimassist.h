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
		
		// Speed 0-10 scale: 0=minimal, 5=balanced, 10=very fast
		float horizontalSpeed = 5.0f;
		float verticalSpeed = 3.0f;
		float maxFov = 60.0f;
		float maxDistance = 4.0f;
		float randomization = 0.15f;
		
		// Movement prediction (0-30%): subtle lead aim for smooth tracking
		float prediction = 15.0f; // 15% default - subtle but effective
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
	
	// Velocity tracking for prediction
	double lastTargetX = 0.0;
	double lastTargetZ = 0.0;
	double smoothVelocityX = 0.0;
	double smoothVelocityZ = 0.0;
	bool hasLastPosition = false;

	float getYawTo(double deltaX, double deltaZ);
	float getPitchTo(double deltaX, double deltaY, double deltaZ);
	float wrapAngle(float angle);
	double getHorizontalDistance(double x1, double z1, double x2, double z2);
	float randomFloat(float min, float max);
	
	// Calculates smooth rotation amount using MC sensitivity formula
	float calculateSmoothRotation(float angleDiff, float speed, float deltaMs, int hurtTime);
	
	// Check if player is already aiming at target's hitbox
	bool isAimingAtHitbox(double localX, double localY, double localZ,
						   float localYaw, float localPitch,
						   double targetX, double targetY, double targetZ);
	
	// Easing functions for smooth movement
	float easeOutQuad(float t);
	float easeOutCubic(float t);
	
	// Smooth velocity tracking
	void updateVelocity(double targetX, double targetZ, float deltaMs);
	void getPredictedPosition(double targetX, double targetZ, double& outX, double& outZ);
};

extern AimAssist aimAssist;

