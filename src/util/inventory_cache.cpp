#include "inventory_cache.h"
#include "logger.hpp"

// Static member initialization
std::array<int, 9> InventoryCache::hotbarItems = {-1, -1, -1, -1, -1, -1, -1, -1, -1};
std::atomic<int> InventoryCache::occupiedSlots{0};
std::atomic<bool> InventoryCache::hasPotion{false};
std::mutex InventoryCache::updateMutex;

void InventoryCache::init()
{
	reset();
	log_ok("[InventoryCache] Initialized");
}

void InventoryCache::onSlotChanged(int slot, int itemId)
{
	// Only track hotbar slots (0-8)
	if (slot < 0 || slot > 8) {
		return;
	}
	
	{
		std::lock_guard<std::mutex> lock(updateMutex);
		hotbarItems[slot] = itemId;
		recalculateState();
	}
	
	// Debug logging
	log_debug("[InventoryCache] Slot %d changed to item %d (occupied: %d, hasPotion: %s)",
		slot, itemId, occupiedSlots.load(), hasPotion.load() ? "true" : "false");
}

void InventoryCache::recalculateState()
{
	// Must be called with mutex locked
	int count = 0;
	bool foundPotion = false;
	
	for (int itemId : hotbarItems) {
		if (itemId > 0) {  // Slot has an item
			count++;
			if (isPotion(itemId)) {
				foundPotion = true;
			}
		}
	}
	
	occupiedSlots.store(count);
	hasPotion.store(foundPotion);
}

bool InventoryCache::isPotion(int itemId)
{
	// Minecraft 1.8.9 potion IDs
	return itemId == 373;  // minecraft:potion
}

bool InventoryCache::shouldBlockInventoryClick()
{
	// Block only if hotbar is FULL (9/9) with at least one potion
	int occupied = occupiedSlots.load();
	bool hasPot = hasPotion.load();
	bool shouldBlock = (occupied == 9 && hasPot);
	
	// Debug logging
	static int logCounter = 0;
	if (++logCounter % 60 == 0) {
		log_debug("[InventoryCache] Check: occupied=%d, hasPotion=%s, blocking=%s",
			occupied, hasPot ? "YES" : "NO", shouldBlock ? "YES" : "NO");
	}
	
	return shouldBlock;
}

void InventoryCache::reset()
{
	std::lock_guard<std::mutex> lock(updateMutex);
	hotbarItems.fill(-1);
	occupiedSlots.store(0);
	hasPotion.store(false);
	log_debug("[InventoryCache] Reset");
}
