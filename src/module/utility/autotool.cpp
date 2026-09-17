#include "autotool.h"
#include <Windows.h>
#include <cmath>
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/player.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

AutoTool* autoTool = nullptr;

// =============================================================================
// Tool Type Detection - Item IDs for Minecraft 1.8.9
// =============================================================================

bool AutoTool::isPickaxe(int itemId) {
    // Wood=270, Stone=274, Iron=257, Gold=285, Diamond=278
    return itemId == 270 || itemId == 274 || itemId == 257 || itemId == 285 || itemId == 278;
}

bool AutoTool::isAxe(int itemId) {
    // Wood=271, Stone=275, Iron=258, Gold=286, Diamond=279
    return itemId == 271 || itemId == 275 || itemId == 258 || itemId == 286 || itemId == 279;
}

bool AutoTool::isShovel(int itemId) {
    // Wood=269, Stone=273, Iron=256, Gold=284, Diamond=277
    return itemId == 269 || itemId == 273 || itemId == 256 || itemId == 284 || itemId == 277;
}

bool AutoTool::isSword(int itemId) {
    // Wood=268, Stone=272, Iron=267, Gold=283, Diamond=276
    return itemId == 268 || itemId == 272 || itemId == 267 || itemId == 283 || itemId == 276;
}

bool AutoTool::isShears(int itemId) {
    // Shears = 359
    return itemId == 359;
}

// =============================================================================
// Block to Tool Mapping - Which tool is best for which block (1.7.10/1.8.9)
// =============================================================================

AutoTool::ToolType AutoTool::getBestToolForBlock(int blockId) {
    // Shears blocks - Wool, Leaves, Cobweb, Vines (MUST BE FIRST - takes priority)
    switch (blockId) {
        case 18:   // Oak/Spruce/Birch/Jungle Leaves
        case 30:   // Cobweb
        case 35:   // Wool (all colors)
        case 106:  // Vines
        case 161:  // Acacia/Dark Oak Leaves
        case 175:  // Double Plants (Tall Grass, Fern - partial)
            return ToolType::SHEARS;
    }

    // Stone-type blocks -> Pickaxe
    switch (blockId) {
        case 1:    // Stone (all variants: granite, diorite, andesite)
        case 4:    // Cobblestone
        case 14:   // Gold Ore
        case 15:   // Iron Ore
        case 16:   // Coal Ore
        case 21:   // Lapis Ore
        case 22:   // Lapis Block
        case 23:   // Dispenser
        case 24:   // Sandstone (all variants)
        case 27:   // Powered Rail
        case 28:   // Detector Rail
        case 29:   // Sticky Piston
        case 33:   // Piston
        case 34:   // Piston Head
        case 41:   // Gold Block
        case 42:   // Iron Block
        case 43:   // Double Stone Slab
        case 44:   // Stone Slab
        case 45:   // Brick Block
        case 48:   // Mossy Cobblestone
        case 49:   // Obsidian
        case 52:   // Monster Spawner
        case 56:   // Diamond Ore
        case 57:   // Diamond Block
        case 61:   // Furnace
        case 62:   // Burning Furnace
        case 66:   // Rail
        case 67:   // Cobblestone Stairs
        case 70:   // Stone Pressure Plate
        case 71:   // Iron Door Block
        case 73:   // Redstone Ore
        case 74:   // Lit Redstone Ore
        case 77:   // Stone Button
        case 79:   // Ice
        case 87:   // Netherrack
        case 89:   // Glowstone
        case 97:   // Monster Egg (all variants)
        case 98:   // Stone Bricks (all variants)
        case 101:  // Iron Bars
        case 108:  // Brick Stairs
        case 109:  // Stone Brick Stairs
        case 112:  // Nether Brick
        case 113:  // Nether Brick Fence
        case 114:  // Nether Brick Stairs
        case 116:  // Enchanting Table
        case 117:  // Brewing Stand
        case 118:  // Cauldron
        case 120:  // End Portal Frame
        case 121:  // End Stone
        case 128:  // Sandstone Stairs
        case 129:  // Emerald Ore
        case 130:  // Ender Chest
        case 133:  // Emerald Block
        case 138:  // Beacon
        case 139:  // Cobblestone Wall
        case 145:  // Anvil
        case 147:  // Weighted Pressure Plate Light
        case 148:  // Weighted Pressure Plate Heavy
        case 152:  // Redstone Block
        case 153:  // Nether Quartz Ore
        case 154:  // Hopper
        case 155:  // Quartz Block (all variants)
        case 156:  // Quartz Stairs
        case 157:  // Activator Rail
        case 158:  // Dropper
        case 159:  // Stained Hardened Clay
        case 167:  // Iron Trapdoor
        case 168:  // Prismarine (all variants)
        case 169:  // Sea Lantern
        case 172:  // Hardened Clay
        case 173:  // Coal Block
        case 174:  // Packed Ice
        case 179:  // Red Sandstone (all variants)
        case 180:  // Red Sandstone Stairs
        case 181:  // Double Red Sandstone Slab
        case 182:  // Red Sandstone Slab
            return ToolType::PICKAXE;
    }

    // Wood-type blocks -> Axe
    switch (blockId) {
        case 5:    // Wooden Planks (all types)
        case 17:   // Log (Oak, Spruce, Birch, Jungle)
        case 25:   // Note Block
        case 26:   // Bed
        case 47:   // Bookshelf
        case 53:   // Oak Stairs
        case 54:   // Chest
        case 58:   // Crafting Table
        case 63:   // Standing Sign
        case 64:   // Wooden Door Block
        case 65:   // Ladder
        case 68:   // Wall Sign
        case 72:   // Wooden Pressure Plate
        case 84:   // Jukebox
        case 85:   // Oak Fence
        case 86:   // Pumpkin
        case 91:   // Jack o' Lantern
        case 92:   // Cake
        case 96:   // Wooden Trapdoor
        case 99:   // Brown Mushroom Block
        case 100:  // Red Mushroom Block
        case 103:  // Melon Block
        case 107:  // Oak Fence Gate
        case 125:  // Double Wooden Slab
        case 126:  // Wooden Slab
        case 127:  // Cocoa
        case 134:  // Spruce Stairs
        case 135:  // Birch Stairs
        case 136:  // Jungle Stairs
        case 143:  // Wooden Button
        case 144:  // Mob Head
        case 146:  // Trapped Chest
        case 151:  // Daylight Sensor
        case 162:  // Log2 (Acacia, Dark Oak)
        case 163:  // Acacia Stairs
        case 164:  // Dark Oak Stairs
        case 170:  // Hay Bale
        case 176:  // Standing Banner
        case 177:  // Wall Banner
        case 178:  // Inverted Daylight Sensor
        case 183:  // Spruce Fence Gate
        case 184:  // Birch Fence Gate
        case 185:  // Jungle Fence Gate
        case 186:  // Dark Oak Fence Gate
        case 187:  // Acacia Fence Gate
        case 188:  // Spruce Fence
        case 189:  // Birch Fence
        case 190:  // Jungle Fence
        case 191:  // Dark Oak Fence
        case 192:  // Acacia Fence
        case 193:  // Spruce Door Block
        case 194:  // Birch Door Block
        case 195:  // Jungle Door Block
        case 196:  // Acacia Door Block
        case 197:  // Dark Oak Door Block
            return ToolType::AXE;
    }

    // Dirt-type blocks -> Shovel
    switch (blockId) {
        case 2:    // Grass
        case 3:    // Dirt (all variants: coarse, podzol)
        case 12:   // Sand (all variants)
        case 13:   // Gravel
        case 60:   // Farmland
        case 78:   // Snow Layer
        case 80:   // Snow Block
        case 82:   // Clay
        case 88:   // Soul Sand
        case 110:  // Mycelium
        case 198:  // Grass Path (1.9+ but exists in later 1.8.9 servers)
            return ToolType::SHOVEL;
    }

    return ToolType::NONE;
}

