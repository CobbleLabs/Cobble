#include "aimassist.h"
#include "../../sdk/minecraft.h"
#include "../../util/logger.hpp"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

AimAssist* aimAssist = nullptr;

// ========== Angle math utilities ==========

static inline float wrap180(float a) {
	a = std::fmod(a, 360.0f);
	if (a >= 180.0f) a -= 360.0f;
	if (a < -180.0f) a += 360.0f;
	return a;
}

static inline float angDelta(float from, float to) {
	return wrap180(to - from);
}

// Calculate MC's internal sensitivity multiplier for proper aim scaling
static inline float getSensitivityMultiplier() {
	float sens = mc.getMouseSensitivity();
	float f = sens * 0.6f + 0.2f;
	float f1 = f * f * f * 8.0f;
	return f1 * 0.15f;
}

// =================================================================

AimAssist::AimAssist() : Module("AimAssist")
{
	keybind = 0;  // No default keybind
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
	smoothedYawAccel = 0.0f;
	smoothedPitchAccel = 0.0f;
	
	auto now = std::chrono::steady_clock::now();
	noise.inZeroPeriod = false;
	noise.yawNoise = randomFloat(-0.6f, 0.6f);
	noise.pitchNoise = randomFloat(-0.4f, 0.4f);
	noise.nextZero = now + std::chrono::milliseconds(static_cast<int>(randomFloat(100.0f, 180.0f)));
	noise.nextRefresh = now;
	
	lastUpdate = now;
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
	hasLockedTarget = false;
	currentYawVelocity = 0.0f;
	currentPitchVelocity = 0.0f;
	smoothedYawAccel = 0.0f;
	smoothedPitchAccel = 0.0f;
}

