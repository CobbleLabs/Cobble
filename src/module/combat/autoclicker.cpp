#include "autoclicker.h"
#include "../../sdk/core/minecraft.h"
#include "../../util/logger.hpp"
#include "../../util/inventory/inventory_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/core/overlay.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <vector>

AutoClicker* autoClicker = nullptr;

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
	keybind = 0;  // No default keybind
	
	std::random_device rd;
	rng.seed(rd());
	
	regenerateDelayRange();
}

void AutoClicker::onEnable()
{
        auto now = std::chrono::steady_clock::now();

        // Stable mode - add 1ms margin to ensure display never exceeds target  
        stableIntervalMs = (1000.0 / static_cast<double>(settings.stableCPSValue));
        stableInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double, std::milli>(stableIntervalMs));
        nextStableClickTime = now;
	
	// Random mode
	lastClick = now;
	lastClickWasDown = false;
	wasBreakingBlock = false;
	clicksSinceRangeChange = 0;
	sessionStart = now;
	fatigueMultiplier = 1.0f;
	clicksSinceLastFatigueUpdate = 0;
	inBurst = false;
	burstClicksRemaining = 0;
	burstSpeedMultiplier = 1.0f;
	currentJitterMode = 0;
	jitterModeClicksRemaining = 0;
	
	if (!settings.stableCPS) {
		regenerateDelayRange();
		calculateNextDelay();
	}
}

void AutoClicker::onDisable()
{
	HWND hwnd = GetForegroundWindow();
	
	// Release any held block-breaking click
	if (wasBreakingBlock && hwnd) {
		PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
		wasBreakingBlock = false;
	}
	
	// Release click if we ended on a DOWN
	if (lastClickWasDown && hwnd) {
		PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
		lastClickWasDown = false;
	}
}

