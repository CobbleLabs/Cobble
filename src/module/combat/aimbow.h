#pragma once

#include <atomic>
#include <string>
#include "../module.h"

// AimBow - projectile aim assist for the bow. While you draw the bow it locks
// onto the player nearest your crosshair and solves the arrow's real trajectory
// for the launch angle that passes through its body.
//
// The solver is the vanilla arrow integration, not a vacuum parabola. One tick of
// EntityArrow.onUpdate is
//     pos += motion;  motion *= 0.99;  motion.y -= 0.05
// and that recursion telescopes into a closed form (see aimbow.cpp), so the tick
// count and height at a given horizontal distance are exact rather than
// approximated. Dropping the 0.99 drag term is what makes a naive solver shoot
// low and short past ~30 blocks: it compounds to a ~20% speed loss over a long
// shot, and no amount of tuning a gravity constant compensates for it.
//
// Launch speed is 3.0 * charge, where charge follows ItemBow's own curve
// ((t/20)^2 + 2*(t/20)) / 3 over the draw, read from the player's item-use
// counter so it matches what the server computes on release.
class AimBow : public Module {
public:
    AimBow();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;

    // True while the solver is steering the client's own rotation fields. Declared for the
    // same reason AimAssist declares it: anything that writes the rotation has to say so, or two
    // writers on one tick produce a rotation neither of them solved for. A launch pitch is the
    // worst possible thing to have fought over -- an arcing shot solves to tens of degrees above
    // the horizon, so losing the argument points the bow at the sky.
    //
    // The tail exists because this runs at ~500 Hz and the consumers are on the 20 Hz tick.
    bool isRewritingRotation() const;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    enum class AimMode { Automatic, ClosestToCrosshair };

    // Armor-tier filter bitmask: only aim at players wearing one of the checked
    // tiers (a player matches if ANY piece is a checked tier). 0 = everyone.
    enum ArmorTierBit {
        ARMOR_NO_ARMOR = 1 << 0,  // players wearing NO armor
        ARMOR_LEATHER = 1 << 1,
        ARMOR_CHAIN = 1 << 2,
        ARMOR_IRON = 1 << 3,
        ARMOR_GOLD = 1 << 4,
        ARMOR_DIAMOND = 1 << 5,
    };

    struct Settings {
        AimMode mode = AimMode::Automatic;
        float range = 60.0f;           // max target distance (blocks)
        int fov = 120;                 // acquisition cone (degrees)
        float speed = 25.0f;           // aim smoothing 1..100 (100 = snap)
        bool predict = true;           // lead a moving target
        float predictStrength = 1.0f;  // 0..2 horizontal lead multiplier
        float verticalLead = 0.5f;     // 0..2 vertical lead multiplier, deliberately weaker:
                                       // a player's vertical velocity reverses mid-flight
        float aimHeight = 0.62f;       // point on the body, 0 = feet, 1 = top of head
        bool liveCharge = true;        // solve for the charge actually on the bow
        float minSolveCharge = 0.5f;   // never solve below this charge (kills the early lob)
        bool targetInvisible = false;
        bool targetTeammates = false;
        bool onlyWhileDrawing = true;  // only aim while actually drawing the bow
        bool onlyVisible = true;
        int armorTierMask = 0;           // ArmorTierBit bitmask (0 = target everyone)
        bool armorFilterEnabled = true;  // master toggle for the armor filter (key-toggleable)
        int armorFilterKeybind = 0;      // virtual key to toggle armorFilterEnabled (0 = none)
    } settings;

private:
    unsigned long chargeStartMs = 0;
    bool wasDrawing = false;
    bool hasTarget = false;
    bool armorFilterKeyPrev_ = false;  // edge detection for the armor filter keybind

    // Smoothed target velocity (blocks/tick) for stable movement prediction.
    //
    // onUpdate() runs at ~500 Hz but the game only advances a remote player's
    // position 20 times a second, so a naive per-call EMA folds the same sample in
    // 25 times and collapses to zero the moment a frame reads no movement. We
    // therefore fold a sample only when the position we read actually changed —
    // then one EMA step really is one tick, and the estimate survives the ~24
    // duplicate reads between server updates.
    double smoothVx_ = 0.0;
    double smoothVy_ = 0.0;
    double smoothVz_ = 0.0;
    std::string velUuid_;   // target the estimate belongs to; a switch resets it
    double sampleX_ = 0.0;  // last position folded in, to spot a genuine advance
    double sampleY_ = 0.0;
    double sampleZ_ = 0.0;
    bool hasSample_ = false;

    // Last solution, for the ArrayList status line.
    float lastCharge_ = 0.0f;   // 0..1 bow charge used by the solver
    float lastDist_ = 0.0f;     // blocks to the target
    bool lastInReach_ = false;  // false when the arc has no solution at this range

    float manualYaw = 0.0f;
    float manualPitch = 0.0f;
    float lastOverrideYaw = 0.0f;
    float lastOverridePitch = 0.0f;
    bool hasOverride = false;

    // Stamped every time a rotation write lands on the player; read off-thread.
    std::atomic<unsigned long long> lastRotWriteMs_{0};

    static float wrapDeg(float a);
    void resetVelocity();
};

extern AimBow* aimBow;
