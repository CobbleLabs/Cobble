#pragma once

#include "../module.h"
#include "../../sdk/minecraft.h"
#include <chrono>
#include <vector>

// Item IDs for refillable items
namespace RefillItems {
	constexpr int MUSHROOM_SOUP = 282;
	constexpr int POTION = 373;
}

// AutoRefill - One-shot action module
// Press keybind -> opens inventory -> refills -> closes
// Not a persistent toggle like other modules
class AutoRefill : public Module
{
public:
	AutoRefill();

	void onEnable() override;  // Called when keybind pressed - starts refill
	void onDisable() override;
	void onUpdate() override;

	struct Settings {
		// Items to refill
		bool refillSoup = true;
		bool refillPotions = true;
		bool useNonHealthPotions = false;  // If false, only refill Instant Health
		
		// Simulate button for anti-cheat bypass
		// 0=left, 1=right
		int simulateButton = 0;
		
		// Timing (ms)
		int delayAfterOpen = 100;    // Delay after opening inventory
		int delayBeforeClose = 100;  // Delay before closing inventory
		int speed = 80;              // Delay between item moves
		
		bool smartSpeed = true;      // Add extra delay when slots are far apart
	} settings;

private:
	enum class State {
		Idle,      // Waiting for keybind
		Opening,   // Opening inventory
		Waiting,   // Waiting for inventory to open
		Refilling, // Moving items
		Closing    // Waiting before closing
	};
	
	State state = State::Idle;
	std::chrono::steady_clock::time_point stateStartTime;
	std::chrono::steady_clock::time_point lastMoveTime;
	
	// Track which hotbar slots need refilling
	struct RefillTask {
		int hotbarSlot;    // 0-8
		int sourceSlot;    // 9-35 (main inventory)
		int itemId;
	};
	std::vector<RefillTask> pendingRefills;
	
	// Find slots that need refilling (empty hotbar slots that accept refill items)
	void scanForRefills();
	
	// Check if an item should be refilled
	bool isRefillableItem(int itemId, bool isHealthPotion);
	
	// Find replacement in main inventory (slots 9-35)
	int findReplacementSlot(const std::vector<SlotInfo>& slots, int itemId, bool requireHealthPotion, const std::vector<int>& usedSlots);
	
	// Calculate smart delay based on slot distance
	int calculateSmartDelay(int fromSlot, int toSlot);
	
	// Complete the refill action and disable module
	void finishRefill();
};

extern AutoRefill autoRefill;
