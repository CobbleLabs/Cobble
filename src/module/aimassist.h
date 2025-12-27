#pragma once

#include "module.h"
#include <cmath>
#include <chrono>

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
		bool blatantMode = false;      // Aggressive mode: no smoothing, instant lock

		float horizontalSpeed = 5.0f;  // 1-10, controls correction strength
		float maxFov = 90.0f;           // Max angle to consider targets
		float maxDistance = 4.0f;       // Max distance to assist
	} settings;

private:
	static constexpr float PI = 3.14159265358979323846f;

	std::chrono::steady_clock::time_point lastUpdate;
	
	// Target lock state
	double lockedTargetX = 0.0;
	double lockedTargetZ = 0.0;
	bool hasLockedTarget = false;
	
	// Click gating
	float clickActivation = 0.0f;
		
	// Current "velocity" or momentum of the mouse movement
	double currentSensX = 0.0;
	double currentSensY = 0.0;
	
	// Prediction state
	double lastTargetX = 0.0;
	double lastTargetZ = 0.0;
	bool hasLastPosition = false;
	double smoothVelocityX = 0.0;
	double smoothVelocityZ = 0.0;
	
	// Warm-up timer: How long we've been tracking current target
	float trackingTimeMs = 0.0f;

	// Helper functions
	float getYawTo(double deltaX, double deltaZ);
	float getPitchTo(double deltaX, double deltaY, double deltaZ);
	float wrapAngle(float angle);
	double getHorizontalDistance(double x1, double z1, double x2, double z2);
	
	// Prediction helpers
	void updateVelocity(double targetX, double targetZ, float deltaMs);
	void getPredictedPosition(double targetX, double targetZ, double& outX, double& outZ);
	
	// The core physics tracker
	void smoothTrack(float desiredYaw, float desiredPitch, float deltaMs);
};

extern AimAssist aimAssist;
