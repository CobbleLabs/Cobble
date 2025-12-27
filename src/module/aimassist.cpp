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
	std::random_device rd;
	rng.seed(rd());
	lastUpdate = std::chrono::steady_clock::now();
}

void AimAssist::onEnable()
{
	log_ok("AimAssist enabled");
	hasLockedTarget = false;
	lastUpdate = std::chrono::steady_clock::now();
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
	hasLockedTarget = false;
}

void AimAssist::onUpdate()
{
	if (!player.isValid()) return;
	if (mc.isInGui()) return;

	// Only when sprinting check
	if (settings.onlySprinting && !player.isSprinting()) return;

	// Only when clicking check
	bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (settings.onlyClicking && !isClicking) {
		hasLockedTarget = false;
		return;
	}

	// Calculate delta time since last update
	auto now = std::chrono::steady_clock::now();
	float deltaMs = std::chrono::duration<float, std::milli>(now - lastUpdate).count();
	
	// Limit update rate (1-10ms like Dream-Advanced)
	if (deltaMs < 1.0f) return;
	if (deltaMs > 10.0f) deltaMs = randomFloat(1.0f, 10.0f);
	
	lastUpdate = now;

	// Get player list from world
	auto players = mc.getPlayerList();
	if (players.empty()) {
		hasLockedTarget = false;
		return;
	}

	double localX = player.getPosX();
	double localY = player.getPosY() + 1.62; // Eye height
	double localZ = player.getPosZ();
	float localYaw = player.getYaw();
	float localPitch = player.getPitch();
	int hurtTime = player.getHurtTime();

	// Find best target
	float bestYawDiff = 999.0f;
	float bestPitchDiff = 999.0f;
	double bestDistance = 999.0;
	PlayerInfo* bestTarget = nullptr;

	for (auto& p : players) {
		// Skip invisible players if configured
		if (settings.ignoreInvisible && p.isInvisible) continue;

		// Horizontal distance for reach detection
		double dist = getHorizontalDistance(localX, localZ, p.posX, p.posZ);
		if (dist > settings.maxDistance || dist < 0.5) continue;

		// Visibility check
		if (settings.visibilityCheck && !mc.canEntityBeSeen(p.instance)) continue;

		// Target lock: prefer locked target if valid
		if (settings.targetLock && hasLockedTarget) {
			double lockDist = getHorizontalDistance(p.posX, p.posZ, lockedTargetX, lockedTargetZ);
			if (lockDist > 0.5) continue;
		}

		// Aim at body center (posY + 0.9 for body, +1.62 for head)
		double targetY = p.posY + 0.9;
		
		// Height adjustment: if we're above, aim at their head
		if (localY > p.posY + 1.8) {
			targetY = p.posY + 1.8;
		} else if (localY > p.posY) {
			targetY = localY;
		}

		// Calculate angle differences
		float yawDiff = wrapAngle(getYawTo(p.posX - localX, p.posZ - localZ) - localYaw);
		float pitchDiff = getPitchTo(p.posX - localX, targetY - localY, p.posZ - localZ) - localPitch;

		// FOV check (horizontal only)
		if (std::abs(yawDiff) > settings.maxFov) continue;

		// Prioritize by angle difference
		if (std::abs(yawDiff) < std::abs(bestYawDiff)) {
			bestYawDiff = yawDiff;
			bestPitchDiff = pitchDiff;
			bestDistance = dist;
			bestTarget = &p;
			
			// Update lock position
			lockedTargetX = p.posX;
			lockedTargetZ = p.posZ;
		}
	}

	// Clean up player instances
	for (auto& p : players) {
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}

	if (!bestTarget) {
		hasLockedTarget = false;
		return;
	}

	hasLockedTarget = true;

	// Dead zone
	if (std::abs(bestYawDiff) < 0.3f && std::abs(bestPitchDiff) < 0.3f) return;

	// Calculate smooth rotations using MC sensitivity formula
	float yawRotation = calculateSmoothRotation(bestYawDiff, settings.horizontalSpeed, deltaMs, hurtTime);
	float pitchRotation = settings.verticalAim ? 
		calculateSmoothRotation(bestPitchDiff, settings.verticalSpeed, deltaMs, hurtTime) : 0.0f;

	// Apply randomization for natural movement
	if (settings.randomization > 0.0f) {
		float randFactor = 1.0f + randomFloat(-settings.randomization, settings.randomization);
		yawRotation *= randFactor;
		pitchRotation *= randFactor;
	}

	// Clamp rotations to avoid over-rotation
	if (std::abs(yawRotation) > std::abs(bestYawDiff)) {
		yawRotation = bestYawDiff;
	}
	if (std::abs(pitchRotation) > std::abs(bestPitchDiff)) {
		pitchRotation = bestPitchDiff;
	}

	// Minimum rotation to prevent getting stuck
	if (std::abs(yawRotation) < 0.05f && std::abs(bestYawDiff) > 0.3f) {
		yawRotation = (bestYawDiff > 0) ? 0.05f : -0.05f;
	}

	// Apply rotations
	if (std::abs(yawRotation) > 0.01f) {
		player.setYaw(localYaw + yawRotation);
	}
	if (settings.verticalAim && std::abs(pitchRotation) > 0.01f) {
		float newPitch = localPitch + pitchRotation;
		newPitch = (std::max)(-90.0f, (std::min)(90.0f, newPitch));
		player.setPitch(newPitch);
	}
}

