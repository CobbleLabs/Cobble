#pragma once

#include "../module.h"

// AimBow - projectile aim assist for the bow. While you draw the bow it locks
// onto the player nearest your crosshair and solves the ballistic arc (gravity
// + charge-based arrow speed) with optional movement prediction.
class AimBow : public Module {
public:
    AimBow();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum class AimMode {
        Automatic,
        ClosestToCrosshair
    };

    // Armor-tier filter bitmask: only aim at players wearing one of the checked
    // tiers (a player matches if ANY piece is a checked tier). 0 = everyone.
    enum ArmorTierBit {
        ARMOR_NO_ARMOR = 1 << 0,  // players wearing NO armor
        ARMOR_LEATHER  = 1 << 1,
        ARMOR_CHAIN    = 1 << 2,
        ARMOR_IRON     = 1 << 3,
        ARMOR_GOLD     = 1 << 4,
        ARMOR_DIAMOND  = 1 << 5,
    };

    struct Settings {
        AimMode mode = AimMode::Automatic;
        float range = 60.0f;           // max target distance (blocks)
        int fov = 120;                 // acquisition cone (degrees)
        float speed = 25.0f;           // aim smoothing 1..100 (100 = snap)
        bool predict = true;           // lead a moving target
        float predictStrength = 1.0f;  // 0..2 prediction multiplier
        bool targetInvisible = false;
        bool targetTeammates = false;
        bool onlyWhileDrawing = true;  // only aim while actually drawing the bow
        bool onlyVisible = true;
        int armorTierMask = 0;         // ArmorTierBit bitmask (0 = target everyone)
        bool armorFilterEnabled = true;  // master toggle for the armor filter (key-toggleable)
        int armorFilterKeybind = 0;      // virtual key to toggle armorFilterEnabled (0 = none)
    } settings;

private:
    unsigned long chargeStartMs = 0;
    bool wasDrawing = false;
    bool hasTarget = false;
    bool armorFilterKeyPrev_ = false;  // edge detection for the armor filter keybind

    float manualYaw = 0.0f;
    float manualPitch = 0.0f;
    float lastOverrideYaw = 0.0f;
    float lastOverridePitch = 0.0f;
    bool hasOverride = false;

    static float wrapDeg(float a);
};

extern AimBow* aimBow;
