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
	currentYawVelocity = 0.0f;
	currentPitchVelocity = 0.0f;
	lastYaw = 0.0f;
	lastPitch = 0.0f;
	lastUpdate = std::chrono::steady_clock::now();
}

void AimAssist::onDisable()
{
	log_ok("AimAssist disabled");
	hasLockedTarget = false;
	currentYawVelocity = 0.0f;
	currentPitchVelocity = 0.0f;
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
		// Decay velocity when not aiming
		currentYawVelocity *= 0.8f;
		currentPitchVelocity *= 0.8f;
		return;
	}

	// Calculate delta time since last update (in seconds)
	auto now = std::chrono::steady_clock::now();
	float deltaTime = std::chrono::duration<float>(now - lastUpdate).count();
	lastUpdate = now;
	
	// Clamp delta time to avoid huge jumps after lag spikes
	deltaTime = (std::min)(deltaTime, 0.05f);  // Max 50ms
	if (deltaTime < 0.0001f) return;  // Skip if deltaTime is basically zero

	// Get current rotation
	float localYaw = player.getYaw();
	float localPitch = player.getPitch();
	
	// Detect user mouse movement (difference from last frame not caused by us)
	float userYawDelta = wrapAngle(localYaw - lastYaw);
	float userPitchDelta = localPitch - lastPitch;
	lastYaw = localYaw;
	lastPitch = localPitch;

	// Get player list from world
	auto players = mc.getPlayerList();
	if (players.empty()) {
		hasLockedTarget = false;
		return;
	}

	double localX = player.getPosX();
	double localY = player.getPosY() + 1.62; // Eye height
	double localZ = player.getPosZ();

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

		// Player hitbox dimensions (Minecraft standard)
		constexpr double HITBOX_WIDTH = 0.6;   // Total width
		constexpr double HITBOX_HEIGHT = 1.8;  // Total height
		constexpr double HALF_WIDTH = HITBOX_WIDTH / 2.0;
		
		// Hitbox bounds
		double boxMinY = p.posY;
		double boxMaxY = p.posY + HITBOX_HEIGHT;
		
		double targetX = p.posX;
		double targetZ = p.posZ;
		double targetY;
		
		if (settings.multipoint) {
			// Multipoint: calculate closest point on hitbox to current look direction
			float yawRad = localYaw * (PI / 180.0f);
			float pitchRad = localPitch * (PI / 180.0f);
			double lookDirX = -std::sin(yawRad) * std::cos(pitchRad);
			double lookDirY = -std::sin(pitchRad);
			double lookDirZ = std::cos(yawRad) * std::cos(pitchRad);
			
			// Project look direction to target distance
			double horizontalLookDist = std::sqrt(lookDirX * lookDirX + lookDirZ * lookDirZ);
			if (horizontalLookDist > 0.001) {
				double projectedY = localY + (lookDirY / horizontalLookDist) * dist;
				// Clamp to hitbox Y bounds
				targetY = (std::max)(boxMinY + 0.1, (std::min)(projectedY, boxMaxY - 0.1));
			} else {
				// Looking straight up/down, aim at configured hitbox Y
				targetY = boxMinY + (HITBOX_HEIGHT * settings.hitboxY);
			}
		} else {
			// Fixed point: use configured hitboxY (0=feet, 0.5=body, 1=head)
			targetY = boxMinY + (HITBOX_HEIGHT * settings.hitboxY);
		}

		// Calculate angle differences
		float yawDiff = wrapAngle(getYawTo(targetX - localX, targetZ - localZ) - localYaw);
		float pitchDiff = getPitchTo(targetX - localX, targetY - localY, targetZ - localZ) - localPitch;

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
		// Decay velocity smoothly when no target
		currentYawVelocity *= 0.85f;
		currentPitchVelocity *= 0.85f;
		return;
	}

	hasLockedTarget = true;
	
	// Calculate dynamic dead zone based on hitbox angular size at current distance
	// At 4 blocks, hitbox width (0.6m) spans ~8.5 degrees
	// Formula: deadZone = atan(hitboxHalfWidth / distance) in degrees
	float hitboxAngularSize = static_cast<float>(std::atan(0.3 / bestDistance) * (180.0 / PI));
	float deadZone = hitboxAngularSize * 0.8f;  // 80% of hitbox = inside target
	deadZone = (std::max)(1.5f, (std::min)(deadZone, 8.0f));  // Clamp 1.5-8 degrees
	
	// Check if we're already on target (inside hitbox dead zone)
	bool onTargetYaw = std::abs(bestYawDiff) < deadZone;
	bool onTargetPitch = std::abs(bestPitchDiff) < deadZone;

	// Detect if user is moving AWAY from target (opposing movement)
	// If user moves in opposite direction to where target is, reduce assistance
	float yawOpposition = 0.0f;
	float pitchOpposition = 0.0f;
	
	if (std::abs(userYawDelta) > 0.1f) {
		// Check if user movement direction opposes target direction
		bool userMovingRight = userYawDelta > 0;
		bool targetIsRight = bestYawDiff > 0;
		if (userMovingRight != targetIsRight) {
			// User is intentionally moving away from target
			yawOpposition = (std::min)(std::abs(userYawDelta) / 2.0f, 1.0f);
		}
	}
	
	if (std::abs(userPitchDelta) > 0.1f) {
		bool userMovingDown = userPitchDelta > 0;
		bool targetIsDown = bestPitchDiff > 0;
		if (userMovingDown != targetIsDown) {
			pitchOpposition = (std::min)(std::abs(userPitchDelta) / 2.0f, 1.0f);
		}
	}

	// Calculate assistance strength based on opposition
	float yawStrength = 1.0f - yawOpposition;
	float pitchStrength = 1.0f - pitchOpposition;
	
	// If on target, don't assist (let user aim freely within hitbox)
	if (onTargetYaw) {
		yawStrength = 0.0f;
		currentYawVelocity *= 0.5f;  // Quickly decay velocity when on target
	}
	if (onTargetPitch) {
		pitchStrength = 0.0f;
		currentPitchVelocity *= 0.5f;
	}

	// Calculate smooth rotation using velocity-based system
	float yawRotation = 0.0f;
	float pitchRotation = 0.0f;
	
	if (yawStrength > 0.01f) {
		yawRotation = calculateSmoothRotation(bestYawDiff, settings.horizontalSpeed * yawStrength, deltaTime, currentYawVelocity);
	}
	if (settings.verticalAim && pitchStrength > 0.01f) {
		pitchRotation = calculateSmoothRotation(bestPitchDiff, settings.verticalSpeed * pitchStrength, deltaTime, currentPitchVelocity);
	}

	// Apply randomization (subtle noise to velocity, not rotation)
	if (settings.randomization > 0.0f && (yawStrength > 0.1f || pitchStrength > 0.1f)) {
		float noiseScale = settings.randomization * 0.3f;
		currentYawVelocity += randomFloat(-noiseScale, noiseScale);
		currentPitchVelocity += randomFloat(-noiseScale, noiseScale);
	}

	// Apply rotations if significant
	if (std::abs(yawRotation) > 0.001f) {
		float newYaw = localYaw + yawRotation;
		player.setYaw(newYaw);
		lastYaw = newYaw;  // Update lastYaw so we don't detect our own rotation as user input
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
	// Velocity-based smoothing with spring-damper physics
	// This produces smooth acceleration and deceleration
	
	float absAngle = std::abs(angleDiff);
	
	// Very small angle - stop
	if (absAngle < 0.05f) {
		velocity *= 0.3f;
		return 0.0f;
	}
	
	// Spring-damper constants
	// Higher speed = stiffer spring (faster response)
	// Damping ensures no oscillation
	float stiffness = speed * 1.5f;  // Spring constant
	float damping = std::sqrt(stiffness) * 2.2f;  // Slightly over-damped for smoothness
	
	// Smoothing factor adjusts response curve
	// Higher smoothing = slower acceleration
	float smoothMult = 1.0f - (settings.smoothing * 0.8f);  // 0.2 to 1.0
	stiffness *= smoothMult;
	
	// Spring force: pulls toward target
	float springForce = angleDiff * stiffness;
	
	// Damping force: resists velocity (prevents overshoot)
	float dampingForce = -velocity * damping;
	
	// Acceleration = net force (mass = 1)
	float acceleration = springForce + dampingForce;
	
	// Update velocity (integrate acceleration)
	velocity += acceleration * deltaTime;
	
	// Clamp max velocity based on speed setting
	float maxVelocity = speed * 2.5f;  // Degrees per second
	velocity = (std::max)(-maxVelocity, (std::min)(velocity, maxVelocity));
	
	// Calculate rotation for this frame
	float rotation = velocity * deltaTime;
	
	// Prevent overshoot: don't rotate past target
	if (std::abs(rotation) > absAngle) {
		rotation = angleDiff;
		velocity *= 0.3f;  // Reduce velocity when clamping
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
