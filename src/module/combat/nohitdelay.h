#pragma once

#include "../module.h"

class NoHitDelay : public Module {
public:
    NoHitDelay();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;

    struct Settings {
        bool weaponOnly = false;
    } settings;
};

extern NoHitDelay* noHitDelay;
