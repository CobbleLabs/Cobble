#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>

#include "../module.h"

// How the pitch is pointed at the ground for the throw.
enum class AutoPotAim {
    Silent = 0,  // camera untouched; only the outgoing movement packet says "down"
    Snap = 1,    // real pitch jumps down, throws, jumps back
    Smooth = 2,  // real pitch flicks down over a few frames, like a mouse pull
    None = 3     // throws wherever you are already looking
};

// Named timing bundles. Everything a preset touches stays editable; moving any of
// those sliders switches the combo to Custom instead of silently disagreeing with
// the label.
enum class AutoPotPreset {
    Instant = 0,  // as fast as the protocol allows, no human noise at all
    Rapid = 1,    // tournament-fast, still not frame-perfect
    Human = 2,    // what a good player actually does
    Legit = 3,    // visibly unhurried, occasional fumble
    Custom = 4
};

// AutoPot — throws splash healing potions at your own feet when your health drops.
//
// Two ideas do most of the work here:
//
//   Inbound heal accounting. A thrown potion heals ~4 or ~8 HP, but the server
//   only reports it one or two ticks later. Deciding from raw health therefore
//   throws two or three potions where one was needed. AutoPot decides from
//   `health + heal already in the air`, so a burst spends exactly as many potions
//   as the missing health calls for.
//
//   Reaction, not polling. A human does not act on the frame their health drops:
//   they notice, then move. Every step of the sequence (notice, press the slot,
//   pull the mouse down, click, click again, go back to the sword) carries its own
//   randomized delay, and a player who was already watching a low health bar
//   reacts measurably faster than one caught at full. The result varies every
//   time without any of the delays being long enough to cost a fight.
//
// Correctness details that are easy to get wrong: the potion use is flushed from
// the outgoing-packet gate so the C08 lands immediately after a movement packet
// (vanilla's own order), the throw is confirmed against the hotbar stack instead
// of assumed, and the burst re-checks health between potions so a heal that lands
// early cancels the rest.
class AutoPot : public Module {
public:
    AutoPot();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Outgoing-gate hooks, mirroring ThrowPots: the queued use is emitted right
    // before a flying packet, and the silent pitch is written onto that same
    // packet so the server sees "looking down" before "use item".
    void onOutgoingPacket(JNIEnv* env, jobject packet);
    bool needsOutgoingPacket() const;
    bool isRewritingRotation() const;

    // True while a burst owns the hotbar slot. Other modules can use this to keep
    // out of the way; nothing is required to.
    bool isBusy() const { return state_ != State::Idle; }

    struct Settings {
        // --- Trigger ---
        int healthPercent = 55;   // start potting below this share of max health
        int targetPercent = 95;   // stop once predicted health reaches this share
        int maxPots = 3;          // hard cap on one burst
        bool allowHealI = true;   // fall back to Instant Health I
        bool preferStrong = true; // spend Instant Health II first
        bool requireGround = true;
        bool onlyInCombat = false;
        int combatWindowMs = 4000;

        // --- Timing ---
        int preset = static_cast<int>(AutoPotPreset::Human);
        int reactionMs = 140;    // health drop -> hotbar key
        int reactionJitter = 45; // percent spread around every delay
        int switchMs = 55;       // hotbar key -> right click
        int throwGapMs = 210;    // between potions of one burst
        int restoreMs = 90;      // last throw -> back to the previous slot
        bool fatigue = true;     // a long burst gets slower, like real hands do

        // --- Aim ---
        int aim = static_cast<int>(AutoPotAim::Silent);
        int pitch = 87;      // degrees down; 90 is straight at your feet
        int pitchJitter = 4; // degrees of variation per throw
        int flickMs = 90;    // Smooth: how long the pull down takes
        bool switchBack = true;

        // --- Reliability ---
        bool retryOnFail = true;
        int inFlightMs = 450;   // how long a thrown potion's heal is assumed inbound
        int misclickChance = 0; // percent chance of grabbing the wrong slot first
    } settings;

