#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include "../module.h"

class AutoClicker : public Module {
public:
    enum class RandomizationMode { RANDOM, SECURE_RANDOM, HUMAN };

    AutoClicker();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRenderMenu() override;
    std::string getDisplayInfo() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        float minCPS = 14.0f;
        float maxCPS = 16.0f;
        bool weaponOnly = false;
        bool breakBlocks = false;  // Skip clicking when targeting block
        bool workInInventory = true;
        RandomizationMode randomizationMode = RandomizationMode::HUMAN;
        bool randomization = true;  // ON = human humanization; OFF = plain uniform CPS
        int randomizationIntensity = 100;

        // Stable CPS mode - no randomization, exact CPS
        bool stableCPS = false;
        float stableCPSValue = 17.0f;  // Exact CPS when stable mode is on

        // Advanced randomization (only when stableCPS is false)
        bool advancedMode = true;
        float dropChance = 5.0f;    // % chance to skip a click
        float spikeChance = 10.0f;  // % chance for faster click
        float spikeMultiplier = 1.3f;
        float kurtosis = 2.0f;  // Higher = more consistent CPS

        // Anti-detection
        bool attackingOnly = false;  // Only click when player in crosshair
    } settings;

private:
    std::chrono::steady_clock::time_point lastClick;
    std::chrono::steady_clock::time_point nextEventTime;
    std::chrono::steady_clock::time_point stableTimelineStart;
    std::chrono::steady_clock::time_point stableLastClick;  // For strict min interval enforcement
    std::chrono::steady_clock::time_point inventoryLastClick;
    int stableClickCount = 0;
    int nextDelayMs = 100;
    std::mt19937 rng;
    std::mt19937_64 secureRng;
    uint64_t secureDrawCount = 0;
    float plusCpsState = 15.0f;
    int plusStateClicksRemaining = 0;
    float clickDownFraction = 0.5f;
    float currentClickIntervalMs = 66.0f;
    float pendingGapMs = 66.0f;  // Remaining DOWN->next-DOWN time after the press hold

    // Dynamic delay anti-detection system
    int clicksSinceRangeChange = 0;
    int clicksUntilRangeChange = 50;
    float currentDelayMin = 25.0f;
    float currentDelayMax = 70.0f;

    // Alternating click pattern
    bool lastClickWasDown = false;

    // Block breaking state
    bool wasBreakingBlock = false;

    // Session fatigue
    std::chrono::steady_clock::time_point sessionStart;
    float fatigueMultiplier = 1.0f;
    float fatigueTargetMultiplier = 1.0f;
    int fatiguePhaseClicksRemaining = 0;
    bool recoveringFromFatigue = true;

    // Burst mode
    bool inBurst = false;
    int burstClicksRemaining = 0;
    float burstSpeedMultiplier = 1.0f;

    // Wandering sigma: kurtosis drifts between focused and loose states
    // instead of staying fixed at one value (a fixed kurtosis is a fingerprint).
    float sigmaState = 0.22f;
    int sigmaClicksRemaining = 0;
    float sigmaTarget = 0.22f;
    float sigmaSessionOffset = 0.0f;  // per-session baseline so windows never match

    // Per-phase shape parameters — the whole distribution shape changes between
    // phases so measured std / tail coverage never settles on one value.
    float tempoStepScale = 0.14f;  // tempo random-walk step size
    float tempoPull = 0.06f;       // tempo mean-reversion strength
    float hesitationChance = 0.03f;
    float doubleTapChance = 0.02f;
    int shapePhaseClicksRemaining = 0;

    // 1/f (pink noise) long-range dependence. Human motor timing has scale-free
    // correlation structure; a plain random walk converges to white noise and
    // fails spectral tests. Voss-McCartney algorithm.
    std::array<float, 16> pinkRows{};
    int pinkCounter = 0;
    float pinkState = 0.0f;
    float pinkAmplitude = 0.10f;  // log-space amplitude, randomized per session

public:
    // For AimAssist integration
    std::chrono::steady_clock::time_point lastAutoClick;

    void calculateNextDelay();
    void regenerateDelayRange();
    void doClick();

    // Humanization
    void updateFatigue();
    void updateBurstMode();
    void updateSigmaState();
    void updateShapeParams();
    float nextPink();
    void reseedSecureRng();
    double secureUnit();
    float generateTargetCps(float minCps, float maxCps);
    float generateHumanIntervalMs(float minCps, float maxCps);

    // Checks
    bool isHoldingWeapon();
    bool isTargetingBlock();
    bool isTargetingPlayer();
    int getHeldItemId();

private:
    float delayAccumulator = 0.0f;
};

extern AutoClicker* autoClicker;
