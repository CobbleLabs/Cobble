#include "autotool.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include <Windows.h>
#include <cmath>

AutoTool* autoTool = nullptr;

// =============================================================================
// Tool Type Detection - Item IDs for Minecraft 1.8.9
// =============================================================================

bool AutoTool::isPickaxe(int itemId)
{
	// Wood=270, Stone=274, Iron=257, Gold=285, Diamond=278
	return itemId == 270 || itemId == 274 || itemId == 257 || itemId == 285 || itemId == 278;
}

bool AutoTool::isAxe(int itemId)
{
	// Wood=271, Stone=275, Iron=258, Gold=286, Diamond=279
	return itemId == 271 || itemId == 275 || itemId == 258 || itemId == 286 || itemId == 279;
}

bool AutoTool::isShovel(int itemId)
{
	// Wood=269, Stone=273, Iron=256, Gold=284, Diamond=277
	return itemId == 269 || itemId == 273 || itemId == 256 || itemId == 284 || itemId == 277;
}

bool AutoTool::isSword(int itemId)
{
	// Wood=268, Stone=272, Iron=267, Gold=283, Diamond=276
	return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 283 || itemId == 276;
}

// =============================================================================
// Block to Tool Mapping - Which tool is best for which block
// =============================================================================

AutoTool::ToolType AutoTool::getBestToolForBlock(int blockId)
{
	// Stone-type blocks -> Pickaxe
	// Stone=1, Cobble=4, Ores=14-16,21,22,56,73,74,129,153, Obsidian=49, 
	// Netherrack=87, Stone Bricks=98, Nether Bricks=112, End Stone=121
	switch (blockId) {
		case 1:   // Stone
		case 4:   // Cobblestone
		case 14:  // Gold Ore
		case 15:  // Iron Ore
		case 16:  // Coal Ore
		case 21:  // Lapis Ore
		case 22:  // Lapis Block
		case 23:  // Dispenser
		case 24:  // Sandstone
		case 27:  // Powered Rail
		case 28:  // Detector Rail
		case 29:  // Sticky Piston
		case 33:  // Piston
		case 41:  // Gold Block
		case 42:  // Iron Block
		case 43:  // Double Stone Slab
		case 44:  // Stone Slab
		case 45:  // Brick Block
		case 48:  // Mossy Cobble
		case 49:  // Obsidian
		case 52:  // Monster Spawner
		case 56:  // Diamond Ore
		case 57:  // Diamond Block
		case 61:  // Furnace
		case 62:  // Burning Furnace
		case 66:  // Rail
		case 67:  // Cobble Stairs
		case 70:  // Stone Pressure Plate
		case 73:  // Redstone Ore
		case 74:  // Lit Redstone Ore
		case 87:  // Netherrack
		case 89:  // Glowstone
		case 98:  // Stone Bricks
		case 108: // Brick Stairs
		case 109: // Stone Brick Stairs
		case 112: // Nether Brick
		case 113: // Nether Brick Fence
		case 114: // Nether Brick Stairs
		case 116: // Enchanting Table
		case 117: // Brewing Stand
		case 118: // Cauldron
		case 120: // End Portal Frame
		case 121: // End Stone
		case 129: // Emerald Ore
		case 130: // Ender Chest
		case 133: // Emerald Block
		case 139: // Cobble Wall
		case 145: // Anvil
		case 152: // Redstone Block
		case 153: // Nether Quartz Ore
		case 155: // Quartz Block
		case 156: // Quartz Stairs
		case 168: // Prismarine
		case 169: // Sea Lantern
		case 173: // Coal Block
			if (settings.pickaxeForStone) return ToolType::PICKAXE;
			break;
	}
	
	// Wood-type blocks -> Axe
	// Log=17, Planks=5, Wood Stairs=53,67,108, Fence=85, Crafting Table=58, Chest=54
	switch (blockId) {
		case 5:   // Wooden Planks
		case 17:  // Log
		case 25:  // Note Block
		case 47:  // Bookshelf
		case 53:  // Oak Stairs
		case 54:  // Chest
		case 58:  // Crafting Table
		case 63:  // Sign Post
		case 64:  // Wooden Door
		case 68:  // Wall Sign
		case 72:  // Wooden Pressure Plate
		case 84:  // Jukebox
		case 85:  // Fence
		case 86:  // Pumpkin
		case 91:  // Jack o' Lantern
		case 96:  // Trapdoor
		case 99:  // Huge Brown Mushroom
		case 100: // Huge Red Mushroom
		case 107: // Fence Gate
		case 125: // Double Wooden Slab
		case 126: // Wooden Slab
		case 127: // Cocoa
		case 134: // Spruce Stairs
		case 135: // Birch Stairs
		case 136: // Jungle Stairs
		case 143: // Wooden Button
		case 146: // Trapped Chest
		case 151: // Daylight Sensor
		case 157: // Activator Rail (has wood?)
		case 158: // Dropper
		case 162: // Log2 (Acacia/Dark Oak)
		case 163: // Acacia Stairs
		case 164: // Dark Oak Stairs
		case 183: // Spruce Fence Gate
		case 184: // Birch Fence Gate
		case 185: // Jungle Fence Gate
		case 186: // Dark Oak Fence Gate
		case 187: // Acacia Fence Gate
		case 188: // Spruce Fence
		case 189: // Birch Fence
		case 190: // Jungle Fence
		case 191: // Dark Oak Fence
		case 192: // Acacia Fence
			if (settings.axeForWood) return ToolType::AXE;
			break;
	}
	
	// Dirt-type blocks -> Shovel
	// Grass=2, Dirt=3, Sand=12, Gravel=13, Clay=82, Soul Sand=88, Snow=78,80
	switch (blockId) {
		case 2:   // Grass
		case 3:   // Dirt
		case 12:  // Sand
		case 13:  // Gravel
		case 60:  // Farmland
		case 78:  // Snow Layer
		case 80:  // Snow Block
		case 82:  // Clay
		case 88:  // Soul Sand
		case 110: // Mycelium
		case 159: // Stained Clay
		case 172: // Hardened Clay
		case 179: // Red Sandstone
			if (settings.shovelForDirt) return ToolType::SHOVEL;
			break;
	}
	
	return ToolType::NONE;
}

