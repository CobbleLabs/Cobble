#pragma once

#include <chrono>
#include <vector>
#include <string>
#include "../module.h"

// InventoryManager - Save and restore hotbar layouts with multiple presets
// Press keybind -> opens inventory -> swaps items to saved positions -> closes
class InventoryManager : public Module {
public:
    InventoryManager();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    bool canToggle() override;

    // Save current hotbar layout to active preset
    void saveHotbar();

    // Check if current preset has been saved
    bool hasSavedLayout() const;

    struct SavedSlot {
        int itemId = 0;
        int damage = 0;  // Used to distinguish items with same ID (e.g. potions)
    };

    // Preset structure
    static constexpr int MAX_PRESETS = 4;
    struct Preset {
        SavedSlot slots[9] = {};
        bool saved = false;
        std::string name;  // Optional custom name
    };

    struct Settings {
        int swapDelay = 50;  // Delay between swaps (ms)
        int activePreset = 0;  // Currently active preset (0-3)
        bool onInventoryOpen = false;  // Only trigger when inventory is already open
    } settings;

    // All presets
    Preset presets[MAX_PRESETS];

    // Get current active preset
    Preset& getActivePreset() { return presets[settings.activePreset]; }
    const Preset& getActivePreset() const { return presets[settings.activePreset]; }

private:
    enum class State { Idle, WaitingForInventory, OpeningInventory, Swapping, Closing };

    State state = State::Idle;
    std::chrono::steady_clock::time_point stateStartTime;
    std::chrono::steady_clock::time_point lastSwapTime;

    struct SwapTask {
        int sourceSlot;    // Where the item currently is (0-35)
        int targetHotbar;  // Where it should go (0-8)
    };
    std::vector<SwapTask> pendingSwaps;

    static constexpr int TIMEOUT_MS = 2000;

    void scanForSwaps();
    void finishRestore();

    // Convert inventory slot to container slot for windowClickSwap
    int inventoryToContainerSlot(int invSlot);
};

extern InventoryManager* inventoryManager;
