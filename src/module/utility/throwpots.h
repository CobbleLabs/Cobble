#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

#include "../module.h"

// Matches the raid0source ordering/settings semantics.
enum class ThrowPotsMode {
    Dynamic = 0,
    Single = 1,
    Double = 2
};

enum class ThrowPotsAim {
    Current = 0,
    Down = 1,
    SilentDown = 2
};

// Named timing bundles, same axis and same names as AutoPot/AutoRefill so the
// three pot-adjacent modules read consistently. This is the *manual* burst, so
// "Instant" is the old flat behaviour rather than something new.
enum class ThrowPotsPreset { Instant = 0, Fast = 1, Human = 2, Careful = 3, Custom = 4 };

// One-shot hotbar action based on raid0source's ThrowPot. Selection and item
// use go through InventoryPlayer.currentItem and PlayerControllerMP.sendUseItem.
class ThrowPots : public Module {
public:
    ThrowPots();

    void onEnable() override;
    void onDisable() override;
    void onWorldChange() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    // Called from the outgoing gate so sendUseItem is emitted immediately
    // before a flying packet instead of at an arbitrary point after it.
    void onOutgoingPacket(JNIEnv* env, jobject packet);
    bool needsOutgoingPacket() const;
    bool isRewritingRotation() const;

    struct Settings {
        bool randomSlots = false;
        ThrowPotsMode mode = ThrowPotsMode::Double;
        ThrowPotsAim aim = ThrowPotsAim::Current;
        int targetHealthPercent = 100;
        int maxThrows = 9;
        int throwDelay = 50;
        int switchDelay = 50;
        bool allowHealthI = true;
        bool switchBack = true;
        bool retryOnFail = true;

        // --- Humanization ---
        int preset = static_cast<int>(ThrowPotsPreset::Human);
        int reactionMs = 120;    // pause between the keybind and the first slot switch
        int jitterPercent = 30;  // spread applied to every delay
        bool fatigue = true;     // gaps grow slightly across a long burst
        int aimPitch = 90;       // pitch Down / Silent Down aims at, in degrees
        int pitchJitter = 4;     // spread applied to that pitch, so it is never exactly 90.0
        int aimSmoothMs = 90;    // sweep the pitch over this long instead of snapping (0 = snap)
    } settings;

    void applyPreset(ThrowPotsPreset preset);

private:
    enum class State {
        Idle,
        Reacting,  // Human delay between the keybind and the first slot switch
        SwitchDelay,
        AwaitRotation,
        AwaitUseFlush,
        Verify,
        RestoreDelay
    };

    struct PlannedPotion {
        int slot = -1;
        int damage = -1;
        int stackSize = 0;
        int healAmount = 0;
    };

    static constexpr int kPotionItemId = 373;
    static constexpr int kMaxRetries = 2;
    static constexpr int kPacketTimeoutMs = 1000;

    State state_ = State::Idle;
    std::chrono::steady_clock::time_point stateStart_{};
    int64_t stateDelay_ = 0;  // jittered wait for the current state
    std::vector<PlannedPotion> plan_;
    size_t planIndex_ = 0;
    PlannedPotion target_{};
    float healTarget_ = 20.0f;
    int originalSlot_ = -1;
    float originalPitch_ = 0.0f;
    bool pitchModified_ = false;
    int successfulThrows_ = 0;
    int retryCount_ = 0;

    // Shape of one pitch sweep. The module thread eases aimPitch_ from start to
    // end and only raises aimFinal_ once it has arrived, so the gate keeps
    // rewriting intermediate values and the server sees a flick, not a teleport.
    float aimStartPitch_ = 0.0f;
    float aimEndPitch_ = 90.0f;
    int64_t aimSweepMs_ = 0;
    int64_t restoreSweepMs_ = 0;

    std::atomic<bool> rotationRequested_{false};
    std::atomic<bool> rotationSent_{false};
    std::atomic<float> aimYaw_{0.0f};
    std::atomic<float> aimPitch_{90.0f};
    std::atomic<bool> aimFinal_{false};
    std::atomic<bool> pendingUse_{false};
    std::atomic<bool> useDispatched_{false};
    std::atomic<bool> useSucceeded_{false};

    mutable std::mt19937_64 rng_;

    int64_t jitter(int baseMs) const;
    int64_t jitter(int baseMs, int spreadPercent) const;
    float fatigueScale() const;
    float pickAimPitch() const;
    void enterState(State next, int64_t delayMs);

    bool buildPlan();
    bool selectNextPotion();
    bool targetPotionPresent(bool requireOriginalStackSize) const;
    void requestDownRotation();
    void queueUse();
    void retryOrRestore();
    void beginRestore();
    void finish(bool restorePlayerState);
    void resetTransientState();
};

extern ThrowPots* throwPots;
