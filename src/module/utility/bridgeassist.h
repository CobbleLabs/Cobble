#pragma once
#include <chrono>
#include "../../sdk/core/minecraft.h"
#include "../module.h"

class BridgeAssist : public Module {
public:
    BridgeAssist() : Module("Bridge Assist") {}

    void onUpdate() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        bool onlyWhenSneaking = true;
        bool onlyLookingDown = true;
        bool autoRefillBlocks = true;
    } settings;

private:
    Minecraft::Vec3 m_from = {};

    bool m_isEdge = false;
    bool m_wasSneaking = false;
    bool m_shiftInitiated = false;
    int m_lastBlockId = -1;
    std::chrono::steady_clock::time_point m_lastRefillTime;

    void Sneak();
    void UnSneak();
    int findBlockInInventory(int preferredId) const;
};

extern BridgeAssist* bridgeAssist;
