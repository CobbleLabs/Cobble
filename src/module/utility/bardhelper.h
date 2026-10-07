#pragma once

#include <chrono>
#include <random>
#include <string>
#include <vector>

#include "../module.h"

enum class BardMode {
    Single = 0,      // One-shot macro: cycle once, switch back, and disable
    Continuous = 1   // Continuous loop: keep cycling aura while enabled
};

class BardHelper : public Module {
public:
    BardHelper();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        int mode = static_cast<int>(BardMode::Single);
        bool switchBack = true;
        bool clickBuff = false;
        int switchDelay = 100;    // Milliseconds to hold each item
        int loopDelay = 200;      // Milliseconds between cycles in continuous mode
        int randomization = 15;   // Jitter (+/- ms) for humanized switching

        // Buff items
        bool resistance = true;       // Iron Ingot (265)
        bool strength = true;         // Blaze Powder (377)
        bool regeneration = true;     // Ghast Tear (370)
        bool speed = true;            // Sugar (353)
        bool jump = true;             // Feather (288)
        bool fireResistance = false;  // Magma Cream (378)
        bool debuff = false;          // Spider Eye (375)
    } settings;

private:
    enum class State {
        Idle,
        Holding,
        BetweenCycles
    };

    struct PlannedItem {
        int slot = -1;
        int effectIndex = -1;
        int itemId = 0;
    };

    State state_ = State::Idle;
    std::chrono::steady_clock::time_point nextAction_{};
    std::vector<PlannedItem> plan_;
    size_t currentTargetIndex_ = 0;
    int originalSlot_ = -1;
    bool active_ = false;
    std::mt19937 rng_;

    int getEffectForItemId(int itemId) const;
    bool buildPlan();
    void applyTarget(size_t index);
    std::chrono::milliseconds getDelayWithJitter(int baseMs);
    void finish(bool restoreSlot);
};

extern BardHelper* bardHelper;
