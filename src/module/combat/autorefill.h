#pragma once

#include "../module.h"
#include "../../sdk/core/minecraft.h"
#include <chrono>
#include <vector>

// Item IDs for refillable items
namespace RefillItems {
	constexpr int MUSHROOM_SOUP = 282;
	constexpr int POTION = 373;
}

// AutoRefill - One-shot action module
// Press keybind -> opens inventory -> refills -> closes
class AutoRefill : public Module
{
public:
	AutoRefill();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;

	struct Settings {
		// Items to refill
		bool refillSoup = true;
		bool refillPotions = true;
		
		// Speed: delay between item moves (ms)
		int speed = 80;
	} settings;

private:
	enum class State {
		Idle,
		Waiting,   // Waiting for inventory to open
		Refilling, // Moving items
		Closing    // Waiting before closing
	};
	
	State state = State::Idle;
	std::chrono::steady_clock::time_point stateStartTime;
	std::chrono::steady_clock::time_point lastMoveTime;
	
	struct RefillTask {
		int hotbarSlot;
		int sourceSlot;
	};
	std::vector<RefillTask> pendingRefills;
	
	void scanForRefills();
	void finishRefill();
	
	// Internal delays
	static constexpr int DELAY_AFTER_OPEN = 150;
	static constexpr int DELAY_BEFORE_CLOSE = 100;
};

extern AutoRefill* autoRefill;