    // Applies a preset's timing bundle. Public so the menu can call it from the
    // combo without duplicating the numbers.
    void applyPreset(AutoPotPreset preset);

private:
    enum class State {
        Idle,
        Arming,      // reaction delay before touching anything
        Switching,   // hotbar slot selected, waiting out the hand-to-mouse gap
        Aiming,      // pitch being pointed down (silent, snap or flick)
        Throwing,    // use queued, waiting for the gate to flush it
        Confirming,  // waiting for the hotbar stack to actually drop by one
        Cadence,     // pause between potions of the same burst
        Restoring    // pitch and slot going back
    };

    struct PotionChoice {
        int slot = -1;
        int damage = -1;
        int stackSize = 0;
        float heal = 0.0f;
    };

    static constexpr int kPotionItemId = 373;
    static constexpr int kPollIntervalMs = 6;
    static constexpr int kScanIntervalMs = 200;
    static constexpr int kUseTimeoutMs = 800;
    static constexpr int kConfirmTimeoutMs = 400;
    static constexpr int kMaxRetries = 2;

    // --- Async-loop state (single threaded: only onUpdate touches these) ---
    State state_ = State::Idle;
    int64_t stateStartMs_ = 0;
    int64_t stateDelayMs_ = 0;  // how long the current wait-style state lasts

    PotionChoice target_{};
    int originalSlot_ = -1;
    float originalPitch_ = 0.0f;
    bool pitchModified_ = false;
    float aimPitch_ = 90.0f;
    float flickFrom_ = 0.0f;
    int thrown_ = 0;
    int retries_ = 0;
    int misclickSlot_ = -1;

    float maxHealth_ = 20.0f;
    float lastHealth_ = 20.0f;
    int64_t lastMaxHealthMs_ = 0;
    int64_t lastPollMs_ = 0;
    int64_t lastScanMs_ = 0;
    int64_t lastUiScanMs_ = 0;  // slow refresh that only runs while the menu is open
    int64_t lastDamageMs_ = 0;
    int64_t lowSinceMs_ = 0;   // when health first went near the threshold
    int64_t lastThrowMs_ = 0;
    int burstFatigue_ = 0;     // potions thrown in the recent past

    // Heal that has been thrown but not yet reported by the server.
    float inFlightHeal_ = 0.0f;
    int64_t inFlightUntilMs_ = 0;
    float healthAtThrow_ = 0.0f;

    // --- Shared with the Netty write thread ---
    std::atomic<bool> rotationRequested_{false};
    std::atomic<bool> rotationSent_{false};
    std::atomic<float> aimYaw_{0.0f};
    std::atomic<float> aimPitchPacket_{90.0f};
    std::atomic<bool> pendingUse_{false};
    std::atomic<bool> useDispatched_{false};
    std::atomic<bool> useSucceeded_{false};
    std::atomic<bool> silentAimOk_{true};

    // Published for the settings page so it can show what the module can see
    // without doing its own inventory walk on the render thread. Written from the
    // const scan helpers, hence mutable.
    mutable std::atomic<int> potsAvailable_{-1};

    mutable std::mt19937_64 rng_;

    // Humanization helpers.
    int64_t jitter(int baseMs) const;
    int64_t jitter(int baseMs, int spreadPercent) const;
    bool roll(int percent) const;
    float fatigueScale() const;

    bool wantsPot(float predicted) const;
    bool pickPotion(PotionChoice& out) const;
    int countPotions() const;
    bool targetStillThere(bool requireFullStack) const;
    AutoPotAim effectiveAim() const;

    void beginBurst();
    void beginSwitch(int slot);
    void beginAim();
    void queueUse();
    void failStep();
    void beginRestore();
    void finishBurst();
    void resetSequence();
    void enterState(State next, int64_t delayMs);
};

extern AutoPot* autoPot;
