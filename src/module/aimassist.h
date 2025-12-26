#pragma once

#include "module.h"
#include <cmath>

// Smooth aim assistance towards nearby players
// Features:
// - Visibility check (only aim at visible players)
// - Invisible player filtering
// - Only when clicking/sprinting options
// - Configurable FOV, speed, and distance
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
		
		float speed = 15.0f;       // Rotation speed (higher = faster snap)
		float maxFov = 90.0f;      // Max angle to consider targets
		float maxDistance = 4.5f;  // Max distance to target
	} settings;

private:
	static constexpr float PI = 3.14159265358979323846f;

	// Calculate yaw angle to target position
	float calculateYawTo(double targetX, double targetZ);

	// Wrap angle to -180 to 180 range
	float wrapAngle(float angle);

	// Get distance between two 3D points
	double getDistance(double x1, double y1, double z1, double x2, double y2, double z2);
};

extern AimAssist aimAssist;
