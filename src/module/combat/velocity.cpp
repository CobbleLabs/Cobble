#include "velocity.h"
#include "../../sdk/minecraft.h"
#include "../../overlay/menu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <random>
#include <cmath>

Velocity* velocity = nullptr;

static std::mt19937 rng(std::random_device{}());

Velocity::Velocity() : Module("Velocity")
{
	keybind = 0;
}

void Velocity::onEnable()
{
	lastHurtTime = 0;
	ticksSinceHit = 0;
	wasHitThisTick = false;
}

void Velocity::onDisable()
{
}

bool Velocity::checkChance(float chance)
{
	if (chance >= 100.0f) return true;
	if (chance <= 0.0f) return false;
	std::uniform_real_distribution<float> dist(0.0f, 100.0f);
	return dist(rng) < chance;
}

bool Velocity::checkFOV()
{
	// 360 FOV means accept all angles
	if (settings.fov >= 360.0f) return true;
	
	// Get angle to attacker - simplified: check if we're facing the direction knockback came from
	// Since we don't have direct attacker info, we use motion direction as approximation
	double motionX = player.getMotionX();
	double motionZ = player.getMotionZ();
	
	if (motionX == 0 && motionZ == 0) return true;
	
	// Calculate knockback direction angle
	float kbAngle = (float)(atan2(-motionX, motionZ) * 180.0 / 3.14159265);
	float playerYaw = player.getYaw();
	
	// Normalize angles
	while (kbAngle < 0) kbAngle += 360;
	while (playerYaw < 0) playerYaw += 360;
	
	// Calculate angle difference (knockback from front = 180 degrees from motion)
	float angleDiff = abs(kbAngle - playerYaw);
	if (angleDiff > 180) angleDiff = 360 - angleDiff;
	
	// Check if within FOV (knockback from front = ~180 diff, from back = ~0 diff)
	// FOV 180 = only reduce from front (angleDiff > 90)
	float halfFov = settings.fov / 2.0f;
	return angleDiff >= (180.0f - halfFov);
}

bool Velocity::isLookingAtPlayer()
{
	return mc.isTargetingEntity();
}

bool Velocity::isHoldingWeapon()
{
	int itemId = player.getHeldItemId();
	// Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
	// Axes: 271, 275, 258, 279, 286
	return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 ||
			itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

bool Velocity::checkConditions()
{
	// Check all enabled conditions
	if (settings.onlyLookingAtPlayer && !isLookingAtPlayer()) return false;
	
	if (settings.onlyMousePressed) {
		if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return false;
	}
	
	if (settings.onlyMovingForward) {
		if (!(GetAsyncKeyState('W') & 0x8000)) return false;
	}
	
	if (settings.onlyHoldingWeapon && !isHoldingWeapon()) return false;
	
	if (settings.onlySprinting && !player.isSprinting()) return false;
	
	return true;
}

void Velocity::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	if (mc.isInGui()) return;
	if (!player.isValid()) return;

	int currentHurtTime = player.getHurtTime();
	
	// Detect hit (hurtTime goes from 0 to max on hit)
	bool justGotHit = currentHurtTime > 0 && currentHurtTime > lastHurtTime;
	
	if (justGotHit) {
		ticksSinceHit = 0;
		wasHitThisTick = true;
	} else if (wasHitThisTick) {
		ticksSinceHit++;
		if (currentHurtTime == 0) {
			wasHitThisTick = false;
		}
	}
	
	// Only process on the exact hit frame (+ delay)
	bool shouldProcess = justGotHit && (settings.reduceDelay == 0);
	if (!shouldProcess && wasHitThisTick && ticksSinceHit == settings.reduceDelay) {
		shouldProcess = true;
	}
	
	if (shouldProcess) {
		// Check FOV filter
		if (!checkFOV()) {
			lastHurtTime = currentHurtTime;
			return;
		}
		
		// Check conditions
		if (!checkConditions()) {
			lastHurtTime = currentHurtTime;
			return;
		}
		
		// Apply based on mode
		switch (settings.mode) {
			case Mode::Regular: {
				// Check chance
				if (!checkChance(settings.chance)) break;
				
				// Apply horizontal reduction
				if (settings.horizontal < 100.0f) {
					double motionX = player.getMotionX();
					double motionZ = player.getMotionZ();
					player.setMotionX(motionX * (settings.horizontal / 100.0));
					player.setMotionZ(motionZ * (settings.horizontal / 100.0));
				}
				
				// Apply vertical reduction
				if (settings.vertical < 100.0f) {
					double motionY = player.getMotionY();
					player.setMotionY(motionY * (settings.vertical / 100.0));
				}
				break;
			}
			
			case Mode::Jump: {
				// Check chance
				if (!checkChance(settings.jumpChance)) break;
				
				// Check sprint condition for jump mode
				if (settings.onlySprinting && !player.isSprinting()) break;
				
				// Jump immediately to negate knockback
				if (player.isOnGround()) {
					player.jump();
				}
				break;
			}
			
			case Mode::Reduce: {
				// Attack slowdown abuse - reduce motion by attacking
				if (!checkChance(settings.chance)) break;
				
				// Apply reduction similar to regular but can work on double clicks
				if (settings.horizontal < 100.0f) {
					double motionX = player.getMotionX();
					double motionZ = player.getMotionZ();
					player.setMotionX(motionX * (settings.horizontal / 100.0));
					player.setMotionZ(motionZ * (settings.horizontal / 100.0));
				}
				
				if (settings.vertical < 100.0f) {
					double motionY = player.getMotionY();
					player.setMotionY(motionY * (settings.vertical / 100.0));
				}
				break;
			}
			
			case Mode::Ignore: {
				// Cancel velocity completely without losing speed
				if (!checkChance(settings.chance)) break;
				
				// Horizontal: set to 0 (ignore knockback)
				player.setMotionX(0);
				player.setMotionZ(0);
				
				// Vertical based on setting
				switch (settings.verticalMode) {
					case VerticalMode::Never:
						// Don't modify vertical velocity
						break;
					case VerticalMode::OnlyInAir:
						if (!player.isOnGround()) {
							player.setMotionY(0);
						}
						break;
					case VerticalMode::Always:
						player.setMotionY(0);
						break;
				}
				break;
			}
		}
	}

	lastHurtTime = currentHurtTime;
}

float Velocity::getAttackerAngle()
{
	// This would require tracking the last attacker position
	// For now, return 0 (approximated in checkFOV using motion)
	return 0.0f;
}
