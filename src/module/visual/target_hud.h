#pragma once

#include "../module.h"

// TargetHUD - modern panel for the player you're currently fighting: name,
// predicted-HP bar (via HealthPredictor) and distance. Target = the player
// nearest your crosshair within reach. Registered as a Visuals module.
class TargetHUD : public Module {
public:
    TargetHUD();

    void onRender() override;
    void onRenderMenu() override;
};

extern TargetHUD* targetHud;