// =============================================================================
// Find Tool in Hotbar
// =============================================================================

int AutoTool::findToolInHotbar(ToolType type)
{
	for (int slot = 0; slot <= 8; slot++) {
		int itemId = player.getItemInSlot(slot);
		if (itemId <= 0) continue;
		
		bool matches = false;
		switch (type) {
			case ToolType::PICKAXE: matches = isPickaxe(itemId); break;
			case ToolType::AXE:     matches = isAxe(itemId);     break;
			case ToolType::SHOVEL:  matches = isShovel(itemId);  break;
			case ToolType::SWORD:   matches = isSword(itemId);   break;
			default: break;
		}
		
		if (matches) return slot;
	}
	return -1;
}

// =============================================================================
// Main Update Loop
// =============================================================================

void AutoTool::onUpdate()
{
	if (!enabled) {
		m_switched = false;
		m_previousSlot = -1;
		return;
	}
	
	// Don't run while in menu
	if (Menu::isOpen || mc.isInGui()) {
		return;
	}
	
	if (!player.isValid()) return;
	
	// Check if player is holding left click (mining)
	bool isLeftClickHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	
	// Check if targeting a block
	bool targetingBlock = mc.isTargetingBlock();
	
	if (isLeftClickHeld && targetingBlock) {
		// Get player look position to find the targeted block
		float yaw = player.getYaw();
		float pitch = player.getPitch();
		
		// Calculate look direction
		float yawRad = yaw * 3.14159f / 180.0f;
		float pitchRad = pitch * 3.14159f / 180.0f;
		
		float lookX = -sinf(yawRad) * cosf(pitchRad);
		float lookY = -sinf(pitchRad);
		float lookZ = cosf(yawRad) * cosf(pitchRad);
		
		// Raycast from player eye position
		float eyeHeight = 1.62f;
		Minecraft::Vec3 from(
			(float)player.getPosX(),
			(float)player.getPosY() + eyeHeight,
			(float)player.getPosZ()
		);
		
		// Ray endpoint (4 blocks reach is standard)
		Minecraft::Vec3 to(
			from.x + lookX * 4.5f,
			from.y + lookY * 4.5f,
			from.z + lookZ * 4.5f
		);
		
		Minecraft::Vec3 hitPos;
		auto result = mc.rayTraceBlocks(from, to, hitPos, false);
		
		if (result == Minecraft::RAYTRACE_BLOCKS_RESULT::HIT) {
			int blockId = mc.getBlockId((int)hitPos.x, (int)hitPos.y, (int)hitPos.z);
			
			if (blockId > 0) {
				ToolType bestTool = getBestToolForBlock(blockId);
				
				if (bestTool != ToolType::NONE) {
					int toolSlot = findToolInHotbar(bestTool);
					int currentSlot = player.getCurrentSlot();
					
					if (toolSlot != -1 && toolSlot != currentSlot) {
						// Save previous slot for switch-back
						if (!m_switched) {
							m_previousSlot = currentSlot;
							m_switched = true;
						}
						
						// Switch to the tool
						player.setCurrentSlot(toolSlot);
					}
				}
			}
		}
	}
	else {
		// Not mining - switch back to previous slot if enabled
		if (m_switched && settings.switchBack && m_previousSlot != -1) {
			int currentSlot = player.getCurrentSlot();
			if (currentSlot != m_previousSlot) {
				player.setCurrentSlot(m_previousSlot);
			}
			m_switched = false;
			m_previousSlot = -1;
		}
	}
}

// =============================================================================
// UI Rendering
// =============================================================================

void AutoTool::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
		},
		"Options", [this]() {
			ui::Checkbox("Pickaxe for Stone/Ores", &settings.pickaxeForStone);
			ui::Checkbox("Axe for Wood/Logs", &settings.axeForWood);
			ui::Checkbox("Shovel for Dirt/Sand", &settings.shovelForDirt);
			ui::Checkbox("Switch Back After Mining", &settings.switchBack);
		}
	);
}
