#include "hitselect.h"
#include "../../util/logger.hpp"
#include "../../overlay/menu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

HitSelect hitSelect;

HitSelect::HitSelect() : Module("HitSelect")
{
	keybind = 'H';  // Default keybind
	
	std::random_device rd;
	rng.seed(rd());
}

void HitSelect::onEnable()
{
	enableTime = std::chrono::steady_clock::now();
	lastTargetHit = std::chrono::steady_clock::now();
	lastPlayerHit = std::chrono::steady_clock::now();
	lastTargetHurtTime = 0;
	lastPlayerHurtTime = 0;
	wasHitFirst = false;
	inCombat = false;
}

void HitSelect::onDisable()
{
	// Nothing to clean up
}

void HitSelect::onUpdate()
{
	if (!enabled) return;
	if (Menu::isOpen) return;
	
	// Track when target gets hit (hurtTime goes from 0 to 10)
	int targetHurtTime = mc.getTargetedEntityHurtTime();
	if (targetHurtTime > lastTargetHurtTime && lastTargetHurtTime == 0) {
		lastTargetHit = std::chrono::steady_clock::now();
	}
	lastTargetHurtTime = targetHurtTime;
	
	// Track when player gets hit
	int playerHurtTime = player.getHurtTime();
	if (playerHurtTime > lastPlayerHurtTime && lastPlayerHurtTime == 0) {
		lastPlayerHit = std::chrono::steady_clock::now();
		wasHitFirst = true;
		inCombat = true;
	}
	lastPlayerHurtTime = playerHurtTime;
	
	// Reset combat state if no hits for 3 seconds
	auto now = std::chrono::steady_clock::now();
	auto timeSinceHit = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayerHit).count();
	if (timeSinceHit > 3000) {
		inCombat = false;
	}
}

bool HitSelect::shouldCancelClick()
{
	if (!enabled) return false;
	if (Menu::isOpen) return false;
	
	// Human error - sometimes we "fail" and let a bad hit through
	// This makes the pattern less perfect and more human-like
	if (settings.humanErrorRate > 0 && passesChanceCheck(settings.humanErrorRate)) {
		return false;  // Intentional "mistake" - don't cancel
	}
	
	bool isTargeting = mc.isTargetingEntity();
	
	// Not targeting an entity - use missed swings cancel rate
	// Humans swing at air sometimes, don't cancel too aggressively
	if (!isTargeting) {
		if (settings.cancelRateMissedSwings > 0) {
			return passesChanceCheck(settings.cancelRateMissedSwings);
		}
		return false;
	}
	
	// Targeting an entity - check mode-specific logic
	bool shouldCancel = false;
	
	// Check wait for first hit
	if (shouldCancelWaitForHit()) {
		return true;  // Block all hits until hit first
	}
	
	// Check mode
	switch (settings.mode) {
		case 0:  // Burst mode
			shouldCancel = shouldCancelBurst();
			break;
		case 1:  // Criticals mode
			shouldCancel = shouldCancelCriticals();
			break;
	}
	
	// Apply cancel rate chance - not 100% perfect
	if (shouldCancel && settings.cancelRateInCombat < 100) {
		return passesChanceCheck(settings.cancelRateInCombat);
	}
	
	return shouldCancel;
}

bool HitSelect::shouldCancelBurst()
{
	// Burst mode: cancel clicks when target has damage invulnerability
	int targetHurtTime = mc.getTargetedEntityHurtTime();
	
	// Add timing randomization for human-like variance
	int randomOffset = 0;
	if (settings.timingRandomization > 0) {
		std::uniform_int_distribution<int> offsetDist(-settings.timingRandomization, settings.timingRandomization);
		randomOffset = offsetDist(rng);
	}
	
	if (settings.useServerAttackTime) {
		// Server mode: wait for server to confirm the hit
		// hurtTime > 0 means target is invulnerable
		if (targetHurtTime > 0) {
			// Check pause duration with randomization
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTargetHit).count();
			
			int effectivePause = settings.pauseDuration + randomOffset;
			if (effectivePause < 100) effectivePause = 100;  // Min safety
			
			if (elapsed < effectivePause) {
				return true;  // Still in pause window, cancel click
			}
		}
	} else {
		// Prediction mode: use local hurtTime tracking
		// Target takes 10 ticks (500ms) to become vulnerable again
		// But we add some tolerance - don't cancel at very end of i-frames
		// hurtTime counts DOWN from 10 to 0
		if (targetHurtTime > 1) {  // Allow hits when hurtTime is 1 (almost done)
			return true;
		}
	}
	
	// Hit later in trades: add delay after getting hit
	if (settings.hitLaterDelay > 0 && lastPlayerHurtTime > 0) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPlayerHit).count();
		
		int effectiveDelay = settings.hitLaterDelay + (randomOffset / 2);  // Less variance here
		if (effectiveDelay < 0) effectiveDelay = 0;
		
		if (elapsed < effectiveDelay) {
			return true;  // Wait before hitting back
		}
	}
	
	return false;
}

bool HitSelect::shouldCancelCriticals()
{
	// Criticals mode: only allow clicks while falling
	// Critical hits in Minecraft require: falling + not on ground
	
	double motionY = player.getMotionY();
	bool onGround = player.isOnGround();
	
	// If on ground, behave like burst mode (still cancel unnecessary hits)
	if (onGround) {
		int targetHurtTime = mc.getTargetedEntityHurtTime();
		// More lenient - only cancel if clearly in i-frames
		return targetHurtTime > 2;
	}
	
	// Disable during knockback if setting is on
	if (settings.disableDuringKnockback) {
		int playerHurtTime = player.getHurtTime();
		if (playerHurtTime > 0) {
			return false;  // Don't cancel during knockback - need to fight back
		}
	}
	
	// Only while damaged: wait until hit to try crits
	if (settings.onlyWhileDamaged && !wasHitFirst) {
		return false;  // Don't cancel until hit first
	}
	
	// Cancel if moving upward (wait for fall)
	// Small threshold to account for floating point and lag
	if (motionY > 0.05) {
		return true;  // Going up, cancel click to wait for crit
	}
	
	// At peak or falling - allow the hit
	// motionY <= 0.05 means we're at peak or falling
	return false;
}

bool HitSelect::shouldCancelWaitForHit()
{
	if (settings.waitForFirstHit == 0) return false;
	
	// Wait for first hit: don't attack until hit first
	if (!wasHitFirst) {
		auto now = std::chrono::steady_clock::now();
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - enableTime).count();
		
		// Allow attacks after timeout
		if (elapsed > settings.waitForFirstHit) {
			return false;
		}
		
		return true;  // Block attacks until hit or timeout
	}
	
	return false;
}

void HitSelect::doFakeSwing()
{
	// Show swing animation without actually clicking
	// This is done by calling Minecraft.thePlayer.swingItem()
	
	if (!player.isValid()) return;
	
	jclass entityCls = nullptr;
	if (!Java::AssignClass("net.minecraft.entity.EntityLivingBase", entityCls)) {
		return;
	}
	
	jmethodID mid_swingItem = Java::Env->GetMethodID(entityCls, "swingItem", "()V");
	if (mid_swingItem) {
		Java::Env->CallVoidMethod(player.instance, mid_swingItem);
	}
	
	Java::Env->DeleteLocalRef(entityCls);
}

bool HitSelect::passesChanceCheck(int percentage)
{
	if (percentage >= 100) return true;
	if (percentage <= 0) return false;
	
	std::uniform_int_distribution<int> dist(1, 100);
	return dist(rng) <= percentage;
}
