#pragma once
#include "../module.h"
#include "../../sdk/core/minecraft.h"

class AutoTool : public Module
{
public:
	AutoTool() : Module("Auto Tool") {}

	void onUpdate() override;
	void onRenderMenu() override;

	struct Settings {
		bool pickaxeForStone = true;   // Use pickaxe for stone/ores
		bool axeForWood = true;        // Use axe for wood/logs
		bool shovelForDirt = true;     // Use shovel for dirt/sand/gravel
		bool switchBack = true;        // Switch back to previous slot when not mining
	} settings;

private:
	int m_previousSlot = -1;  // Slot before auto-switch (for switch back feature)
	bool m_switched = false;  // Track if we performed an auto-switch
	
	// Tool type enumeration
	enum class ToolType {
		NONE,
		PICKAXE,
		AXE,
		SHOVEL,
		SWORD
	};
	
	// Get the best tool type for a block ID
	ToolType getBestToolForBlock(int blockId);
	
	// Find a tool of given type in hotbar (0-8), returns slot or -1 if not found
	int findToolInHotbar(ToolType type);
	
	// Check if an item ID is a specific tool type
	bool isPickaxe(int itemId);
	bool isAxe(int itemId);
	bool isShovel(int itemId);
	bool isSword(int itemId);
};

extern AutoTool* autoTool;
