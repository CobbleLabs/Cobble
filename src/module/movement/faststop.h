#pragma once

#include "../module.h"

class FastStop : public Module {
public:
    FastStop();

    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        // 1.0 = full stop the tick you release the keys; lower values bleed
        // the remaining momentum off over a few ticks.
        float stopStrength = 1.0f;
        bool onlyOnGround = true;  // don't kill momentum mid-air (looks legit)
    } settings;

private:
    bool wasMoving = false;
};

extern FastStop* fastStop;
