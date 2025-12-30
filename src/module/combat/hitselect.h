#pragma once

#include "../module.h"
#include "../../sdk/minecraft.h"
#include <chrono>
#include <random>

// Hit Select - Filters unnecessary clicks for better knockback in PvP
// Works alongside AutoClicker to cancel hits that won't deal damage
class HitSelect : public Module
{
public:
	HitSelect();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;

	// Called by AutoClicker to check if current click should be cancelled
	bool shouldCancelClick();
	
	// Show swing animation without actual attack (cosmetic)
	void doFakeSwing();

	struct Settings {
		// Mode: 0=Burst, 1=Criticals
		int mode = 0;
		
		// === Burst Mode Settings ===
		// Max duration (ms) to prevent clicks when target has invulnerability
		int pauseDuration = 450;  // ~9 ticks, safe default
		// Wait for server confirmation instead of prediction
		bool useServerAttackTime = false;
		
		// === Wait for First Hit ===
		// Don't attack until hit first (ms delay, 0=disabled)
		int waitForFirstHit = 0;
		
		// === Hit Later in Trades ===
		// Delay after getting hit before attacking back (ms, 0=disabled)
		int hitLaterDelay = 0;
		
		// === Criticals Mode Settings ===
		// Only try crits when not in knockback
		bool disableDuringKnockback = true;
		// Wait to get hit before trying crits
		bool onlyWhileDamaged = false;
		
		// === Visual ===
		// Show swing animation even when click is cancelled
		bool fakeSwing = true;
		
		// === Cancel Rates (0-100%) ===
		// Chance to cancel hits that won't deal damage (lower = more human)
		int cancelRateInCombat = 65;   // Safe default - not too aggressive
		// Chance to cancel swings at air (not targeting entity)
		int cancelRateMissedSwings = 30;  // Low - humans miss swings
		
		// === Humanization ===
		// Random variation in cancel timing (ms)
		int timingRandomization = 50;
		// Chance to "fail" a cancel (let bad hit through) for realism
		int humanErrorRate = 8;
	} settings;

private:
	std::mt19937 rng;
	
	// State tracking
	std::chrono::steady_clock::time_point lastTargetHit;
	std::chrono::steady_clock::time_point lastPlayerHit;
	std::chrono::steady_clock::time_point enableTime;
	
	int lastTargetHurtTime = 0;
	int lastPlayerHurtTime = 0;
	bool wasHitFirst = false;  // For "wait for first hit" feature
	bool inCombat = false;
	
	// Helpers
	bool shouldCancelBurst();
	bool shouldCancelCriticals();
	bool shouldCancelWaitForHit();
	bool passesChanceCheck(int percentage);
};

extern HitSelect hitSelect;
