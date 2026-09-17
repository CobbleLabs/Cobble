#pragma once

#include "../module.h"

class Reach : public Module {
public:
    Reach();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        float distance = 3.6f;

        // Conditions
        bool weaponsOnly = false;    // Only when holding sword/axe
        bool sprintingOnly = false;  // Only when sprinting
        bool movingOnly = false;     // Only when moving

        // Anti-detection
        int hitChance = 100;  // % chance to apply reach per tick
        int tickDelay = 0;    // Skip N ticks between applications (0 = every tick)
    } settings;

private:
    int tickCounter = 0;
    unsigned int rngState = 0;

    float randomFloat(float min, float max);
    bool shouldApplyThisTick();
};

extern Reach* reach;
