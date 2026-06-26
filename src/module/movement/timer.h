#pragma once

#include "../module.h"

class TimerMod : public Module {
public:
    TimerMod();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        double speed = 1.0;         // 1.00x to 3.00x - use double for precision
        bool mousePressed = false;  // Only apply when mouse is clicked
        bool weaponOnly = false;    // Only apply when holding a weapon
    } settings;
};

extern TimerMod* timerModule;
