#pragma once
#include "../../sdk/core/minecraft.h"
#include "../module.h"

class AutoTool : public Module {
public:
    AutoTool() : Module("Auto Tool") {}

    void onUpdate() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        bool switchBack = true;  // Switch back to previous slot when not mining
        bool autoSword = true;   // Auto-switch to sword when attacking entities
    } settings;

private:
    int m_previousSlot = -1;  // Slot before auto-switch (for switch back feature)
    bool m_switched = false;  // Track if we performed an auto-switch

    // Tool type enumeration
    enum class ToolType { NONE, PICKAXE, AXE, SHOVEL, SWORD, SHEARS };

    // Get the best tool type for a block ID
    ToolType getBestToolForBlock(int blockId);

    // Find a tool of given type in hotbar (0-8), returns slot or -1 if not found
    int findToolInHotbar(ToolType type);

    // Check if an item ID is a specific tool type
    bool isPickaxe(int itemId);
    bool isAxe(int itemId);
    bool isShovel(int itemId);
    bool isSword(int itemId);
    bool isShears(int itemId);
};

extern AutoTool* autoTool;
