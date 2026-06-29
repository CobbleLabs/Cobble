#pragma once

#include <chrono>
#include <vector>
#include <string>
#include <algorithm>
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
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

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
        bool retryOnFail = false;
        int activePreset = 0;  // Currently active preset (0-3)
    } settings;

    // All presets
    Preset presets[MAX_PRESETS];

    // Get current active preset
    Preset& getActivePreset() { return presets[settings.activePreset]; }
    const Preset& getActivePreset() const { return presets[settings.activePreset]; }

private:
    enum class State { Idle, OpeningInventory, Swapping, Verifying, Closing };

    State state = State::Idle;
    std::chrono::steady_clock::time_point stateStartTime;
    std::chrono::steady_clock::time_point lastSwapTime;

    struct SwapTask {
        int sourceSlot;    // Where the item currently is (0-35)
        int targetHotbar;  // Where it should go (0-8)
        int expectedId;    // Expected item id in target hotbar after swap
        int expectedDamage; // Expected damage when compareDamage is true
        bool compareDamage;
        int retryCount;
    };
    std::vector<SwapTask> pendingSwaps;
    SwapTask lastSwap{};   // Last executed swap, for verification

    // If true, last scan detected mismatches but couldn't schedule swaps (items missing)
    bool lastScanMissingItems = false;

    static constexpr int TIMEOUT_MS = 2000;
    static constexpr int VERIFY_DELAY_MS = 40;
    static constexpr int MAX_RETRIES = 5;
    static constexpr int RETRY_MIN_DELAY = 25;

    void scanForSwaps();
    void finishRestore();

    // Convert inventory slot to container slot for windowClickSwap
    int inventoryToContainerSlot(int invSlot);

    int effectiveDelay() const {
        return settings.retryOnFail ? (std::max)(settings.swapDelay, RETRY_MIN_DELAY) : settings.swapDelay;
    }
};

extern InventoryManager* inventoryManager;
