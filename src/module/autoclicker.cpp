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

AutoClicker autoClicker;

// Weapon item IDs
namespace ItemIds {
	// Swords
	constexpr int WOOD_SWORD = 268;
	constexpr int STONE_SWORD = 272;
	constexpr int IRON_SWORD = 267;
	constexpr int DIAMOND_SWORD = 276;
	constexpr int GOLD_SWORD = 283;
	
	// Axes
	constexpr int WOOD_AXE = 271;
	constexpr int STONE_AXE = 275;
	constexpr int IRON_AXE = 258;
	constexpr int DIAMOND_AXE = 279;
	constexpr int GOLD_AXE = 286;
}

AutoClicker::AutoClicker() : Module("AutoClicker")
{
	keybind = 'R';
	
	std::random_device rd;
	rng.seed(rd());
	
	regenerateDelayRange();
}

void AutoClicker::onEnable()
{
	lastClick = std::chrono::steady_clock::now();
	holdingMouseDown = false;
	clicksSinceRangeChange = 0;
	regenerateDelayRange();
	calculateNextDelay();
}

void AutoClicker::onDisable()
{
	// Release click if holding
	if (holdingMouseDown) {
		releaseClick();
	}
}

void AutoClicker::regenerateDelayRange()
{
	// Dream-Advanced technique: randomize delay range periodically
	// This makes timing patterns less predictable
	std::uniform_real_distribution<float> minDist(15.0f, 40.0f);
	std::uniform_real_distribution<float> maxDist(60.0f, 85.0f);
	std::uniform_int_distribution<int> nextChangeDist(30, 70);
	
	currentDelayMin = minDist(rng);
	currentDelayMax = maxDist(rng);
	clicksUntilRangeChange = nextChangeDist(rng);
	clicksSinceRangeChange = 0;
}

void AutoClicker::onUpdate()
{
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;
	
	// Not holding left mouse button - release if we were holding
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
		if (holdingMouseDown) {
			releaseClick();
		}
		return;
	}
	
	// Right click blocks autoclicker (Dream-Advanced behavior)
	if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
		if (holdingMouseDown) {
			releaseClick();
		}
		return;
	}
	
	// Check GUI state
	bool inGui = mc.isInGui();
	if (inGui && !settings.workInInventory) {
		if (holdingMouseDown) releaseClick();
		return;
	}
	
	// Weapon-only check
	if (settings.weaponOnly && !inGui && !isHoldingWeapon()) {
		if (holdingMouseDown) releaseClick();
		return;
	}
	
	// Break blocks mode - hold click when targeting block
	if (settings.breakBlocks && isTargetingBlock()) {
		if (!holdingMouseDown) {
			PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
			holdingMouseDown = true;
		}
		return;
	}
	
	// Ignore blocks (if not breaking)
	if (settings.ignoreBlocks && isTargetingBlock()) {
		if (holdingMouseDown) releaseClick();
		return;
	}
	
	// Attacking only - check if player is in crosshair
	if (settings.attackingOnly && !inGui) {
		if (!isTargetingPlayer()) {
			if (holdingMouseDown) releaseClick();
			return;
		}
	}
	
	// Release hold if we switched from break blocks mode
	if (holdingMouseDown) {
		releaseClick();
	}
	
	// Check timing
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick);
	
	if (elapsed.count() < nextDelayMs) return;
	
	// Drop check - random chance to skip a click
	if (settings.advancedMode && settings.dropChance > 0.0f) {
		std::uniform_real_distribution<float> dropDist(0.0f, 100.0f);
		if (dropDist(rng) < settings.dropChance) {
			lastClick = now;
			calculateNextDelay();
			return;
		}
	}
	
	// Perform click
	doClick();
	lastClick = now;
	lastAutoClick = now;
	
	// Update dynamic delay system
	clicksSinceRangeChange++;
	if (clicksSinceRangeChange >= clicksUntilRangeChange) {
		regenerateDelayRange();
	}
	
	calculateNextDelay();
}