// =============================================================================
// Find Tool in Hotbar
// =============================================================================

int AutoTool::findToolInHotbar(ToolType type) {
    for (int slot = 0; slot <= 8; slot++) {
        int itemId = player.getItemInSlot(slot);
        if (itemId <= 0)
            continue;

        bool matches = false;
        switch (type) {
            case ToolType::PICKAXE:
                matches = isPickaxe(itemId);
                break;
            case ToolType::AXE:
                matches = isAxe(itemId);
                break;
            case ToolType::SHOVEL:
                matches = isShovel(itemId);
                break;
            case ToolType::SWORD:
                matches = isSword(itemId);
                break;
            case ToolType::SHEARS:
                matches = isShears(itemId);
                break;
            default:
                break;
        }

        if (matches)
            return slot;
    }
    return -1;
}

// =============================================================================
// Main Update Loop
// =============================================================================

void AutoTool::onUpdate() {
    if (!enabled) {
        m_switched = false;
        m_previousSlot = -1;
        return;
    }

    // Don't run while in menu
    if (Menu::isOpen || mc.isInGui()) {
        return;
    }

    if (!player.isValid())
        return;

    // Check if player is holding left click (mining/attacking)
    bool isLeftClickHeld = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // Check what we're targeting
    bool targetingBlock = mc.isTargetingBlock();
    bool targetingEntity = mc.isTargetingEntity();

    // Priority: Attack entity (AutoSword) > Mine block (AutoTool)
    if (isLeftClickHeld && targetingEntity && settings.autoSword) {
        // Auto-switch to sword when attacking an entity
        // NOTE: Sword stays equipped - no switch-back for combat
        int swordSlot = findToolInHotbar(ToolType::SWORD);
        int currentSlot = player.getCurrentSlot();

        if (swordSlot != -1 && swordSlot != currentSlot) {
            // Switch to sword (permanent - no switch-back)
            player.setCurrentSlot(swordSlot);
        }
    } else if (isLeftClickHeld && targetingBlock) {
        // Get block position directly from game's objectMouseOver
        Minecraft::Vec3 hitPos = mc.getTargetedBlockPos();

        // Valid position check (Y should be between 0 and 256 for valid blocks)
        if (hitPos.y >= 0 && hitPos.y < 256) {
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
    } else {
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

void AutoTool::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
        },
        "Options",
        [this]() {
            ui::Checkbox("Auto Sword (Attack)", &settings.autoSword);
            ui::Checkbox("Switch Back", &settings.switchBack);
        });
}

void AutoTool::saveSettings(json::Value& node) const {
    node["switchBack"] = settings.switchBack;
    node["autoSword"] = settings.autoSword;
}

void AutoTool::loadSettings(const json::Value& node) {
    settings.switchBack = node["switchBack"].as_bool(settings.switchBack);
    settings.autoSword = node["autoSword"].as_bool(settings.autoSword);
}
