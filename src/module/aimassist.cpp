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
#include <algorithm>

AimAssist aimAssist;

AimAssist::AimAssist() : Module("AimAssist")
{
	keybind = 'V';
	lastUpdate = std::chrono::steady_clock::now();
}

void AimAssist::onEnable()
{
	log_ok("AimAssist enabled");
	hasLockedTarget = false;
	lastUpdate = std::chrono::steady_clock::now();
	
	// Reset physics
	currentSensX = 0.0;
	currentSensY = 0.0;
	clickActivation = 0.0f;
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
	hasLockedTarget = false;
	currentSensX = 0.0;
	currentSensY = 0.0;
	clickActivation = 0.0f;
}

// ============================================================
// MAIN UPDATE: Continuous Tracking Engine
// ============================================================
void AimAssist::onUpdate()
{
	if (!player.isValid()) return;
	if (mc.isInGui()) return;

	// Only when sprinting check
	if (settings.onlySprinting && !player.isSprinting()) return;

	auto now = std::chrono::steady_clock::now();
	float deltaMs = std::chrono::duration<float, std::milli>(now - lastUpdate).count();
	lastUpdate = now;
	
	// Clamp delta to prevent crazy jumps on lag spikes
	if (deltaMs > 50.0f) deltaMs = 50.0f;

	// --- Click Logic (Ramp up/down) ---
	bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	
	// Ramp speed: fast reaction
	float rampSpeed = 0.005f * deltaMs;
	
	if (settings.onlyClicking && !isClicking) {
		clickActivation = (std::max)(0.0f, clickActivation - rampSpeed * 3.0f); // Decay fast
	} else {
		clickActivation = (std::min)(1.0f, clickActivation + rampSpeed);
	}

	if (clickActivation <= 0.01f) {
		hasLockedTarget = false;
		// Reset inertia when not aiming
		currentSensX = 0.0;
		currentSensY = 0.0;
		return;
	}

// ============================================================
	// TARGET SELECTION
	// ============================================================
	auto players = mc.getPlayerList();
	if (players.empty()) {
		hasLockedTarget = false;
		hasLastPosition = false;
		currentSensX *= 0.85; // Friction slide
		currentSensY *= 0.85;
		return;
	}

	double localX = player.getPosX();
	double localY = player.getPosY() + 1.62;
	double localZ = player.getPosZ();
	float localYaw = player.getYaw();
	float localPitch = player.getPitch();

	float bestScore = -999999.0f;
	float desiredYaw = localYaw;
	float desiredPitch = localPitch;
	bool foundTarget = false;
	
	// Vars for prediction
	double targetPosX = 0.0;
	double targetPosZ = 0.0;

	for (auto& other : players) {
		if (!other.instance) continue;
		if (settings.ignoreInvisible && other.isInvisible) continue;

		double targetX = other.posX;
		double targetY = other.posY + 0.9;
		double targetZ = other.posZ;

		double distance = getHorizontalDistance(localX, localZ, targetX, targetZ);
		if (distance > settings.maxDistance || distance < 0.5) continue;

		float targetYaw = getYawTo(targetX - localX, targetZ - localZ);
		float yawDiff = wrapAngle(targetYaw - localYaw);
		if (std::abs(yawDiff) > settings.maxFov / 2.0f) continue;
		
		float score = -static_cast<float>(distance) * 2.0f - std::abs(yawDiff) * 0.5f;

		if (settings.targetLock && hasLockedTarget) {
			if (std::abs(targetX - lockedTargetX) < 1.0 && std::abs(targetZ - lockedTargetZ) < 1.0) {
				score += 10.0f;
			}
		}

		if (score > bestScore) {
			bestScore = score;
			desiredYaw = targetYaw;
			desiredPitch = getPitchTo(targetX - localX, targetY - localY, targetZ - localZ);
			
			lockedTargetX = targetX;
			lockedTargetZ = targetZ;
			targetPosX = targetX;
			targetPosZ = targetZ;
			foundTarget = true;
		}
	}
	
	// Cleanup
	for (auto& p : players) {
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}

	if (!foundTarget) {
		hasLockedTarget = false;
		hasLastPosition = false;
		currentSensX *= 0.85; // Friction slide
		currentSensY *= 0.85;
		return;
	}

	// Detect NEW target acquisition
	// Only reset if: 1) we weren't tracking before, AND 2) target is actually different
	bool wasTracking = hasLockedTarget;
	bool isSameTarget = wasTracking && 
		(std::abs(targetPosX - lockedTargetX) < 2.0) && 
		(std::abs(targetPosZ - lockedTargetZ) < 2.0);
	
	// Only reset state if it's a DIFFERENT target, not just re-clicking same one
	bool isNewTarget = !wasTracking || !isSameTarget;
	
	hasLockedTarget = true;
	lockedTargetX = targetPosX;
	lockedTargetZ = targetPosZ;
	
	// Reset state on NEW (different) target to prevent snap
	if (isNewTarget && !isSameTarget) {
		currentSensX = 0.0;
		currentSensY = 0.0;
		smoothVelocityX = 0.0;
		smoothVelocityZ = 0.0;
		trackingTimeMs = 0.0f;
		hasLastPosition = false;
	} else {
		// Same target or continuing - increment tracking time
		trackingTimeMs += deltaMs;
		// Cap trackingTimeMs to prevent overflow
		if (trackingTimeMs > 10000.0f) trackingTimeMs = 10000.0f;
	}
	
	// Update velocity for strafe prediction
	updateVelocity(targetPosX, targetPosZ, deltaMs);
	
	// Apply strafe prediction (WITH DISTANCE CLAMP)
	if (hasLastPosition) {
		// Ramp up prediction based on distance
		// Close range (< 2m): No prediction (avoids jitter)
		// Mid range (2m - 5m): Linear ramp
		// Long range (> 5m): Full prediction
		float distToTarget = (float)getHorizontalDistance(localX, localZ, targetPosX, targetPosZ);
		float predFactor = (std::max)(0.0f, (std::min)(1.0f, (distToTarget - 2.0f) / 3.0f));
		
		if (predFactor > 0.01f) {
			// REDUCED PREDICTION TIME: 40ms max (more stable for PvP)
			double predictionTime = 40.0 * predFactor;
			double predX = targetPosX + smoothVelocityX * predictionTime;
			double predZ = targetPosZ + smoothVelocityZ * predictionTime;
			
			// Interpolate between raw target and predicted target based on distance
			double finalTargetX = targetPosX + (predX - targetPosX) * predFactor;
			double finalTargetZ = targetPosZ + (predZ - targetPosZ) * predFactor;
			
			desiredYaw = getYawTo(finalTargetX - localX, finalTargetZ - localZ);
		}
	}

	// --- TRACKING CONTINUO ---
	smoothTrack(desiredYaw, desiredPitch, deltaMs);
}

