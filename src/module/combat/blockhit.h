#pragma once

#include <chrono>

#include "../module.h"

// BlockHit - sword blocking around your attacks. Instead of holding the block
// constantly (which eats your own hits / desyncs), it holds the sword block for
// damage reduction and drops it for a short window around each attack so the
// hit lands clean, then re-blocks. Two modes, switchable with a keybind:
//   Perfect  - always block while fighting (max ~50% incoming reduction).
//   Adaptive - only block while actually taking hits (drip-combo style).
class BlockHit : public Module {
public:
    BlockHit();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum class Mode { Adaptive = 0, Perfect = 1 };

    struct Settings {
        int mode = (int)Mode::Perfect;
        int modeKeybind = 0;        // key to toggle Adaptive <-> Perfect
        int unblockWindowMs = 55;   // how long the block drops around each attack
        bool weaponOnly = true;     // only engage with a sword
    } settings;

private:
    bool blocking_ = false;
    bool prevLeftDown_ = false;
    bool prevModeKey_ = false;
    std::chrono::steady_clock::time_point lastPhysAttack_;

    void setBlocking(bool block);
    bool isHoldingSword();
};

extern BlockHit* blockHit;
