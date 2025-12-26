#include "aimassist.h"
#include "autoclicker.h"
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
	hasLastPosition = false;
	smoothVelocityX = 0.0;
	smoothVelocityZ = 0.0;
	yawVelocity = 0.0f;
	pitchVelocity = 0.0f;
	tickCounter = 0;
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
	
	// Check if yaw is within hitbox bounds (with large margin for comfort)
	if (yawDiff > hitboxAngularWidth + 5.0f) return false;
	
	// Check pitch - calculate angles to top and bottom of hitbox
	float pitchToBottom = getPitchTo(targetX - localX, targetY - localY, targetZ - localZ);
	float pitchToTop = getPitchTo(targetX - localX, (targetY + HITBOX_HEIGHT) - localY, targetZ - localZ);
	
	// Add large margin for smooth transition (don't be picky)
	float pitchMin = (std::min)(pitchToBottom, pitchToTop) - 8.0f;
	float pitchMax = (std::max)(pitchToBottom, pitchToTop) + 8.0f;
	
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

void AimAssist::updateVelocity(double targetX, double targetZ, float deltaMs)
{
	if (!hasLastPosition) {
		lastTargetX = targetX;
		lastTargetZ = targetZ;
		hasLastPosition = true;
		return;
	}
	
	// Calculate raw velocity (blocks per ms)
	double rawVelX = (targetX - lastTargetX) / deltaMs;
	double rawVelZ = (targetZ - lastTargetZ) / deltaMs;
	
	// Smooth velocity with exponential moving average (very smooth)
	// Lower alpha = smoother but more lag; higher = more responsive
	double alpha = 0.15; // Balanced smoothing
	smoothVelocityX = alpha * rawVelX + (1.0 - alpha) * smoothVelocityX;
	smoothVelocityZ = alpha * rawVelZ + (1.0 - alpha) * smoothVelocityZ;
	
	// Clamp velocity to reasonable values (max ~0.3 blocks/ms = sprinting speed)
	double maxVel = 0.02; // Per ms
	if (std::abs(smoothVelocityX) > maxVel) smoothVelocityX = (smoothVelocityX > 0 ? 1 : -1) * maxVel;
	if (std::abs(smoothVelocityZ) > maxVel) smoothVelocityZ = (smoothVelocityZ > 0 ? 1 : -1) * maxVel;
	
	lastTargetX = targetX;
	lastTargetZ = targetZ;
}

void AimAssist::getPredictedPosition(double targetX, double targetZ, double& outX, double& outZ)
{
	// Prediction amount 0-30 maps to 0-0.3 multiplier
	if (settings.prediction <= 0.0f || !hasLastPosition) {
		outX = targetX;
		outZ = targetZ;
		return;
	}
	
	// Convert prediction setting (0-30) to time ahead in ms
	float predictionMs = settings.prediction * 3.33f; // 0-30 -> 0-100ms
	
	// Apply prediction based on smoothed velocity
	outX = targetX + smoothVelocityX * predictionMs;
	outZ = targetZ + smoothVelocityZ * predictionMs;
	
	// HUMAN-LIKE: Prediction error (humans over/underestimate)
	float error = randomFloat(-0.15f, 0.15f);
	outX += error;
	outZ -= error * 0.7f;
}

