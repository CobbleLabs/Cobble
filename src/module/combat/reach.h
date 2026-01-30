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

    struct Settings {
        float distance = 3.6f;
    } settings;
};

extern Reach* reach;
