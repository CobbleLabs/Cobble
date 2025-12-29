#include "aimassist.h"
#include "../../sdk/minecraft.h"
#include "../../util/logger.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AimAssist aimAssist;

// ========== Angle math utilities (circular domain safe) ==========

static inline float wrap180(float a) {
	a = std::fmod(a, 360.0f);
	if (a >= 180.0f) a -= 360.0f;
	if (a < -180.0f) a += 360.0f;
	return a;
}

// Returns minimal angular delta from 'from' to 'to' in [-180, 180)
// Returns minimal angular delta from 'from' to 'to' in [-180, 180)
static inline float angDelta(float from, float to) {
	return wrap180(to - from);
}

// SOFT DEADZONE: Returns angle to target CENTER (not edge)
// The caller will apply soft strength based on how far inside/outside the hitbox
static inline float angleToCenterCircular(float current, float center) {
	return angDelta(current, center);
}

// Calculate soft strength: 1.0 when far outside hitbox, fades to ~0 at center
// Uses smooth transition instead of binary cutoff
static inline float calculateSoftStrength(float angleToCenter, float hitboxHalf) {
	float absAngle = std::abs(angleToCenter);
	
	// Define transition zone: from hitbox edge inward
	// Full strength: outside hitbox (absAngle > hitboxHalf)
	// Fade zone: from edge to center
	// Near-zero: at center
	
	if (absAngle >= hitboxHalf * 1.5f) {
		// Well outside hitbox - full strength
		return 1.0f;
	} else if (absAngle <= hitboxHalf * 0.2f) {
		// Very close to center - minimal strength (let spring die naturally)
		return 0.03f;
	} else {
		// Soft transition zone - smooth fade
		float normalizedPos = (absAngle - hitboxHalf * 0.2f) / (hitboxHalf * 1.3f);
		// Smooth S-curve for natural feel
		float t = normalizedPos * normalizedPos * (3.0f - 2.0f * normalizedPos);
		return 0.03f + t * 0.97f;
	}
}

// =================================================================

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
	currentYawVelocity = 0.0f;
	currentPitchVelocity = 0.0f;
	lastYaw = player.isValid() ? player.getYaw() : 0.0f;
	lastPitch = player.isValid() ? player.getPitch() : 0.0f;
	yawBias = 0.0f;
	pitchBias = 0.0f;
	
	// Initialize noise state properly (start with active noise, not silence)
	auto now = std::chrono::steady_clock::now();
	noise.inZeroPeriod = false;
	noise.yawNoise = randomFloat(-0.6f, 0.6f);
	noise.pitchNoise = randomFloat(-0.4f, 0.4f);
	noise.nextZero = now + std::chrono::milliseconds(static_cast<int>(randomFloat(100.0f, 180.0f)));
	noise.nextRefresh = now;  // Not used until we enter zero period
	
	lastUpdate = now;
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
	hasLockedTarget = false;
	currentYawVelocity = 0.0f;
	currentPitchVelocity = 0.0f;
}

void AimAssist::updateNoiseState()
{
	auto now = std::chrono::steady_clock::now();
	
	if (noise.inZeroPeriod) {
		// In zero period - check if it's time to generate new noise
		if (now >= noise.nextRefresh) {
			noise.inZeroPeriod = false;
			noise.yawNoise = randomFloat(-0.8f, 0.8f) * randomFloat(0.5f, 1.1f);
			noise.pitchNoise = randomFloat(-0.6f, 0.6f) * randomFloat(0.4f, 0.95f);
			noise.nextZero = now + std::chrono::milliseconds(static_cast<int>(randomFloat(80.0f, 150.0f)));
		}
	} else {
		// Active noise period - check if it's time to go quiet
		if (now >= noise.nextZero) {
			noise.inZeroPeriod = true;
			noise.yawNoise = 0.0f;
			noise.pitchNoise = 0.0f;
			noise.nextRefresh = now + std::chrono::milliseconds(static_cast<int>(randomFloat(100.0f, 200.0f)));
		}
	}
}