void AimAssist::onUpdate()
{
	if (!player.isValid()) return;
	if (mc.isInGui()) return;

	// Only when sprinting check
	if (settings.onlySprinting && !player.isSprinting()) return;

	// CLICK-TRIGGERED PARADIGM: Only assist during/after clicks
	bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	auto now = std::chrono::steady_clock::now();
	
	// Detect AutoClicker NEW click (timestamp changed since we last checked)
	static std::chrono::steady_clock::time_point lastProcessedAutoClick;
	bool newAutoClick = (autoClicker.enabled && autoClicker.lastAutoClick != lastProcessedAutoClick);
	if (newAutoClick) {
		lastProcessedAutoClick = autoClicker.lastAutoClick;
	}
	
	// Detect new click (manual edge detection OR new autoclick)
	bool newManualClick = isClicking && !wasClicking;
	bool newClick = newManualClick || newAutoClick;
	
	if (newClick) {
		lastClickTime = now;
		currentWindowDuration = randomFloat(30.0f, 80.0f);
		assistAppliedThisClick = false; // Reset for new click
		log_debug("Click detected");
	}
	wasClicking = isClicking;
	
	// Calculate time since last click
	float msSinceClick = std::chrono::duration<float, std::milli>(now - lastClickTime).count();
	
	// Use fixed window duration (not recalculated)
	inAssistanceWindow = (msSinceClick < currentWindowDuration);
	
	// SINGLE PULSE: If already applied assist this click → stop
	if (assistAppliedThisClick) {
		yawVelocity *= 0.95f;
		pitchVelocity *= 0.95f;
		return;
	}
	
	// If not clicking AND outside assistance window → NO ASSIST
	if (!isClicking && !inAssistanceWindow) {
		yawVelocity *= 0.95f;
		pitchVelocity *= 0.95f;
		hasLockedTarget = false;
		return;
	}
	
	// Calculate delta time since last update
	float deltaMs = std::chrono::duration<float, std::milli>(now - lastUpdate).count();
	
	// Minimum delay with randomization
	float minDelay = randomFloat(1.0f, 3.0f);
	if (deltaMs < minDelay) return;
	
	// Cap deltaMs with jitter
	float maxDelay = randomFloat(8.0f, 12.0f);
	if (deltaMs > maxDelay) deltaMs = randomFloat(3.0f, 8.0f);
	
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
			
			// Update velocity tracking for prediction
			updateVelocity(p.posX, p.posZ, deltaMs);
			
			// Get predicted position for aiming
			double predictedX, predictedZ;
			getPredictedPosition(p.posX, p.posZ, predictedX, predictedZ);
			
			// Recalculate yaw to predicted position (smoother tracking)
			float predictedYawDiff = wrapAngle(getYawTo(predictedX - localX, predictedZ - localZ) - localYaw);
			
			// Blend between current and predicted (smooth transition)
			float predictionBlend = settings.prediction / 100.0f;
			bestYawDiff = yawDiff * (1.0f - predictionBlend) + predictedYawDiff * predictionBlend;
			
			// Update lock position to predicted
			lockedTargetX = predictedX;
			lockedTargetZ = predictedZ;
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
		// HUMAN-LIKE: Decay velocity when no target (don't snap stop)
		yawVelocity *= 0.9f;
		pitchVelocity *= 0.9f;
		return;
	}

	hasLockedTarget = true;
	tickCounter++;

	float absYawError = std::abs(bestYawDiff);
	float absPitchError = std::abs(bestPitchDiff);
	
	// Speed factor (0-10 scale): 0=very slow, 5=balanced, 10=fast
	float speedMult = 0.2f + (settings.horizontalSpeed / 10.0f) * 0.8f; // 0.2 to 1.0

	// ============================================================
	// HYBRID AIM SYSTEM:
	// Phase 1 (>7°): Smooth tracking - "going to target" (AimLab-like)
	// Phase 2 (3-7°): Transition - fading out
	// Phase 3 (<3°): Pulse only on clicks - final micro-adjustment
	// ============================================================

	if (absYawError > 7.0f) {
		// PHASE 1: SMOOTH TRACKING (continuous, flowing)
		float baseStrength = 0.006f * speedMult; // Reduced for balanced feel
		// Adjusted: starts from 3° so initial movement is more decisive
		float slowDown = (std::max)(0.0f, (std::min)((absYawError - 3.0f) / 10.0f, 1.0f));
		
		float yawDelta = bestYawDiff * baseStrength * slowDown * deltaMs;
		// Pitch follows less (yaw leads, pitch accompanies)
		float pitchDelta = settings.verticalAim ? bestPitchDiff * baseStrength * slowDown * deltaMs * 0.4f : 0.0f;
		
		// Clamp to human-like max
		yawDelta = (std::max)(-0.6f, (std::min)(yawDelta, 0.6f));
		pitchDelta = (std::max)(-0.4f, (std::min)(pitchDelta, 0.4f));
		
		// Randomization
		if (settings.randomization > 0.0f) {
			yawDelta *= 1.0f + randomFloat(-settings.randomization * 0.2f, settings.randomization * 0.2f);
		}
		
		player.setYaw(localYaw + yawDelta);
		if (settings.verticalAim && std::abs(pitchDelta) > 0.01f) {
			float newPitch = localPitch + pitchDelta;
			newPitch = (std::max)(-90.0f, (std::min)(90.0f, newPitch));
			player.setPitch(newPitch);
		}
		return;
	}
	
	if (absYawError > 3.0f) {
		// PHASE 2: TRANSITION (fading out - smooth deceleration)
		float t = (absYawError - 3.0f) / 4.0f; // 0 to 1
		float fade = t * t; // Ease-out curve
		
		float baseStrength = 0.004f * speedMult; // Reduced
		float yawDelta = bestYawDiff * baseStrength * fade * deltaMs;
		float pitchDelta = settings.verticalAim ? bestPitchDiff * baseStrength * fade * deltaMs * 0.5f : 0.0f;
		
		yawDelta = (std::max)(-0.35f, (std::min)(yawDelta, 0.35f));
		pitchDelta = (std::max)(-0.25f, (std::min)(pitchDelta, 0.25f));
		
		player.setYaw(localYaw + yawDelta);
		if (settings.verticalAim && std::abs(pitchDelta) > 0.01f) {
			float newPitch = localPitch + pitchDelta;
			newPitch = (std::max)(-90.0f, (std::min)(90.0f, newPitch));
			player.setPitch(newPitch);
		}
		return;
	}
	
	// PHASE 3: CLICK PULSE ONLY (< 3° error)
	// Only apply micro-correction on NEW clicks, not continuously
	if (!assistAppliedThisClick && inAssistanceWindow) {
		// Micro-correction: 5-12% of remaining error
		float correctionPercent = randomFloat(0.05f, 0.12f) * speedMult;
		float yawCorrection = bestYawDiff * correctionPercent;
		float pitchCorrection = settings.verticalAim ? bestPitchDiff * correctionPercent * 0.7f : 0.0f;
		
		// Hurt time boost
		if (settings.hurtTimeBoost && hurtTime > 15) {
			yawCorrection *= randomFloat(1.1f, 1.4f);
		}
		
		// Very small clamp
		yawCorrection = (std::max)(-0.5f, (std::min)(yawCorrection, 0.5f));
		
		if (std::abs(yawCorrection) > 0.02f) {
			player.setYaw(localYaw + yawCorrection);
			assistAppliedThisClick = true;
		}
		if (settings.verticalAim && std::abs(pitchCorrection) > 0.02f) {
			float newPitch = localPitch + pitchCorrection;
			newPitch = (std::max)(-90.0f, (std::min)(90.0f, newPitch));
			player.setPitch(newPitch);
			assistAppliedThisClick = true;
		}
	}
}