// ============================================================
// SMOOTH TRACKING: Humanized with dynamic deadzone and soft falloff
// ============================================================
void AimAssist::smoothTrack(float desiredYaw, float desiredPitch, float deltaMs)
{
	float currentYaw = player.getYaw();
	float currentPitch = player.getPitch();

	// 1. Calculate raw error
	float yawDiff = wrapAngle(desiredYaw - currentYaw);
	float pitchDiff = desiredPitch - currentPitch;
	float absYawDiff = std::abs(yawDiff);
	float absPitchDiff = std::abs(pitchDiff);

	// 2. DYNAMIC DEADZONE based on hitbox angle at distance
	// Player hitbox is ~0.6m wide. At distance D, the angle subtended is:
	// angle = 2 * atan(0.3 / D) in degrees
	// At 4m: ~8.5°, at 3m: ~11.4°, at 2m: ~17°
	float distToTarget = (float)getHorizontalDistance(
		player.getPosX(), player.getPosZ(), lockedTargetX, lockedTargetZ);
	distToTarget = (std::max)(1.0f, distToTarget); // Prevent division issues
	
	// Calculate hitbox angle (half-width, so we use 0.3m)
	float hitboxHalfAngle = std::atan(0.3f / distToTarget) * (180.0f / PI);
	// Add some margin (player can be off-center and still hit)
	float deadzoneAngle = hitboxHalfAngle * 1.2f; // 120% of hitbox = comfortable zone
	deadzoneAngle = (std::max)(2.0f, (std::min)(deadzoneAngle, 12.0f)); // Clamp 2°-12°
	
	// 3. If already inside hitbox zone, DON'T CORRECT
	if (absYawDiff < deadzoneAngle) {
		// Smoothly decay any residual velocity
		currentSensX *= 0.7;
		currentSensY *= 0.7;
		
		// Debug: show we're in deadzone
		static float dzDebugTimer = 0.0f;
		dzDebugTimer += deltaMs;
		if (dzDebugTimer > 1000.0f) {
			dzDebugTimer = 0.0f;
			log_debug("AA: In hitbox deadzone, no correction needed");
		}
		return;
	}

	// 4. SOFT FALLOFF: Reduce strength as we approach the deadzone edge
	// This prevents the "snap to edge then stop" feeling
	// falloffZone = area between deadzone and full-strength zone
	float falloffStart = deadzoneAngle;
	float falloffEnd = deadzoneAngle + 15.0f; // Full strength at 15° past deadzone
	
	float falloffFactor = (absYawDiff - falloffStart) / (falloffEnd - falloffStart);
	falloffFactor = (std::max)(0.05f, (std::min)(1.0f, falloffFactor)); // Never fully zero
	
	// Apply a smooth curve (ease-in) so it ramps up gently
	falloffFactor = falloffFactor * falloffFactor; // Quadratic ease-in

	// 5. Calculate base correction strength
	float speedMult = settings.horizontalSpeed / 10.0f;
	
	// Warmup: start slow, ramp up over 400ms (slightly longer for smoothness)
	float warmupProgress = (std::min)(1.0f, trackingTimeMs / 400.0f);
	float warmupMult = 0.15f + 0.85f * warmupProgress; // 15% to 100%
	
	// Click activation
	float clickMult = clickActivation;
	
	// Combined strength with falloff applied
	// Base max is 0.35 (35% of error per frame at 60fps) - less aggressive than before
	float strength = speedMult * warmupMult * clickMult * falloffFactor * 0.35f;
	
	// 6. Frame-rate independent correction
	float timeFactor = deltaMs / 16.67f;
	
	// 7. Calculate corrections
	float correctionX = yawDiff * strength * timeFactor;
	float correctionY = pitchDiff * strength * timeFactor * 0.5f; // Pitch even slower
	
	// 8. MINIMUM CORRECTION THRESHOLD
	// If the correction is too small, don't apply it (prevents micro-jitter)
	float minCorrection = 0.05f * timeFactor;
	if (std::abs(correctionX) < minCorrection) {
		correctionX = 0.0f;
	}
	if (std::abs(correctionY) < minCorrection) {
		correctionY = 0.0f;
	}
	
	// Cap correction to reasonable values (max 2° per frame at 60fps - reduced from 3°)
	float maxCorrection = 2.0f * timeFactor;
	correctionX = (std::max)(-maxCorrection, (std::min)(correctionX, maxCorrection));
	correctionY = (std::max)(-maxCorrection, (std::min)(correctionY, maxCorrection));
	
	// 9. SMOOTHER BLENDING (50/50 instead of 30/70)
	// Higher blend with previous frame = smoother but slightly more lag
	currentSensX = currentSensX * 0.5 + correctionX * 0.5;
	currentSensY = currentSensY * 0.5 + correctionY * 0.5;
	
	// 10. Apply movement
	float newYaw = currentYaw + static_cast<float>(currentSensX);
	float newPitch = currentPitch;
	
	if (settings.verticalAim) {
		newPitch += static_cast<float>(currentSensY);
		newPitch = (std::max)(-90.0f, (std::min)(90.0f, newPitch));
	}

	player.setYaw(newYaw);
	if (settings.verticalAim) {
		player.setPitch(newPitch);
	}
	
	// Debug
	static float debugTimer = 0.0f;
	debugTimer += deltaMs;
	if (debugTimer > 500.0f) {
		debugTimer = 0.0f;
		std::ostringstream ss;
		ss << "AA: diff=" << absYawDiff << "° dz=" << deadzoneAngle << "° falloff=" << (int)(falloffFactor*100) << "% str=" << strength;
		log_debug(ss.str().c_str());
	}
}