float AimAssist::applyCurve(float normalizedDiff)
{
	// Non-linear ease-in curve: slow start, accelerates naturally
	// But with a minimum floor to prevent stalling near target
	float clamped = (std::min)(std::abs(normalizedDiff), 1.0f);
	float curve = 1.0f - std::exp(-clamped * 3.0f);
	
	// Minimum floor: never go below 15% speed
	return (std::max)(curve, 0.15f);
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
		currentYawVelocity *= 0.8f;
		currentPitchVelocity *= 0.8f;
		return;
	}

	// Calculate delta time since last update (in seconds)
	auto now = std::chrono::steady_clock::now();
	float deltaTime = std::chrono::duration<float>(now - lastUpdate).count();
	lastUpdate = now;
	
	// Clamp delta time to avoid huge jumps after lag spikes
	deltaTime = (std::min)(deltaTime, 0.05f);
	if (deltaTime < 0.0001f) return;

	// Update temporal noise
	updateNoiseState();
	
	// Update accumulated bias (micro-error drift)
	yawBias += randomFloat(-0.015f, 0.015f);
	pitchBias += randomFloat(-0.01f, 0.01f);
	yawBias = (std::max)(-0.25f, (std::min)(yawBias, 0.25f));
	pitchBias = (std::max)(-0.2f, (std::min)(pitchBias, 0.2f));

	// Get current rotation
	float localYaw = player.getYaw();
	float localPitch = player.getPitch();
	
	// Detect user mouse movement
	float userYawDelta = wrap180(localYaw - lastYaw);
	float userPitchDelta = localPitch - lastPitch;
	lastYaw = localYaw;
	lastPitch = localPitch;
	
	// Intent detection: use angular velocity (deg/sec) not deg/frame
	float intentFactor = 1.0f;
	constexpr float fastThresholdDegPerSec = 200.0f;  // Fast mouse threshold
	
	if (deltaTime > 0.0001f) {
		float yawRate = std::abs(userYawDelta) / deltaTime;
		float pitchRate = std::abs(userPitchDelta) / deltaTime;
		
		if (yawRate > fastThresholdDegPerSec) {
			intentFactor *= (std::max)(0.0f, 1.0f - (yawRate - fastThresholdDegPerSec) / 300.0f);
		}
		if (pitchRate > fastThresholdDegPerSec) {
			intentFactor *= (std::max)(0.0f, 1.0f - (pitchRate - fastThresholdDegPerSec) / 300.0f);
		}
	}

	// Get player list from world
	auto players = mc.getPlayerList();
	if (players.empty()) {
		hasLockedTarget = false;
		return;
	}

	double localX = player.getPosX();
	double localY = player.getPosY() + 1.62;
	double localZ = player.getPosZ();

	// Find best target
	float bestYawCenter = 0.0f, bestYawHalf = 0.0f;
	float bestPitchCenter = 0.0f, bestPitchHalf = 0.0f;
	float bestYawDiff = 999.0f;
	double bestDistance = 999.0;
	bool foundTarget = false;

	for (auto& p : players) {
		if (settings.ignoreInvisible && p.isInvisible) continue;

		double dist = getHorizontalDistance(localX, localZ, p.posX, p.posZ);
		if (dist > settings.maxDistance || dist < 0.5) continue;

		if (settings.visibilityCheck && !mc.canEntityBeSeen(p.instance)) continue;

		if (settings.targetLock && hasLockedTarget) {
			double lockDist = getHorizontalDistance(p.posX, p.posZ, lockedTargetX, lockedTargetZ);
			if (lockDist > 0.5) continue;
		}

		constexpr double HITBOX_WIDTH = 0.6;
		constexpr double HITBOX_HEIGHT = 1.8;
		constexpr double HALF_WIDTH = HITBOX_WIDTH / 2.0;
		
		double boxMinY = p.posY;
		double boxMaxY = p.posY + HITBOX_HEIGHT;
		
		float hitboxAngularWidth = static_cast<float>(std::atan(HALF_WIDTH / dist) * (180.0 / PI));
		float hitboxAngularHeight = static_cast<float>(std::atan((HITBOX_HEIGHT * 0.4) / dist) * (180.0 / PI));
		
		double targetX = p.posX;
		double targetZ = p.posZ;
		double targetY;
		
		if (settings.multipoint) {
			float yawRad = localYaw * (PI / 180.0f);
			float pitchRad = localPitch * (PI / 180.0f);
			double lookDirY = -std::sin(pitchRad);
			double horizontalLookDist = std::cos(pitchRad);
			
			if (horizontalLookDist > 0.001) {
				double projectedY = localY + (lookDirY / horizontalLookDist) * dist;
				targetY = (std::max)(boxMinY + 0.1, (std::min)(projectedY, boxMaxY - 0.1));
			} else {
				targetY = boxMinY + (HITBOX_HEIGHT * settings.hitboxY);
			}
		} else {
			targetY = boxMinY + (HITBOX_HEIGHT * settings.hitboxY);
		}

		float targetYaw = getYawTo(targetX - localX, targetZ - localZ);
		float targetPitch = getPitchTo(targetX - localX, targetY - localY, targetZ - localZ);
		
		float yawDiff = std::abs(angDelta(localYaw, targetYaw));

		if (yawDiff > settings.maxFov) continue;

		if (yawDiff < std::abs(bestYawDiff)) {
			bestYawDiff = angDelta(localYaw, targetYaw);
			bestYawCenter = targetYaw;
			bestYawHalf = hitboxAngularWidth;
			bestPitchCenter = targetPitch;
			bestPitchHalf = hitboxAngularHeight;
			bestDistance = dist;
			foundTarget = true;
			
			lockedTargetX = p.posX;
			lockedTargetZ = p.posZ;
		}
	}

	// Clean up player instances (defensive: check before delete)
	for (auto& p : players) {
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}

	if (!foundTarget) {
		hasLockedTarget = false;
		currentYawVelocity *= 0.85f;
		currentPitchVelocity *= 0.85f;
		return;
	}

	hasLockedTarget = true;
	
	// SOFT DEADZONE: Get angle to center (not to edge)
	float yawToCenter = angleToCenterCircular(localYaw, bestYawCenter);
	float pitchToCenter = angleToCenterCircular(localPitch, bestPitchCenter);
	
	// Calculate soft strength based on position relative to hitbox
	// This creates smooth transition instead of binary on/off
	float yawSoftStrength = calculateSoftStrength(yawToCenter, bestYawHalf);
	float pitchSoftStrength = calculateSoftStrength(pitchToCenter, bestPitchHalf);
	
	// Add accumulated bias (to target, not noise)
	float yawDiff = yawToCenter + yawBias;
	float pitchDiff = pitchToCenter + pitchBias;
	
	// Add temporal noise
	yawDiff += noise.yawNoise * 0.3f;
	pitchDiff += noise.pitchNoise * 0.3f;

	// Detect opposing movement
	float yawOpposition = 0.0f;
	float pitchOpposition = 0.0f;
	
	if (std::abs(userYawDelta) > 0.1f && std::abs(yawDiff) > 0.1f) {
		bool userMovingRight = userYawDelta > 0;
		bool targetIsRight = yawDiff > 0;
		if (userMovingRight != targetIsRight) {
			yawOpposition = (std::min)(std::abs(userYawDelta) / 2.0f, 1.0f);
		}
	}
	
	if (std::abs(userPitchDelta) > 0.1f && std::abs(pitchDiff) > 0.1f) {
		bool userMovingDown = userPitchDelta > 0;
		bool targetIsDown = pitchDiff > 0;
		if (userMovingDown != targetIsDown) {
			pitchOpposition = (std::min)(std::abs(userPitchDelta) / 2.0f, 1.0f);
		}
	}

	// Final strength = soft strength * opposition * intent
	// NO BINARY CUTS - everything is continuous
	float yawStrength = yawSoftStrength * (1.0f - yawOpposition) * intentFactor;
	float pitchStrength = pitchSoftStrength * (1.0f - pitchOpposition) * intentFactor;

	float yawRotation = 0.0f;
	float pitchRotation = 0.0f;
	
	// Always let spring work (no strength > 0.01 check that cuts force)
	// The soft strength already handles the fade to near-zero
	float effectiveYawSpeed = settings.horizontalSpeed * yawStrength;
	float effectivePitchSpeed = settings.verticalSpeed * pitchStrength;
	
	yawRotation = calculateSmoothRotation(yawDiff, effectiveYawSpeed, deltaTime, currentYawVelocity);
	if (settings.verticalAim) {
		pitchRotation = calculateSmoothRotation(pitchDiff, effectivePitchSpeed, deltaTime, currentPitchVelocity);
	}

	if (std::abs(yawRotation) > 0.001f) {
		float newYaw = localYaw + yawRotation;
		player.setYaw(newYaw);
		lastYaw = newYaw;
	}
	if (settings.verticalAim && std::abs(pitchRotation) > 0.001f) {
		float newPitch = localPitch + pitchRotation;
		newPitch = (std::max)(-90.0f, (std::min)(90.0f, newPitch));
		player.setPitch(newPitch);
		lastPitch = newPitch;
	}
}