void AutoClicker::regenerateDelayRange()
{
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
	if (!enabled) return;
	
	// Don't click while in menu
	if (Menu::isOpen) return;

	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;
	
	// Only click when Minecraft window is focused (prevents issues with window resizing)
	if (hwnd != Overlay::gameWindow) return;
	
	// Not holding left mouse button - resync timeline to prevent burst on re-press
	if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
		if (settings.stableCPS) {
			nextStableClickTime = std::chrono::steady_clock::now();
		}
		return;
	}
	
	// Check GUI state
	bool inInventory = mc.isInventoryOpen();  // True only for GuiContainer (chest, inventory, etc.)
	bool inAnyGui = mc.isInGui();  // True for any GUI screen including pause menu
	
	// If in a GUI but not an inventory, always stop clicking
	if (inAnyGui && !inInventory) return;
	
	// If in inventory, check the workInInventory setting
	if (inInventory && !settings.workInInventory) return;
	
	// Weapon-only check (skip in inventory)
	if (settings.weaponOnly && !inInventory && !isHoldingWeapon()) {
		// Resync stable mode timeline to prevent burst when weapon is equipped
		if (settings.stableCPS) {
			nextStableClickTime = std::chrono::steady_clock::now();
		}
		return;
	}
	
	// Break blocks - edge detection state machine
	// Only transition on state CHANGE, not every frame
	bool isBreaking = settings.breakBlocks && isTargetingBlock();
	
	if (isBreaking && !wasBreakingBlock) {
		// ENTERING breaking state: release any pending synthetic click, then hold native DOWN
		if (lastClickWasDown) {
			PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
			lastClickWasDown = false;
		}
		// Send one DOWN to start breaking (Minecraft needs this)
		PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
		wasBreakingBlock = true;
		return;
	}
	
	if (isBreaking && wasBreakingBlock) {
		// HOLDING breaking state: do nothing, let the DOWN stay pressed
		return;
	}
	
	if (!isBreaking && wasBreakingBlock) {
		// EXITING breaking state: release the block-breaking click
		PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
		wasBreakingBlock = false;
		lastClickWasDown = false;
		// Reset timing so autoclicks resume immediately
		auto now = std::chrono::steady_clock::now();
		lastClick = now;
		nextEventTime = now;
	}
	
	// Attacking only - check if player is in crosshair (skip in inventory)
	if (settings.attackingOnly && !inInventory) {
		if (!isTargetingPlayer()) {
			return;
		}
	}
	
        // =======================
        // STABLE MODE - FIXED CPS
        // =======================
        if (settings.stableCPS) {
                auto now = std::chrono::steady_clock::now();
                if (stableInterval.count() <= 0) {
                        stableIntervalMs = (1000.0 / static_cast<double>(settings.stableCPSValue));
                        stableInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                std::chrono::duration<double, std::milli>(stableIntervalMs));
                }

                if (now < nextStableClickTime) return;

                // Click completo - SendMessage para timing exacto, compensando frames lentos
                static auto startTime = std::chrono::steady_clock::now();       
                static int clickCount = 0;
                do {
                        SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
                        SendMessage(hwnd, WM_LBUTTONUP, 0, 0);
                        clickCount++;
                        nextStableClickTime += stableInterval;
                } while (now >= nextStableClickTime);

                // Debug: log CPS every 5 seconds
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
                if (elapsed >= 5000) {
                        double avgCps = static_cast<double>(clickCount) * 1000.0 / static_cast<double>(elapsed);
                        log_debug("[AutoClicker] Stable: %.2f CPS (target: %.0f, interval: %.2fms)",
                                avgCps, settings.stableCPSValue, stableIntervalMs);
                        clickCount = 0;
                        startTime = now;
                }

                return;
        }
	
	// Random mode: relative delay
	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick);
	if (elapsed.count() < nextDelayMs) return;
	
	// Drop check - random chance to skip a click (random mode only)
	if (settings.advancedMode && settings.dropChance > 0.0f) {
		std::uniform_real_distribution<float> dropDist(0.0f, 100.0f);
		if (dropDist(rng) < settings.dropChance) {
			lastClick = now;
			calculateNextDelay();
			return;
		}
	}
	
	// Perform click (random mode)
	doClick();
	lastClick = now;
	lastAutoClick = now;
	
	// Update dynamic delay system (random mode only)
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
	
	// Stable mode no pasa por calculateNextDelay - usa su propio timeline
	if (settings.stableCPS) return;
	
	// Update humanization systems (only in randomized mode)
	updateFatigue();
	updateBurstMode();
	updateJitterMode();
	
	// Spike check
	bool isSpike = false;
	if (settings.advancedMode && settings.spikeChance > 0.0f) {
		std::uniform_real_distribution<float> spikeDist(0.0f, 100.0f);
		if (spikeDist(rng) < settings.spikeChance) {
			maxCps = (std::min)(maxCps * settings.spikeMultiplier, 25.0f);
			isSpike = true;
		}
	}
	
	// Generate random CPS within the min-max range
	std::uniform_real_distribution<float> cpsDist(minCps, maxCps);
	float targetCps = cpsDist(rng);
	
	// Convert CPS to delay (ms)
	float delay = 1000.0f / targetCps;
	
	// Add some variation from the dynamic delay range system
	std::uniform_real_distribution<float> variationDist(-0.1f, 0.1f);
	delay *= (1.0f + variationDist(rng));
	
	// Apply fatigue (session-based speed variation)
	delay *= fatigueMultiplier;
	
	// Apply burst mode (clusters of fast/slow clicking)
	if (inBurst) {
		delay *= burstSpeedMultiplier;
	}
	
	// Add non-uniform jitter based on current mode
	int jitterAmount = 0;
	switch (currentJitterMode) {
		case 0:  // No jitter
			jitterAmount = 0;
			break;
		case 1:  // Small jitter Â±2ms
			{
				std::uniform_int_distribution<int> jitterDist(-2, 2);
				jitterAmount = jitterDist(rng);
			}
			break;
		case 2:  // Medium jitter Â±5ms
			{
				std::uniform_int_distribution<int> jitterDist(-5, 5);
				jitterAmount = jitterDist(rng);
			}
			break;
		case 3:  // Large jitter Â±12ms
			{
				std::uniform_int_distribution<int> jitterDist(-12, 12);
				jitterAmount = jitterDist(rng);
			}
			break;
	}
	delay += jitterAmount;
	
	// Halve the delay because alternating pattern needs 2 calls per click
	delay *= 0.5f;
	
	// Clamp to sane values (these are per-event, not per-click)
	nextDelayMs = static_cast<int>(delay);
	nextDelayMs = (std::max)(nextDelayMs, 20);   // Min ~25 CPS
	nextDelayMs = (std::min)(nextDelayMs, 100);  // Max ~5 CPS
}

