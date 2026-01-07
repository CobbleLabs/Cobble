#pragma once

#include <atomic>
#include <mutex>
#include <array>

// Thread-safe inventory state cache
// Updated via JVMTI hooks when Minecraft modifies inventory
class InventoryCache {
public:
	// Called by JVMTI hooks when inventory slot changes
	static void onSlotChanged(int slot, int itemId);
	
	// Reset cache (call when inventory closes)
	static void reset();
	
	// Called by AutoClicker to check if should block clicks
	static bool shouldBlockInventoryClick();
	
	// Initialize (call on startup)
	static void init();

private:
	// Hotbar state (slots 0-8)
	static std::array<int, 9> hotbarItems;
	
	// Cached computed state
	static std::atomic<int> occupiedSlots;
	static std::atomic<bool> hasPotion;
	
	// Thread safety
	static std::mutex updateMutex;
	
	// Recalculate occupied/potion state from hotbarItems
	static void recalculateState();
	
	// Check if item is a potion
	static bool isPotion(int itemId);
};
