#pragma once

#include "../module.h"

#include <chrono>
#include <random>

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
        bool blocksOnly = true;    // Only for blocks, not items
        int minDelayMs = 50;       // Lower bound of the place interval (ms)
        int maxDelayMs = 110;      // Upper bound of the place interval (ms)
        bool randomization = true; // Humanized (non-uniform) interval, like the AutoClicker
    } settings;

private:
    std::chrono::steady_clock::time_point lastPlace{};
    float nextDelayMs = 0.0f;
    std::mt19937 rng;

    double unit();        // uniform draw in [0,1)
    void rollNextDelay(); // pick nextDelayMs from the configured range
};

extern FastPlace* fastPlace;