void AutoClicker::calculateNextDelay()
{
	float minCps = settings.minCPS;
	float maxCps = settings.maxCPS;
	
	if (minCps > maxCps) std::swap(minCps, maxCps);
	
	// Spike check
	bool isSpike = false;
	if (settings.advancedMode && settings.spikeChance > 0.0f) {
		std::uniform_real_distribution<float> spikeDist(0.0f, 100.0f);
		if (spikeDist(rng) < settings.spikeChance) {
			maxCps = (std::min)(maxCps * settings.spikeMultiplier, 25.0f);
			isSpike = true;
		}
	}
	
	// Dream-Advanced delay calculation:
	// Base delay from current randomized range, scaled by CPS
	std::uniform_real_distribution<float> baseDist(currentDelayMin, currentDelayMax);
	float baseDelay = baseDist(rng);
	
	// Scale by target CPS (higher CPS = lower delay)
	float targetCps = (minCps + maxCps) / 2.0f;
	float delay = baseDelay / (targetCps / 10.0f);
	
	// Apply kurtosis for clustering around mean
	if (settings.advancedMode && settings.kurtosis > 0.0f && !isSpike) {
		float meanDelay = (1000.0f / targetCps);
		float deviation = delay - meanDelay;
		float kurtosisEffect = 1.0f / (1.0f + settings.kurtosis * 0.5f);
		delay = meanDelay + (deviation * kurtosisEffect);
	}
	
	// Add micro-jitter (±3ms)
	std::uniform_int_distribution<int> jitterDist(-3, 3);
	delay += jitterDist(rng);
	
	// Clamp to sane values
	nextDelayMs = static_cast<int>(delay);
	nextDelayMs = (std::max)(nextDelayMs, 40);   // Min 25 CPS
	nextDelayMs = (std::min)(nextDelayMs, 200);  // Max 5 CPS
}

void AutoClicker::doClick()
{
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;

	// PostMessage is async and more natural than SendMessage
	PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
	PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
}

void AutoClicker::releaseClick()
{
	if (!holdingMouseDown) return;
	
	HWND hwnd = GetForegroundWindow();
	if (hwnd) {
		PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
	}
	holdingMouseDown = false;
}

bool AutoClicker::isHoldingWeapon()
{
	int id = getHeldItemId();
	if (id == -1) return false;
	
	// Swords
	if (id == ItemIds::WOOD_SWORD || id == ItemIds::STONE_SWORD ||
		id == ItemIds::IRON_SWORD || id == ItemIds::DIAMOND_SWORD ||
		id == ItemIds::GOLD_SWORD)
		return true;

	// Axes
	if (id == ItemIds::WOOD_AXE || id == ItemIds::STONE_AXE ||
		id == ItemIds::IRON_AXE || id == ItemIds::DIAMOND_AXE ||
		id == ItemIds::GOLD_AXE)
		return true;

	return false;
}

bool AutoClicker::isTargetingBlock()
{
	return mc.isTargetingBlock();
}

bool AutoClicker::isTargetingPlayer()
{
	// Check if the objectMouseOver is an entity (type 1 = block, type 2 = entity)
	// For now, we use a simpler check: if not targeting block and in combat range
	
	if (isTargetingBlock()) return false;
	
	// Get player list and check if any are in reach
	auto players = mc.getPlayerList();
	if (players.empty()) return false;
	
	float playerX = static_cast<float>(player.getPosX());
	float playerZ = static_cast<float>(player.getPosZ());
	
	for (const auto& p : players) {
		float dx = static_cast<float>(p.posX) - playerX;
		float dz = static_cast<float>(p.posZ) - playerZ;
		float distSq = dx * dx + dz * dz;
		
		// Within 4.5 blocks (reach + some buffer)
		if (distSq < 20.25f) { // 4.5^2
			return true;
		}
	}
	
	return false;
}

int AutoClicker::getHeldItemId()
{
	return player.getHeldItemId();
}