void AimAssist::updateNoiseState()
{
	auto now = std::chrono::steady_clock::now();
	
	if (noise.inZeroPeriod) {
		if (now >= noise.nextRefresh) {
			noise.inZeroPeriod = false;
			noise.yawNoise = randomFloat(-0.8f, 0.8f) * randomFloat(0.5f, 1.1f);
			noise.pitchNoise = randomFloat(-0.6f, 0.6f) * randomFloat(0.4f, 0.95f);
			noise.nextZero = now + std::chrono::milliseconds(static_cast<int>(randomFloat(80.0f, 150.0f)));
		}
	} else {
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
	float clamped = (std::min)(std::abs(normalizedDiff), 1.0f);
	float curve = 1.0f - std::exp(-clamped * 3.0f);
	return (std::max)(curve, 0.15f);
}

void AimAssist::onUpdate()
{
	if (!player.isValid()) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;

	if (settings.onlySprinting && !player.isSprinting()) return;

	bool isClicking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	if (settings.onlyClicking && !isClicking) {
		hasLockedTarget = false;
		currentYawVelocity *= 0.8f;
		currentPitchVelocity *= 0.8f;
		return;
	}

	auto now = std::chrono::steady_clock::now();
	float deltaTime = std::chrono::duration<float>(now - lastUpdate).count();
	lastUpdate = now;
	
	deltaTime = (std::min)(deltaTime, 0.05f);
	if (deltaTime < 0.0001f) return;

	updateNoiseState();
	
	yawBias += randomFloat(-0.015f, 0.015f);
	pitchBias += randomFloat(-0.01f, 0.01f);
	yawBias = (std::max)(-0.25f, (std::min)(yawBias, 0.25f));
	pitchBias = (std::max)(-0.2f, (std::min)(pitchBias, 0.2f));

	float localYaw = player.getYaw();
	float localPitch = player.getPitch();
	
	float userYawDelta = wrap180(localYaw - lastYaw);
	float userPitchDelta = localPitch - lastPitch;
	lastYaw = localYaw;
	lastPitch = localPitch;
	
	// Intent detection - back off when user is flicking
	float intentFactor = 1.0f;
	constexpr float fastThresholdDegPerSec = 200.0f;
	
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

	auto players = mc.getPlayerList();
	if (players.empty()) {
		hasLockedTarget = false;
		return;
	}

	double localX = player.getPosX();
	double localY = player.getPosY() + 1.62;
	double localZ = player.getPosZ();

	double maxDistSq = settings.maxDistance * settings.maxDistance;
	constexpr double minDistSq = 0.25;

	float bestYawCenter = 0.0f;
	float bestPitchCenter = 0.0f;
	float bestYawDiff = 999.0f;
	bool foundTarget = false;

	for (auto& p : players) {
		if (settings.ignoreInvisible && p.isInvisible) continue;

		double dx = p.posX - localX;
		double dz = p.posZ - localZ;
		double distSq = dx * dx + dz * dz;
		
		if (distSq > maxDistSq || distSq < minDistSq) continue;
		
		double dist = std::sqrt(distSq);

		if (settings.visibilityCheck && !mc.canEntityBeSeen(p.instance)) continue;

		if (settings.targetLock && hasLockedTarget) {
			double lockDx = p.posX - lockedTargetX;
			double lockDz = p.posZ - lockedTargetZ;
			double lockDistSq = lockDx * lockDx + lockDz * lockDz;
			if (lockDistSq > 0.25) continue;
		}

		constexpr double HITBOX_WIDTH = 0.6;
		constexpr double HITBOX_HEIGHT = 1.8;
		constexpr double HALF_WIDTH = HITBOX_WIDTH / 2.0;
		
		// VELOCITY PREDICTION: Aim at where enemy WILL be
		// (DISABLED FOR TESTING - set to 0)
		constexpr double PREDICTION_TICKS = 0.0; // Set to 1.5 to enable
		double velX = (p.posX - p.lastPosX) * PREDICTION_TICKS;
		double velY = (p.posY - p.lastPosY) * PREDICTION_TICKS;
		double velZ = (p.posZ - p.lastPosZ) * PREDICTION_TICKS;
		
		// Filter out laggy/teleporting players (velocity too high = skip prediction)
		double velMagSq = velX * velX + velZ * velZ;
		if (velMagSq > 1.0) { // > 1 block/tick is suspicious
			velX = velY = velZ = 0.0;
		}
		
		double predictedX = p.posX + velX;
		double predictedY = p.posY + velY;
		double predictedZ = p.posZ + velZ;
		
		double boxMinY = predictedY;
		double boxMaxY = predictedY + HITBOX_HEIGHT;
		
		double targetX = predictedX;
		double targetZ = predictedZ;
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
			bestPitchCenter = targetPitch;
			foundTarget = true;
			
			lockedTargetX = p.posX;
			lockedTargetZ = p.posZ;
		}
	}

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
	
	// ================================================
	// NEW PARADIGM: AMPLIFY USER INPUT, DON'T CREATE IT
	// The assist should feel like "my hand moved better"
	// not "something moved my camera"
	// ================================================
	
	// 1. Raw angle to target (no bias/noise yet)
	float rawYawDiff = angDelta(localYaw, bestYawCenter);
	float rawPitchDiff = angDelta(localPitch, bestPitchCenter);
	float absYaw = std::abs(rawYawDiff);
	float absPitch = std::abs(rawPitchDiff);
	
	// 2. ALIGNMENT CHECK: Continuous, not binary
	// ratio = how much user is moving vs error angle
	float yawAlignment = 0.0f;
	float pitchAlignment = 0.0f;
	
	if (std::abs(userYawDelta) > 0.01f && absYaw > 0.1f) {
		float ratio = std::abs(userYawDelta) / (absYaw + 0.001f);
		ratio = (std::min)(ratio, 1.0f);
		yawAlignment = ((userYawDelta > 0) == (rawYawDiff > 0)) ? ratio : -ratio;
	}
	if (std::abs(userPitchDelta) > 0.01f && absPitch > 0.1f) {
		float ratio = std::abs(userPitchDelta) / (absPitch + 0.001f);
		ratio = (std::min)(ratio, 1.0f);
		pitchAlignment = ((userPitchDelta > 0) == (rawPitchDiff > 0)) ? ratio : -ratio;
	}
	
	// 3. INPUT ENERGY: How much is the user actually moving?
	float inputEnergy = (std::abs(userYawDelta) + std::abs(userPitchDelta)) / (deltaTime > 0.001f ? deltaTime : 0.016f);
	float energyFactor = (std::min)(inputEnergy / 150.0f, 1.0f);
	
	// 4. PROXIMITY STRENGTH: Smooth curve (calculate BEFORE noise)
	float yawProximity = (std::min)(absYaw / 3.0f, 1.0f);
	float pitchProximity = (std::min)(absPitch / 3.0f, 1.0f);
	
	// Soft magnet: smoothstep for natural feel
	yawProximity = yawProximity * yawProximity * (3.0f - 2.0f * yawProximity);
	pitchProximity = pitchProximity * pitchProximity * (3.0f - 2.0f * pitchProximity);
	
	// 5. DYNAMIC NOISE: Scales with input AND proximity
	// Near target = clean, tracking = human imperfection
	float noiseScale = energyFactor * yawProximity;
	float dynamicNoise = noiseScale * randomFloat(-0.08f, 0.08f);
	float yawDiff = rawYawDiff + dynamicNoise;
	float pitchDiff = rawPitchDiff + dynamicNoise * 0.6f;
	
	// 6. GOLDEN RULE: Assist NEVER initiates movement
	// If user isn't moving, strength drops to 10%
	float yawInitiator = (std::abs(userYawDelta) > 0.02f) ? 1.0f : 0.1f;
	float pitchInitiator = (std::abs(userPitchDelta) > 0.02f) ? 1.0f : 0.1f;
	
	// 7. ALIGNMENT BOOST: Continuous based on ratio
	// yawAlignment is now -1 to +1 continuously
	float yawAlignBoost = 1.0f + yawAlignment * 0.4f;
	float pitchAlignBoost = 1.0f + pitchAlignment * 0.4f;
	yawAlignBoost = (std::max)(0.0f, (std::min)(yawAlignBoost, 1.4f));
	pitchAlignBoost = (std::max)(0.0f, (std::min)(pitchAlignBoost, 1.4f));
	
	// 8. FINAL STRENGTH: Combine all factors
	float yawStrength = yawProximity * yawInitiator * yawAlignBoost * intentFactor;
	float pitchStrength = pitchProximity * pitchInitiator * pitchAlignBoost * intentFactor;
	
	// Scale by MC sensitivity (inverse - higher sens = lower multiplier for consistency)
	float sensMultiplier = getSensitivityMultiplier();
	float baseMultiplier = (sensMultiplier > 0.001f) ? (0.15f / sensMultiplier) : 1.0f;

	// Increased multiplier to be competitive: speed 100 = very fast (but still smooth)
	// 2.0x multiplier makes it comparable to other clients at max speed
	float effectiveYawSpeed = settings.horizontalSpeed * yawStrength * baseMultiplier * 2.0f;
	float effectivePitchSpeed = settings.verticalSpeed * pitchStrength * baseMultiplier * 2.0f;
	
	float yawRotation = calculateSmoothRotation(yawDiff, effectiveYawSpeed, deltaTime, currentYawVelocity, smoothedYawAccel);
	float pitchRotation = 0.0f;
	if (settings.verticalAim) {
		pitchRotation = calculateSmoothRotation(pitchDiff, effectivePitchSpeed, deltaTime, currentPitchVelocity, smoothedPitchAccel);
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

float AimAssist::calculateSmoothRotation(float angleDiff, float speed, float deltaTime, float& velocity, float& smoothedAccel)
{
	if (speed < 0.01f) {
		velocity *= 0.9f;
		return 0.0f;
	}
	
	float absAngle = std::abs(angleDiff);
	if (absAngle < 0.01f) {
		velocity *= 0.7f;
		smoothedAccel *= 0.7f;
		return 0.0f;
	}
	
	// Continuous stiffness curve (SmoothStep)
	constexpr float MAX_ANGLE = 5.0f;
	float t = (std::min)(absAngle / MAX_ANGLE, 1.0f);
	float smoothT = t * t * (3.0f - 2.0f * t);
	float stiffnessFactor = 0.2f + 0.8f * smoothT;
	
	float baseStiffness = speed * stiffnessFactor;
	
	// User smoothing
	float smoothMult = 1.0f - (settings.smoothing * 0.7f);
	baseStiffness *= smoothMult;
	
	// Dynamic damping based on proximity AND velocity
	// Less damping when close = less "sticky" feel
	float velocityMagnitude = std::abs(velocity);
	float velocityFactor = (std::min)(velocityMagnitude / (speed * 2.0f), 1.0f);
	
	// Proximity damping: 0.2 (close) -> 0.6 (far)
	float proximityDamping = 0.2f + (std::min)(absAngle / 2.0f, 1.0f) * 0.4f;
	
	float dampingMultiplier = 1.0f + (velocityFactor * proximityDamping);
	float baseDamping = std::sqrt(baseStiffness) * 1.8f * dampingMultiplier; // 1.8 instead of 2.0 for lighter feel
	
	// Sub-frame integration
	constexpr int subSteps = 8;
	float subDeltaTime = deltaTime / static_cast<float>(subSteps);
	float totalRotation = 0.0f;
	
	for (int i = 0; i < subSteps; ++i) {
		float springForce = angleDiff * baseStiffness;
		float dampingForce = -velocity * baseDamping;
		float targetAccel = springForce + dampingForce;
		
		constexpr float accelLerp = 0.4f;
		smoothedAccel = smoothedAccel + (targetAccel - smoothedAccel) * accelLerp;
		
		velocity += smoothedAccel * subDeltaTime;
		
		float maxVel = speed * 2.5f;
		velocity = (std::max)(-maxVel, (std::min)(velocity, maxVel));
		
		float subRotation = velocity * subDeltaTime;
		totalRotation += subRotation;
		
		angleDiff -= subRotation;
	}
	
	return totalRotation;
}

float AimAssist::getYawTo(double deltaX, double deltaZ)
{
	return static_cast<float>(std::atan2(deltaZ, deltaX)) * (180.0f / PI) - 90.0f;
}

float AimAssist::getPitchTo(double deltaX, double deltaY, double deltaZ)
{
	double horizontalDist = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
	
	if (horizontalDist < 0.001) {
		return (deltaY > 0) ? -90.0f : 90.0f;
	}
	
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

void AimAssist::onRenderMenu()
{
	ui::two_column(
		"Settings", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::SliderFloat("Speed", &settings.horizontalSpeed, 1.0f, 100.0f, "%.0f");
			ui::SliderFloat("FOV", &settings.maxFov, 10.0f, 180.0f, "%.0f");
			ui::SliderFloat("Distance", &settings.maxDistance, 3.0f, 8.0f, "%.1f");
		},
		"Filters", [this]() {
			ui::Checkbox("Clicking Only", &settings.onlyClicking);
			ui::Checkbox("Target Lock", &settings.targetLock);
			ui::Checkbox("Visibility", &settings.visibilityCheck);
			ui::Checkbox("Ignore Invis", &settings.ignoreInvisible);
		}
	);
}