#pragma once

#include "../module.h"
#include <cmath>
#include <chrono>
#include <random>

// Premium AimAssist with human-like behavior
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
		bool hurtTimeBoost = false;  // Legacy setting for config compatibility
		bool multipoint = false;  // Aim at closest point on hitbox
		
		float horizontalSpeed = 45.0f;
		float verticalSpeed = 25.0f;
		float maxFov = 360.0f;
		float maxDistance = 4.5f;
		float randomization = 0.15f;
		float smoothing = 0.15f;  // Lower = faster snap, higher = smoother
		float hitboxY = 0.5f;     // 0.0 = feet, 0.5 = body, 1.0 = head
	} settings;

private:
	static constexpr float PI = 3.14159265358979323846f;

	std::mt19937 rng;
	std::chrono::steady_clock::time_point lastUpdate;
	
	// Target lock state
	double lockedTargetX = 0.0;
	double lockedTargetZ = 0.0;
	bool hasLockedTarget = false;
	
	// Velocity state for smooth movement
	float currentYawVelocity = 0.0f;
	float currentPitchVelocity = 0.0f;
	
	// Last rotation for detecting user mouse movement
	float lastYaw = 0.0f;
	float lastPitch = 0.0f;
	
	// Temporal noise system (not per-frame)
	struct NoiseState {
		float yawNoise = 0.0f;
		float pitchNoise = 0.0f;
		std::chrono::steady_clock::time_point nextRefresh;
		std::chrono::steady_clock::time_point nextZero;
		bool inZeroPeriod = false;
	} noise;
	
	// Accumulated micro-error (human imperfection)
	float yawBias = 0.0f;
	float pitchBias = 0.0f;

	float getYawTo(double deltaX, double deltaZ);
	float getPitchTo(double deltaX, double deltaY, double deltaZ);
	float wrapAngle(float angle);
	double getHorizontalDistance(double x1, double z1, double x2, double z2);
	float randomFloat(float min, float max);
	
	// Update temporal noise state
	void updateNoiseState();
	
	// Non-linear speed curve (ease-in for natural acceleration)
	float applyCurve(float normalizedDiff);
	
	// Calculates smooth rotation using velocity-based spring-damper system
	// Acceleration state for velocity interpolation
	float smoothedYawAccel = 0.0f;
	float smoothedPitchAccel = 0.0f;

	// Calculates smooth rotation using velocity-based spring-damper system
	float calculateSmoothRotation(float angleDiff, float speed, float deltaTime, float& velocity, float& smoothedAccel);
};

extern AimAssist aimAssist;
