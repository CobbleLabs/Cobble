#include "aimassist.h"
#include "../sdk/minecraft.h"
#include "../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AimAssist aimAssist;

AimAssist::AimAssist() : Module("AimAssist")
{
	keybind = 'V';
}

void AimAssist::onEnable()
{
	log_ok("AimAssist enabled");
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
}

void AimAssist::onUpdate()
{
	if (!player.isValid()) return;
	if (mc.isInGui()) return;

	// Only when sprinting check
	if (settings.onlySprinting && !player.isSprinting()) return;

	// Only when clicking check
	if (settings.onlyClicking && !(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;

	// Get player list from world
	auto players = mc.getPlayerList();
	if (players.empty()) return;

	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	float localYaw = player.getYaw();

	// Find closest valid target within FOV
	float closestAngleDiff = 999.0f;
	PlayerInfo* bestTarget = nullptr;

	for (auto& p : players) {
		// Skip invisible players if configured
		if (settings.ignoreInvisible && p.isInvisible) continue;

		// Distance check
		double dist = getDistance(localX, localY, localZ, p.posX, p.posY, p.posZ);
		if (dist > settings.maxDistance) continue;

		// Visibility check
		if (settings.visibilityCheck && !mc.canEntityBeSeen(p.instance)) continue;

		// Calculate angle to target
		float targetYaw = calculateYawTo(p.posX - localX, p.posZ - localZ);
		float angleDiff = wrapAngle(targetYaw - localYaw);

		// FOV check
		if (std::abs(angleDiff) > settings.maxFov) continue;

		// Track closest target by angle
		if (std::abs(angleDiff) < std::abs(closestAngleDiff)) {
			closestAngleDiff = angleDiff;
			bestTarget = &p;
		}
	}

	// Clean up player instances
	for (auto& p : players) {
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}

	if (!bestTarget) return;

	// Already aimed close enough
	if (std::abs(closestAngleDiff) < 1.0f) return;

	// Apply smooth rotation
	// MC sensitivity formula approximation
	float sensitivity = 0.5f * 0.6f + 0.2f;
	float mcSensitivity = sensitivity * sensitivity * sensitivity * 8.0f;

	float rotationAmount = closestAngleDiff * mcSensitivity * (settings.speed / 100.0f);

	// Clamp rotation to avoid snapping
	float maxRotation = settings.speed * 0.15f;
	if (std::abs(rotationAmount) > maxRotation) {
		rotationAmount = (rotationAmount > 0) ? maxRotation : -maxRotation;
	}

	float newYaw = localYaw + rotationAmount;
	player.setYaw(newYaw);
}

float AimAssist::calculateYawTo(double deltaX, double deltaZ)
{
	return static_cast<float>(std::atan2(deltaZ, deltaX)) * (180.0f / PI) - 90.0f;
}

float AimAssist::wrapAngle(float angle)
{
	angle = std::fmod(angle, 360.0f);
	if (angle >= 180.0f) angle -= 360.0f;
	if (angle < -180.0f) angle += 360.0f;
	return angle;
}

double AimAssist::getDistance(double x1, double y1, double z1, double x2, double y2, double z2)
{
	double dx = x2 - x1;
	double dy = y2 - y1;
	double dz = z2 - z1;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}
