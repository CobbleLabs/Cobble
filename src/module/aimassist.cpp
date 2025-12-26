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

bool AimAssist::isAimingAtHitbox(double localX, double localY, double localZ, 
								  float localYaw, float localPitch,
								  double targetX, double targetY, double targetZ)
{
	// Player hitbox: 0.6 width, 1.8 height
	constexpr float HITBOX_WIDTH = 0.6f;
	constexpr float HITBOX_HEIGHT = 1.8f;
	constexpr float HITBOX_HALF = HITBOX_WIDTH / 2.0f;
	
	// Calculate angles to hitbox corners
	float yawToCenter = getYawTo(targetX - localX, targetZ - localZ);
	float yawDiff = std::abs(wrapAngle(yawToCenter - localYaw));
	
	// Calculate horizontal distance
	double horizDist = getHorizontalDistance(localX, localZ, targetX, targetZ);
	if (horizDist < 0.1) return true; // Very close, consider on target
	
	// Calculate angular size of hitbox at this distance
	float hitboxAngularWidth = static_cast<float>(std::atan2(HITBOX_HALF, horizDist)) * (180.0f / PI);
	
	// Check if yaw is within hitbox bounds (with small margin)
	if (yawDiff > hitboxAngularWidth + 1.0f) return false;
	
	// Check pitch - calculate angles to top and bottom of hitbox
	float pitchToBottom = getPitchTo(targetX - localX, targetY - localY, targetZ - localZ);
	float pitchToTop = getPitchTo(targetX - localX, (targetY + HITBOX_HEIGHT) - localY, targetZ - localZ);
	
	// Add margin for smooth transition
	float pitchMin = (std::min)(pitchToBottom, pitchToTop) - 2.0f;
	float pitchMax = (std::max)(pitchToBottom, pitchToTop) + 2.0f;
	
	return (localPitch >= pitchMin && localPitch <= pitchMax);
}

float AimAssist::easeOutQuad(float t)
{
	// Quadratic ease-out: slower as approaching target
	return t * (2.0f - t);
}

float AimAssist::easeOutCubic(float t)
{
	// Cubic ease-out: even smoother
	float t1 = t - 1.0f;
	return t1 * t1 * t1 + 1.0f;
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
	
	// Smooth update rate (2-8ms for very smooth movement)
	if (deltaMs < 2.0f) return;
	if (deltaMs > 8.0f) deltaMs = randomFloat(2.0f, 8.0f);
	
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
	double bestTargetY = 0.0;

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
			bestTargetY = p.posY;
			
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

	// NATURAL AIMING: If already aiming at hitbox, don't force rotation
	if (isAimingAtHitbox(localX, localY, localZ, localYaw, localPitch,
						  lockedTargetX, bestTargetY, lockedTargetZ)) {
		return; // Already on target, no assist needed
	}

	// Very small dead zone (0.15°) for precision
	if (std::abs(bestYawDiff) < 0.15f && std::abs(bestPitchDiff) < 0.15f) return;

	// Calculate smooth rotations with easing
	float yawRotation = calculateSmoothRotation(bestYawDiff, settings.horizontalSpeed, deltaMs, hurtTime);
	float pitchRotation = settings.verticalAim ? 
		calculateSmoothRotation(bestPitchDiff, settings.verticalSpeed, deltaMs, hurtTime) : 0.0f;

	// Apply randomization for natural movement (very subtle)
	if (settings.randomization > 0.0f) {
		float randFactor = 1.0f + randomFloat(-settings.randomization * 0.5f, settings.randomization * 0.5f);
		yawRotation *= randFactor;
		pitchRotation *= randFactor;
	}

	// Soft clamp: slow down exponentially as approaching target
	float yawProgress = 1.0f - (std::abs(bestYawDiff) / 45.0f);
	yawProgress = (std::max)(0.0f, (std::min)(yawProgress, 1.0f));
	float easeFactor = 1.0f - (yawProgress * 0.3f); // Slow down near target
	yawRotation *= easeFactor;

	// Hard clamp to avoid over-rotation
	if (std::abs(yawRotation) > std::abs(bestYawDiff) * 0.8f) {
		yawRotation = bestYawDiff * 0.8f;
	}
	if (std::abs(pitchRotation) > std::abs(bestPitchDiff) * 0.8f) {
		pitchRotation = bestPitchDiff * 0.8f;
	}

	// Apply rotations (only if meaningful)
	if (std::abs(yawRotation) > 0.005f) {
		player.setYaw(localYaw + yawRotation);
	}
	if (settings.verticalAim && std::abs(pitchRotation) > 0.005f) {
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
	
	// Speed now 0-10 scale. Map to effective multiplier.
	// 0 = minimal assist, 5 = balanced, 10 = fast but smooth
	float speedMultiplier = 0.1f + (speed / 10.0f) * 0.9f; // 0.1 to 1.0
	
	// Exponential decay scaling: smoother as angle decreases
	float absAngle = std::abs(angleDiff);
	float angleScale;
	
	if (absAngle < 3.0f) {
		// Very close: smooth slowdown
		angleScale = 0.3f + (absAngle / 3.0f) * 0.2f;
	} else if (absAngle < 10.0f) {
		// Medium range: good progression
		angleScale = 0.5f + (absAngle - 3.0f) / 7.0f * 0.3f;
	} else if (absAngle < 30.0f) {
		// Larger angles: faster catch up
		angleScale = 0.8f + (absAngle - 10.0f) / 20.0f * 0.2f;
	} else {
		// Very large angles: full speed
		angleScale = 1.0f;
	}
	
	// Hurt time boost: subtle increase when being hit
	float hurtBoost = 1.0f;
	if (settings.hurtTimeBoost && hurtTime > 5) {
		hurtBoost = 1.15f + (hurtTime / 40.0f);
		hurtBoost = (std::min)(hurtBoost, 1.5f);
	}
	
	// Calculate base rotation - HIGHER multiplier for responsive feel
	// speed 5 at 5ms deltaMs should give ~0.3-0.5 deg rotation for medium angles
	float baseRotation = speedMultiplier * 0.15f * mcFactor * angleScale * hurtBoost;
	
	// Time-based scaling
	float timeScale = deltaMs * 0.1f;
	
	// Direction
	float direction = (angleDiff > 0) ? 1.0f : -1.0f;
	
	float rotation = baseRotation * timeScale * direction;
	
	// Micro-threshold for stuck prevention
	if (std::abs(rotation) < 0.01f && absAngle > 1.0f) {
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
