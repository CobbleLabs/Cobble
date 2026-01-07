#pragma once

#include "../module.h"

enum class ItemReleaseMode {
	CONSUMABLE = 0,  // Food, potions, blocks
	SWORD = 1,       // Swords only (blocking)
	ALL = 2          // All items except bow/fishing rod
};

class NoItemRelease : public Module {
public:
	NoItemRelease();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRenderMenu() override;

	// Settings
	struct {
		ItemReleaseMode mode = ItemReleaseMode::ALL;
	} settings;
	
	// State - needs to be public for packet interceptor
	bool wasUsingItem = false;

private:
	bool shouldBlockRelease();
	bool isConsumable(int itemId);
	bool isSword(int itemId);
	bool isBowOrRod(int itemId);
};

extern NoItemRelease* noItemRelease;
