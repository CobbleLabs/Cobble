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
	keybind = 'R'; // TODO: Make configurable
	
	std::random_device rd;
	rng.seed(rd());
}

void AutoClicker::onEnable()
{
	log_ok("AutoClicker enabled");
	lastClick = std::chrono::steady_clock::now();
	calculateNextDelay();
}

void AutoClicker::onDisable()
{
	log_ok("AutoClicker disabled");
}

void AutoClicker::onUpdate()
{
	// Skip if not holding left mouse button
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;

	// Check GUI state
	bool inGui = mc.isInGui();
	if (inGui && !settings.workInInventory) return;

	// Weapon-only check
	if (settings.weaponOnly && !isHoldingWeapon()) return;

	// Ignore blocks check
	if (settings.ignoreBlocks && isTargetingBlock()) return;

	// Check if enough time has passed
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick);

	if (elapsed.count() < nextDelayMs) return;

	// Drop check - random chance to skip a click
	if (settings.advancedMode && settings.dropChance > 0.0f)
	{
		std::uniform_real_distribution<float> dropDist(0.0f, 100.0f);
		if (dropDist(rng) < settings.dropChance)
		{
			lastClick = now;
			calculateNextDelay();
			return; // Skip this click
		}
	}

	// Perform click
	doClick();
	lastClick = now;
	lastAutoClick = now; // Notify AimAssist
	calculateNextDelay();
}

void AutoClicker::calculateNextDelay()
{
	float minCps = settings.minCPS;
	float maxCps = settings.maxCPS;

	// Spike check - random chance for faster clicks
	if (settings.advancedMode && settings.spikeChance > 0.0f)
	{
		std::uniform_real_distribution<float> spikeDist(0.0f, 100.0f);
		if (spikeDist(rng) < settings.spikeChance)
		{
			maxCps = (std::min)(maxCps * settings.spikeMultiplier, 25.0f);
		}
	}

	float cps;

	if (settings.advancedMode && settings.kurtosis > 0.0f)
	{
		// Use normal distribution with kurtosis for more realistic patterns
		float meanCps = (minCps + maxCps) / 2.0f;
		float stdDev = (maxCps - minCps) / (4.0f + settings.kurtosis * 2.0f);
		
		std::normal_distribution<float> normalDist(meanCps, stdDev);
		cps = normalDist(rng);
		
		// Clamp to valid range
		cps = (std::max)(minCps, (std::min)(maxCps, cps));
	}
	else
	{
		// Simple uniform distribution
		std::uniform_real_distribution<float> dist(minCps, maxCps);
		cps = dist(rng);
	}

	// Convert CPS to delay in milliseconds
	nextDelayMs = static_cast<int>(1000.0f / cps);
	
	// ANTI-DETECTION: Add micro-jitter (±5ms random variation)
	std::uniform_int_distribution<int> jitterDist(-5, 5);
	nextDelayMs += jitterDist(rng);
	nextDelayMs = (std::max)(nextDelayMs, 40); // Minimum 40ms (25 CPS max)
}

void AutoClicker::doClick()
{
	// Get the Minecraft window
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;

	// Get cursor position
	POINT cursor;
	GetCursorPos(&cursor);
	ScreenToClient(hwnd, &cursor);

	LPARAM lParam = MAKELPARAM(cursor.x, cursor.y);

	// Send mouse down and up messages
	// This registers on CPS counters unlike JNI clickMouse()
	SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lParam);
	SendMessage(hwnd, WM_LBUTTONUP, 0, lParam);
}

bool AutoClicker::isHoldingWeapon()
{
	int id = getHeldItemId();
	if (id == -1) return false; // Empty hand - not a weapon
	
	// Check swords
	if (id == ItemIds::WOOD_SWORD || id == ItemIds::STONE_SWORD ||
		id == ItemIds::IRON_SWORD || id == ItemIds::DIAMOND_SWORD ||
		id == ItemIds::GOLD_SWORD)
		return true;

	// Check axes
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

int AutoClicker::getHeldItemId()
{
	return player.getHeldItemId();
}
