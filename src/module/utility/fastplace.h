#pragma once

#include "../module.h"

class FastPlace : public Module {
public:
    FastPlace();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        bool blocksOnly = true;  // Only for blocks, not items
    } settings;
};

extern FastPlace* fastPlace;