// ============================================================
// HELPER FUNCTIONS
// ============================================================
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

void AimAssist::updateVelocity(double targetX, double targetZ, float deltaMs)
{
	if (!hasLastPosition) {
		lastTargetX = targetX;
		lastTargetZ = targetZ;
		hasLastPosition = true;
		return;
	}
	
	// Simple velocity calculation
	double rawVelX = (targetX - lastTargetX) / deltaMs;
	double rawVelZ = (targetZ - lastTargetZ) / deltaMs;
	
	// Smooth it (Very high smoothing because position updates are discrete/noisy)
	// Alpha 0.05 means we trust the history more than the instant change
	double alpha = 0.05;
	smoothVelocityX = alpha * rawVelX + (1.0 - alpha) * smoothVelocityX;
	smoothVelocityZ = alpha * rawVelZ + (1.0 - alpha) * smoothVelocityZ;
	
	// Clamp max velocity (sprinting is ~0.02 blocks/ms, fall/kb can be higher)
	double maxVel = 0.04;
	if (std::abs(smoothVelocityX) > maxVel) smoothVelocityX = (smoothVelocityX > 0 ? 1 : -1) * maxVel;
	if (std::abs(smoothVelocityZ) > maxVel) smoothVelocityZ = (smoothVelocityZ > 0 ? 1 : -1) * maxVel;
	
	// Zero out negligible velocity (prevents residual drift causing oscillation)
	if (std::abs(smoothVelocityX) < 0.001) smoothVelocityX = 0.0;
	if (std::abs(smoothVelocityZ) < 0.001) smoothVelocityZ = 0.0;
	
	lastTargetX = targetX;
	lastTargetZ = targetZ;
}

void AimAssist::getPredictedPosition(double targetX, double targetZ, double& outX, double& outZ)
{
	// Legacy helper, now we do simple prediction inline
	outX = targetX + smoothVelocityX * 150.0;
	outZ = targetZ + smoothVelocityZ * 150.0;
}