void AutoClicker::doClick()
{
	HWND hwnd = GetForegroundWindow();
	if (!hwnd) return;
	
	// Solo random mode usa doClick - stable mode hace click directo en onUpdate
	if (lastClickWasDown) {
		PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
		lastClickWasDown = false;
	} else {
		PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
		lastClickWasDown = true;
	}
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

void AutoClicker::updateFatigue()
{
	// Humans get tired over time - clicking naturally slows down
	// Then they "wake up" and speed up again
	
	clicksSinceLastFatigueUpdate++;
	
	// Update fatigue every 100-200 clicks
	if (clicksSinceLastFatigueUpdate < 100) return;
	
	std::uniform_int_distribution<int> shouldUpdateDist(0, 100);
	if (shouldUpdateDist(rng) > 50) return;
	
	clicksSinceLastFatigueUpdate = 0;
	
	// Session duration affects fatigue
	auto sessionDuration = std::chrono::duration_cast<std::chrono::seconds>(
		std::chrono::steady_clock::now() - sessionStart).count();
	
	// Base fatigue increases slowly over time (0.1% per minute)
	float baseFatigue = 1.0f + (sessionDuration / 600.0f) * 0.1f;
	
	// Add random variation (Â±5%)
	std::uniform_real_distribution<float> variationDist(-0.05f, 0.05f);
	fatigueMultiplier = baseFatigue + variationDist(rng);
	
	// Clamp to reasonable range (90% to 115% of normal speed)
	fatigueMultiplier = std::max(0.90f, std::min(1.15f, fatigueMultiplier));
}

void AutoClicker::updateBurstMode()
{
	// Humans click in bursts - periods of faster or slower clicking
	// Not every click, just occasional clusters
	
	if (burstClicksRemaining > 0) {
		burstClicksRemaining--;
		if (burstClicksRemaining == 0) {
			inBurst = false;
			burstSpeedMultiplier = 1.0f;
		}
		return;
	}
	
	// Small chance to enter burst mode each click
	std::uniform_int_distribution<int> burstChanceDist(0, 100);
	if (burstChanceDist(rng) > 8) return;  // 8% chance
	
	inBurst = true;
	
	// Burst length: 3-12 clicks
	std::uniform_int_distribution<int> lengthDist(3, 12);
	burstClicksRemaining = lengthDist(rng);
	
	// Burst speed: 70% chance faster, 30% chance slower
	std::uniform_int_distribution<int> typeDist(0, 100);
	if (typeDist(rng) < 70) {
		// Fast burst (0.85-0.95x delay = faster)
		std::uniform_real_distribution<float> fastDist(0.85f, 0.95f);
		burstSpeedMultiplier = fastDist(rng);
	} else {
		// Slow burst (1.05-1.20x delay = slower)
		std::uniform_real_distribution<float> slowDist(1.05f, 1.20f);
		burstSpeedMultiplier = slowDist(rng);
	}
}

void AutoClicker::updateJitterMode()
{
	// Non-uniform jitter: sometimes no jitter, sometimes more
	// Humans don't have consistent timing variance
	
	if (jitterModeClicksRemaining > 0) {
		jitterModeClicksRemaining--;
		return;
	}
	
	// Choose new jitter mode
	std::uniform_int_distribution<int> modeDist(0, 100);
	int roll = modeDist(rng);
	
	// Distribution:
	// 25% - no jitter (mode 0)
	// 40% - small jitter Â±2ms (mode 1)
	// 25% - medium jitter Â±5ms (mode 2)
	// 10% - large jitter Â±10ms (mode 3)
	
	if (roll < 25) {
		currentJitterMode = 0;
	} else if (roll < 65) {
		currentJitterMode = 1;
	} else if (roll < 90) {
		currentJitterMode = 2;
	} else {
		currentJitterMode = 3;
	}
	
	// How long to stay in this mode: 5-25 clicks
	std::uniform_int_distribution<int> lengthDist(5, 25);
	jitterModeClicksRemaining = lengthDist(rng);
}

void AutoClicker::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
		ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			
                        if (settings.stableCPS) {
                                // Stable mode: single slider for exact CPS (integer steps 1-25)
                                int cpsInt = static_cast<int>(settings.stableCPSValue);
                                if (ui::SliderInt("CPS", &cpsInt, 1, 25, "%d")) {
                                        settings.stableCPSValue = static_cast<float>(cpsInt);
                                        stableIntervalMs = (1000.0 / static_cast<double>(settings.stableCPSValue));
                                        stableInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                std::chrono::duration<double, std::milli>(stableIntervalMs));
                                        nextStableClickTime = std::chrono::steady_clock::now();
                                }
                        } else {
				// Random mode: min/max sliders
				float minLimit = 1.0f;
				float maxLimit = 20.0f;
				ui::SliderFloat("Min CPS", &settings.minCPS, 1.0f, 20.0f, &minLimit, &settings.maxCPS, "%.1f");
				ui::SliderFloat("Max CPS", &settings.maxCPS, 1.0f, 20.0f, &settings.minCPS, &maxLimit, "%.1f");
			}
		},
		"Conditions", [this]() {
			ui::Checkbox("Stable CPS", &settings.stableCPS);
			ui::Checkbox("Weapon Only", &settings.weaponOnly);
			ui::Checkbox("Break Blocks", &settings.breakBlocks);
			ui::Checkbox("Inventory", &settings.workInInventory);
		}
	);
}

std::string AutoClicker::getDisplayInfo() {
	return settings.stableCPS ? "Stable" : "Normal";
}