float AimAssist::calculateSmoothRotation(float angleDiff, float speed, float deltaMs, int hurtTime)
{
	// MC sensitivity formula: (sensitivity * 0.6 + 0.2)^3 * 8
	float sens = MC_SENSITIVITY * 0.6f + 0.2f;
	float mcFactor = sens * sens * sens * 8.0f;
	
	// Speed scaling based on angle (larger angles = faster rotation)
	float angleScale = std::abs(angleDiff) / 45.0f;
	angleScale = (std::max)(0.1f, (std::min)(angleScale, 2.0f));
	
	// Hurt time boost: rotate faster when being hit (like Dream-Advanced)
	float hurtBoost = 1.0f;
	if (settings.hurtTimeBoost && hurtTime > 5) {
		hurtBoost = 1.5f + (hurtTime / 20.0f);
		hurtBoost = (std::min)(hurtBoost, 2.5f);
	}
	
	// Calculate rotation amount
	float baseRotation = speed * 0.01f * mcFactor * angleScale * hurtBoost;
	
	// Time-based scaling (deltaMs factor)
	float timeScale = deltaMs * 0.03f;
	
	// Direction
	float direction = (angleDiff > 0) ? 1.0f : -1.0f;
	
	float rotation = baseRotation * timeScale * direction;
	
	// Ensure minimum rotation for responsiveness
	if (std::abs(rotation) < 0.01f) {
		rotation = 0.01f * direction;
	}
	
	return rotation;
}

float AimAssist::getYawTo(double deltaX, double deltaZ)
{
	return static_cast<float>(std::atan2(deltaZ, deltaX)) * (180.0f / PI) - 90.0f;
}

float AimAssist::getPitchTo(double deltaX, double deltaY, double deltaZ)
{
	double horizontalDist = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
	return static_cast<float>(-std::atan2(deltaY, horizontalDist)) * (180.0f / PI);
}

float AimAssist::wrapAngle(float angle)
{
	angle = std::fmod(angle, 360.0f);
	if (angle >= 180.0f) angle -= 360.0f;
	if (angle < -180.0f) angle += 360.0f;
	return angle;
}

double AimAssist::getHorizontalDistance(double x1, double z1, double x2, double z2)
{
	double dx = x2 - x1;
	double dz = z2 - z1;
	return std::sqrt(dx * dx + dz * dz);
}

float AimAssist::randomFloat(float min, float max)
{
	std::uniform_real_distribution<float> dist(min, max);
	return dist(rng);
}