float AimAssist::calculateSmoothRotation(float angleDiff, float speed, float deltaTime, float& velocity)
{
	float absAngle = std::abs(angleDiff);
	
	if (absAngle < 0.03f) {
		velocity *= 0.3f;
		return 0.0f;
	}
	
	// Base stiffness and damping
	float baseStiffness = speed * 1.5f;
	float baseDamping = std::sqrt(baseStiffness) * 2.3f;
	
	// TWO REGIMES: 
	// - Far from target: spring active (correcting)
	// - Near target: spring fades, damping dominates (settling)
	// Threshold: ~3 degrees = transition zone
	constexpr float settleThreshold = 3.5f;
	
	float stiffnessFactor;
	if (absAngle > settleThreshold) {
		// Correction regime: full spring
		stiffnessFactor = 1.0f;
	} else {
		// Settling regime: spring fades as we approach center
		// This lets the system coast on momentum + friction instead of "pushing slowly"
		float t = absAngle / settleThreshold;  // 0 at center, 1 at threshold
		stiffnessFactor = t * t;  // Quadratic fade for smooth transition
	}
	
	float stiffness = baseStiffness * stiffnessFactor;
	float damping = baseDamping;  // Damping stays constant - this is the "friction"
	
	float smoothMult = 1.0f - (settings.smoothing * 0.8f);
	stiffness *= smoothMult;
	
	float springForce = angleDiff * stiffness;
	float dampingForce = -velocity * damping;
	float acceleration = springForce + dampingForce;
	
	velocity += acceleration * deltaTime;
	
	float maxVelocity = speed * 2.5f;
	velocity = (std::max)(-maxVelocity, (std::min)(velocity, maxVelocity));
	
	float rotation = velocity * deltaTime;
	
	// Prevent overshoot
	if (std::abs(rotation) > absAngle) {
		rotation = angleDiff;
		velocity *= 0.3f;
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
	return wrap180(angle);
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
