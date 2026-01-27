#pragma once

#include <chrono>
#include <vector>
#include "../module.h"
#include "../../sdk/core/minecraft.h"

// Armor IDs in Minecraft 1.8
namespace ArmorItems {
// Helmets
constexpr int LEATHER_HELMET = 298;
constexpr int CHAINMAIL_HELMET = 302;
constexpr int IRON_HELMET = 306;
constexpr int DIAMOND_HELMET = 310;
constexpr int GOLD_HELMET = 314;

// Chestplates
constexpr int LEATHER_CHESTPLATE = 299;
constexpr int CHAINMAIL_CHESTPLATE = 303;
constexpr int IRON_CHESTPLATE = 307;
constexpr int DIAMOND_CHESTPLATE = 311;
constexpr int GOLD_CHESTPLATE = 315;

// Leggings
constexpr int LEATHER_LEGGINGS = 300;
constexpr int CHAINMAIL_LEGGINGS = 304;
constexpr int IRON_LEGGINGS = 308;
constexpr int DIAMOND_LEGGINGS = 312;
constexpr int GOLD_LEGGINGS = 316;

// Boots
constexpr int LEATHER_BOOTS = 301;
constexpr int CHAINMAIL_BOOTS = 305;
constexpr int IRON_BOOTS = 309;
constexpr int DIAMOND_BOOTS = 313;
constexpr int GOLD_BOOTS = 317;
}  // namespace ArmorItems

// Armor material types (full set)
enum class ArmorMaterial { Diamond = 0, Gold = 1, Iron = 2, Leather = 3, Chain = 4 };

// ArmorSwitch - Quickly swap between two armor sets using hotbar swap
// The armor can be anywhere in the inventory (hotbar or main inventory)
// Press keybind -> opens inventory -> moves to hotbar if needed -> swaps -> closes
class ArmorSwitch : public Module {
public:
    ArmorSwitch();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    bool canToggle() override;

    struct Settings {
        // Full set materials
        int set1Material = 0;  // ArmorMaterial::Diamond
        int set2Material = 2;  // ArmorMaterial::Iron

        // Delay between swaps (ms)
        int switchDelay = 50;
        
        // When same material, prefer higher Protection enchant
        bool preferProtection = true;
        
        // Only trigger when inventory is already open
        bool onInventoryOpen = false;
    } settings;

private:
    enum class State { Idle, WaitingForInventory, OpeningInventory, Switching, Closing };

    State state = State::Idle;
    std::chrono::steady_clock::time_point stateStartTime;
    std::chrono::steady_clock::time_point lastClickTime;

    // Which set we're switching TO
    int targetSet = 0;

    // Swap task: swap a container slot with a hotbar slot
    struct SwapTask {
        int containerSlot;  // Container slot id: armor 5-8, main inv 9-35, hotbar 36-44
        int hotbarIndex;    // Hotbar index 0-8
    };
    std::vector<SwapTask> pendingSwaps;

    static constexpr int DELAY_AFTER_OPEN = 100;
    static constexpr int DELAY_BEFORE_CLOSE = 80;

    // Get armor item ID for a material at a specific slot type
    int getArmorId(ArmorMaterial material, int slotType);

    // Find armor in inventory matching the desired material for a slot type
    // Returns slot id (0-35) or -1 if not found
    // Takes pre-fetched slots
    int findArmorInInventory(const std::vector<SlotInfo>& slots, int slotType, ArmorMaterial material, int* outDamage, int* outProtLevel = nullptr);

    // Detect which set the player is currently wearing
    int detectCurrentSet();

    void scanForSwaps();
    void finishSwap();
    
    // Convert mainInventory slot (0-35) to container slot for windowClickSwap
    int inventoryToContainerSlot(int invSlot);
};

extern ArmorSwitch* armorSwitch;