float AimAssist::calculateSmoothRotation(float angleDiff, float speed, float deltaMs, int hurtTime)
{
	// Dream-Advanced style calculation
	// MC sensitivity formula: (sensitivity * 0.6 + 0.2)^3 * 8
	float sens = MC_SENSITIVITY * 0.6f + 0.2f;
	float mcFactor = sens * sens * sens * 8.0f;
	
	float absAngle = std::abs(angleDiff);
	
	// Dream-Advanced: speed * (angle / 45) * random
	// Random range is wider when hurt
	float randomMin = 0.725f;
	float randomMax = (hurtTime > 15) ? 2.25f : 1.35f;
	float randomFactor = randomFloat(randomMin, randomMax);
	
	// Speed scaling: (angle / 45) gives 0-1 for most combat scenarios
	float angleScaling = absAngle / 45.0f;
	angleScaling = (std::min)(angleScaling, 1.0f);
	
	// Calculate aim speed (as int for discrete movements like Dream-Advanced)
	int aimSpeed = static_cast<int>(speed * angleScaling * randomFactor);
	if (aimSpeed < 1) aimSpeed = 1;
	
	// Final rotation using MC sensitivity formula
	// Dream-Advanced: sens^3 * 8 * 0.03 * deltaMs * direction * aimSpeed
	float direction = (angleDiff > 0) ? 1.0f : -1.0f;
	float rotation = mcFactor * 0.03f * deltaMs * direction * static_cast<float>(aimSpeed);
	
	// Clamp to not overshoot
	if (std::abs(rotation) > absAngle) {
		rotation = angleDiff * 0.8f;
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
