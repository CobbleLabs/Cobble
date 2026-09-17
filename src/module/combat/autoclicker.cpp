#include "autoclicker.h"
#include "silentaura.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/core/overlay.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/network/json.hpp"
#include "hitselect.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include <array>
#include <limits>
#include <vector>

AutoClicker* autoClicker = nullptr;

// Weapon item IDs
namespace ItemIds {
// Swords
constexpr int WOOD_SWORD = 268;
constexpr int STONE_SWORD = 272;
constexpr int IRON_SWORD = 267;
constexpr int DIAMOND_SWORD = 276;
constexpr int GOLD_SWORD = 283;

// Axes
constexpr int WOOD_AXE = 271;
constexpr int STONE_AXE = 275;
constexpr int IRON_AXE = 258;
constexpr int DIAMOND_AXE = 279;
constexpr int GOLD_AXE = 286;
}  // namespace ItemIds

AutoClicker::AutoClicker() : Module("AutoClicker") {
    keybind = 0;  // No default keybind

    std::random_device rd;
    rng.seed(rd());
    reseedSecureRng();

    regenerateDelayRange();
}

void AutoClicker::onEnable() {
    if (silentAura && silentAura->enabled)
        silentAura->setEnabled(false);

    auto now = std::chrono::steady_clock::now();

    // Stable mode - counter-based timing with strict interval
    stableTimelineStart = now;
    stableLastClick = now;  // Initialize to prevent first-click burst
    stableClickCount = 0;

    // Random mode
    lastClick = now;
    lastClickWasDown = false;
    wasBreakingBlock = false;
    clicksSinceRangeChange = 0;
    sessionStart = now;
    fatigueMultiplier = 1.0f;
    fatigueTargetMultiplier = 1.0f;
    fatiguePhaseClicksRemaining = 0;
    recoveringFromFatigue = true;
    inBurst = false;
    burstClicksRemaining = 0;
    burstSpeedMultiplier = 1.0f;
    sigmaState = 0.12f + static_cast<float>(secureUnit()) * 0.20f;  // 0.12..0.32
    sigmaClicksRemaining = 0;
    sigmaTarget = sigmaState;
    // Per-session sigma baseline offset: every onEnable starts from a slightly
    // different distribution center so per-session std never repeats.
    sigmaSessionOffset = (static_cast<float>(secureUnit()) - 0.5f) * 0.08f;  // -0.04..+0.04
    tempoStepScale = 0.08f + static_cast<float>(secureUnit()) * 0.16f;        // 0.08..0.24
    tempoPull = 0.02f + static_cast<float>(secureUnit()) * 0.10f;             // 0.02..0.12
    hesitationChance = 0.01f + static_cast<float>(secureUnit()) * 0.05f;      // 1-6%
    doubleTapChance = 0.005f + static_cast<float>(secureUnit()) * 0.035f;     // 0.5-4%
    shapePhaseClicksRemaining = 0;
    for (auto& row : pinkRows)
        row = static_cast<float>(secureUnit()) * 2.0f - 1.0f;
    pinkCounter = 0;
    pinkState = 0.0f;
    pinkAmplitude = 0.05f + static_cast<float>(secureUnit()) * 0.12f;  // 0.05..0.17
    plusCpsState = std::clamp((settings.minCPS + settings.maxCPS) * 0.5f, 1.0f, 25.0f);
    plusStateClicksRemaining = 0;
    clickDownFraction = 0.5f;
    // Seed the interval/gap so the first press waits a normal spacing, not a burst.
    currentClickIntervalMs = 1000.0f / std::clamp((settings.minCPS + settings.maxCPS) * 0.5f, 1.0f, 20.0f);
    pendingGapMs = currentClickIntervalMs;

    if (!settings.stableCPS) {
        regenerateDelayRange();
        calculateNextDelay();
    }
}

void AutoClicker::onDisable() {
    HWND hwnd = GetForegroundWindow();

    // Release any held block-breaking click
    if (wasBreakingBlock && hwnd) {
        PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
        wasBreakingBlock = false;
    }

    // Release click if we ended on a DOWN
    if (lastClickWasDown && hwnd) {
        PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
        lastClickWasDown = false;
    }
}

void AutoClicker::regenerateDelayRange() {
    // This makes timing patterns less predictable
    std::uniform_real_distribution<float> minDist(15.0f, 40.0f);
    std::uniform_real_distribution<float> maxDist(60.0f, 85.0f);
    std::uniform_int_distribution<int> nextChangeDist(30, 70);

    currentDelayMin = minDist(rng);
    currentDelayMax = maxDist(rng);
    clicksUntilRangeChange = nextChangeDist(rng);
    clicksSinceRangeChange = 0;
}

void AutoClicker::onUpdate() {
    if (!enabled)
        return;

    // Don't click while in menu
    if (Menu::isOpen)
        return;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return;

    // Only click when Minecraft window is focused (prevents issues with window resizing)
    if (hwnd != Overlay::gameWindow)
        return;

    // Not holding left mouse button - resync timeline to prevent burst on re-press
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
        if (settings.stableCPS) {
            auto now = std::chrono::steady_clock::now();
            stableTimelineStart = now;
            stableLastClick = now;
            stableClickCount = 0;
        }
        return;
    }

    // Check GUI state
    bool inInventory = mc.isInventoryOpen();  // True only for GuiContainer (chest, inventory, etc.)
    bool inAnyGui = mc.isInGui();             // True for any GUI screen including pause menu

    // If in a GUI but not an inventory, always stop clicking
    if (inAnyGui && !inInventory)
        return;

    // If in inventory, check the workInInventory setting
    if (inInventory && !settings.workInInventory)
        return;

    // In inventory, autoclick only works when Shift is held
    // Without Shift, user can do normal single clicks
    if (inInventory && !(GetAsyncKeyState(VK_SHIFT) & 0x8000)) {
        return;  // No autoclick without Shift in inventory
    }

    // Weapon-only check (skip in inventory)
    if (settings.weaponOnly && !inInventory && !isHoldingWeapon()) {
        // Resync stable mode timeline to prevent burst when weapon is equipped
        if (settings.stableCPS) {
            auto now = std::chrono::steady_clock::now();
            stableTimelineStart = now;
            stableLastClick = now;
            stableClickCount = 0;
        }
        return;
    }

    // Break blocks - edge detection state machine
    // Only transition on state CHANGE, not every frame
    // Ignore break blocks when in inventory
    bool isBreaking = settings.breakBlocks && !inInventory && isTargetingBlock();

    if (isBreaking && !wasBreakingBlock) {
        // ENTERING breaking state: release any pending synthetic click, then hold native DOWN
        if (lastClickWasDown) {
            PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
            lastClickWasDown = false;
        }
        // Send one DOWN to start breaking (Minecraft needs this)
        PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
        wasBreakingBlock = true;
        return;
    }

    if (isBreaking && wasBreakingBlock) {
        // HOLDING breaking state: do nothing, let the DOWN stay pressed
        return;
    }

    if (!isBreaking && wasBreakingBlock) {
        // EXITING breaking state: release the block-breaking click
        PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
        wasBreakingBlock = false;
        lastClickWasDown = false;
        // Reset timing so autoclicks resume cleanly without catch-up burst
        auto now = std::chrono::steady_clock::now();
        lastClick = now;
        nextEventTime = now;
        stableTimelineStart = now;
        stableLastClick = now;
        stableClickCount = 0;
    }

    // Attacking only - check if player is in crosshair (skip in inventory)
    if (settings.attackingOnly && !inInventory) {
        if (!isTargetingPlayer()) {
            return;
        }
    }

    // =======================
    // STABLE MODE - FIXED CPS (Strict timing - EXACT target)
    // =======================
    if (settings.stableCPS) {
        auto now = std::chrono::steady_clock::now();

        // Calculate the MINIMUM interval between clicks
        // This guarantees we NEVER exceed targetCPS
        // For 17 CPS: minInterval = 1000/17 = 58.82ms
        double targetCps = static_cast<double>(settings.stableCPSValue);
        double minIntervalMs = 1000.0 / targetCps;  // EXACT: never exceed target

        // Check time since last click
        double timeSinceLastClick = std::chrono::duration<double, std::milli>(now - stableLastClick).count();

        // HARD CAP: Never click faster than minIntervalMs
        // This mathematically guarantees we cannot exceed targetCPS
        if (timeSinceLastClick < minIntervalMs)
            return;

        // Check if we should use right-click (inventory + shift = right click to avoid bounce)
        bool useRightClick = inInventory && (GetAsyncKeyState(VK_SHIFT) & 0x8000);

        // Perform exactly one click
        if (useRightClick) {
            PostMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
            PostMessage(hwnd, WM_RBUTTONUP, 0, 0);
        } else {
            // Check HitSelect before attacking
            if (hitSelect && hitSelect->enabled && !hitSelect->shouldAllowClick()) {
                // HitSelect blocked this click, still update timing
                stableLastClick = now;
                return;
            }
            PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
            PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
        }
        stableClickCount++;
        stableLastClick = now;
        return;
    }

    // Random mode: relative delay
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastClick);
    if (elapsed.count() < nextDelayMs)
        return;

    // Perform click (random mode)
    // Check if we should use right-click (inventory + shift = right click to avoid bounce)
    bool useRightClick = inInventory && (GetAsyncKeyState(VK_SHIFT) & 0x8000);

    HWND clickHwnd = GetForegroundWindow();
    if (clickHwnd) {
        if (useRightClick) {
            // Right click for inventory mode
            if (lastClickWasDown) {
                PostMessage(clickHwnd, WM_RBUTTONUP, 0, 0);
                lastClickWasDown = false;
            } else {
                PostMessage(clickHwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
                lastClickWasDown = true;
            }
        } else {
            // Normal left click - check HitSelect first
            if (hitSelect && hitSelect->enabled && !hitSelect->shouldAllowClick()) {
                // HitSelect blocked this click
                lastClick = now;
                calculateNextDelay();
                return;
            }
            doClick();
        }
    }
    lastClick = now;
    lastAutoClick = now;

    // Update dynamic delay system (random mode only)
    clicksSinceRangeChange++;
    if (clicksSinceRangeChange >= clicksUntilRangeChange) {
        regenerateDelayRange();
    }

    calculateNextDelay();
}

void AutoClicker::calculateNextDelay() {
    float minCps = settings.minCPS;
    float maxCps = settings.maxCPS;

    if (minCps > maxCps)
        std::swap(minCps, maxCps);

    // Stable mode no pasa por calculateNextDelay - usa su propio timeline
    if (settings.stableCPS)
        return;

    // The server times the DOWN (press) event, so the full generated interval must
    // land on the DOWN->next-DOWN gap. Generate the whole interval ONCE, on the
    // DOWN, and split the cycle as:
    //     DOWN --hold--> UP --(interval - hold)--> next DOWN
    // so down-to-down == interval exactly, preserving its full variance and human
    // right-skew. The previous code regenerated the interval on every UP and split
    // it into two phases, which made the measured down-to-down the AVERAGE of two
    // consecutive intervals — averaging two samples halves the variance and pulls
    // the shape toward a Gaussian, which is exactly the too-low, too-regular
    // deviation an anticheat flags even with randomization on.
    if (lastClickWasDown) {
        // Just pressed: choose THIS click's full interval and a short human hold.
        if (settings.randomization) {
            updateFatigue();
            updateBurstMode();
            currentClickIntervalMs = generateHumanIntervalMs(minCps, maxCps);
        } else {
            std::uniform_real_distribution<float> d(minCps, maxCps);
            currentClickIntervalMs = 1000.0f / std::max(d(rng), 0.1f);
        }
        // Press duration is coupled to the interval: faster clicking means
        // lighter, shorter presses; fatigue makes presses heavier. A fixed
        // uniform hold decorrelated from CPS is a common detector heuristic.
        float holdMs = currentClickIntervalMs * (0.18f + static_cast<float>(secureUnit()) * 0.22f);
        holdMs *= 1.0f + (std::max(fatigueMultiplier, 1.0f) - 1.0f) * 0.6f;
        if (secureUnit() < 0.015) {
            // Occasional heavy/deliberate press.
            holdMs += 30.0f + static_cast<float>(secureUnit()) * 50.0f;
        }
        holdMs = std::clamp(holdMs, 18.0f, std::min(140.0f, currentClickIntervalMs * 0.45f));
        pendingGapMs = std::max(1.0f, currentClickIntervalMs - holdMs);
        nextDelayMs = std::clamp(static_cast<int>(std::lround(holdMs)), 4, 500);
    } else {
        // Just released: wait the rest of the interval so the next press lands one
        // full interval after the previous press.
        nextDelayMs = std::clamp(static_cast<int>(std::lround(pendingGapMs)), 4, 500);
    }
}

void AutoClicker::reseedSecureRng() {
    std::random_device rd;
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::array<uint32_t, 10> seedData{};
    for (size_t i = 0; i < seedData.size(); ++i) {
        const uint64_t mixedTime = static_cast<uint64_t>(now) >> ((i % 4) * 16);
        seedData[i] = rd() ^ static_cast<uint32_t>(mixedTime) ^
                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this) >> (i % 8));
    }
    std::seed_seq seed(seedData.begin(), seedData.end());
    secureRng.seed(seed);
    secureDrawCount = 0;
}

double AutoClicker::secureUnit() {
    if (++secureDrawCount >= 256)
        reseedSecureRng();
    return std::generate_canonical<double, std::numeric_limits<double>::digits>(secureRng);
}

float AutoClicker::generateTargetCps(float minCps, float maxCps) {
    minCps = std::clamp(minCps, 1.0f, 25.0f);
    maxCps = std::clamp(maxCps, minCps, 25.0f);
    const float range = maxCps - minCps;
    if (range <= 0.001f)
        return minCps;

    if (settings.randomizationMode == RandomizationMode::RANDOM) {
        std::uniform_real_distribution<float> distribution(minCps, maxCps);
        return distribution(rng);
    }

    // SECURE_RANDOM: bell-weighted (average of two secure draws) so it isn't a
    // dead-flat uniform histogram, but keeps it simple. The Human mode has the
    // full realistic model.
    const float u = static_cast<float>((secureUnit() + secureUnit()) * 0.5);
    return minCps + u * range;
}

// Human click interval (ms), full down-to-down. Models how a person actually
// clicks: a tempo that slowly wanders, session fatigue, occasional bursts, and
// log-normal jitter that gives the right-skewed, positively-kurtotic interval
// distribution real humans produce — with hard bounds so it never emits an
// impossible CPS peak, while still allowing natural slow pauses.
float AutoClicker::generateHumanIntervalMs(float minCps, float maxCps) {
    minCps = std::clamp(minCps, 1.0f, 20.0f);
    maxCps = std::clamp(maxCps, minCps, 20.0f);

    // Slowly-wandering tempo: re-target every few clicks toward a bell-weighted
    // point in the band, then mean-revert (natural autocorrelation, not a fixed
    // rate and not the too-rigid clamp the old "+" mode used).
    updateShapeParams();
    if (plusStateClicksRemaining <= 0) {
        const double u = (secureUnit() + secureUnit() + secureUnit()) / 3.0;  // central tendency
        const float target = minCps + static_cast<float>(u) * (maxCps - minCps);
        plusCpsState = std::clamp(0.5f * plusCpsState + 0.5f * target, minCps, maxCps);
        plusStateClicksRemaining = 4 + static_cast<int>(secureUnit() * 9.0);  // hold 4-12 clicks
    }
    --plusStateClicksRemaining;
    const float center = (minCps + maxCps) * 0.5f;
    const float step = static_cast<float>(secureUnit() - secureUnit()) * (maxCps - minCps) * tempoStepScale;
    plusCpsState = std::clamp(plusCpsState + step + (center - plusCpsState) * tempoPull, minCps, maxCps);

    float interval = 1000.0f / std::max(plusCpsState, 1.0f);
    interval *= fatigueMultiplier;     // session fatigue: >1 = gradually slower
    interval *= burstSpeedMultiplier;  // bursts: <1 = faster cluster, >1 = slower cluster

    // Log-normal multiplicative jitter (Box-Muller normal in log space). This is
    // what lifts the kurtosis and adds the right-hand tail humans have; the
    // -0.5*sigma^2 term keeps the mean interval unbiased.
    updateSigmaState();
    const double u1 = std::max(1e-9, secureUnit());
    const double u2 = secureUnit();
    const double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    // Session offset shifts the whole sigma track so back-to-back windows never
    // measure the same std / tail coverage.
    const double sigma =
        std::clamp(static_cast<double>(sigmaState + sigmaSessionOffset), 0.08, 0.42);
    interval *= static_cast<float>(std::exp(sigma * z - 0.5 * sigma * sigma));

    // 1/f (pink) component: human motor timing exhibits scale-free long-range
    // dependence that spectral analysis detects as missing in pure random
    // walks. Applied in log space like the jitter above.
    interval *= static_cast<float>(std::exp(static_cast<double>(nextPink()) * pinkAmplitude));

    // Rare human quirks. Rates drift per shape phase so tail mass is never
    // a fixed, measurable fraction of the population.
    const double q = secureUnit();
    if (q < hesitationChance) {
        // Hesitation / attention lapse — an occasional noticeably longer pause.
        interval += 40.0f + static_cast<float>(secureUnit()) * 160.0f;
    } else if (q < hesitationChance + doubleTapChance) {
        // Quick double-tap — a shorter gap (still floored below).
        interval *= 0.70f + static_cast<float>(secureUnit()) * 0.12f;
    }

    // Hard bounds: floor prevents an impossible CPS peak (never faster than
    // ~maxCps*1.08, and never under 48ms ≈ 20.8 CPS); ceiling still allows
    // natural slow pauses down to ~45% of the min CPS.
    const float hardFloor = std::max(1000.0f / (maxCps * 1.08f), 48.0f);
    const float hardCeil = 1000.0f / std::max(minCps * 0.45f, 1.0f);
    return std::clamp(interval, hardFloor, hardCeil);
}

void AutoClicker::doClick() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return;

    // Solo random mode usa doClick - stable mode hace click directo en onUpdate
    if (lastClickWasDown) {
        PostMessage(hwnd, WM_LBUTTONUP, 0, 0);
        lastClickWasDown = false;
    } else {
        PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
        lastClickWasDown = true;
    }
}

bool AutoClicker::isHoldingWeapon() {
    int id = getHeldItemId();
    if (id == -1)
        return false;

    // Swords
    if (id == ItemIds::WOOD_SWORD || id == ItemIds::STONE_SWORD || id == ItemIds::IRON_SWORD ||
        id == ItemIds::DIAMOND_SWORD || id == ItemIds::GOLD_SWORD)
        return true;

    // Axes
    if (id == ItemIds::WOOD_AXE || id == ItemIds::STONE_AXE || id == ItemIds::IRON_AXE || id == ItemIds::DIAMOND_AXE ||
        id == ItemIds::GOLD_AXE)
        return true;

    return false;
}

bool AutoClicker::isTargetingBlock() {
    return mc.isTargetingBlock();
}

bool AutoClicker::isTargetingPlayer() {
    // Check if the objectMouseOver is an entity (type 1 = block, type 2 = entity)
    // For now, we use a simpler check: if not targeting block and in combat range

    JNIEnv* env = Java::Env();
    if (!env)
        return false;

    // Clear any pending JNI exceptions from previous operations
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    if (isTargetingBlock())
        return false;

    // Get player list and check if any are in reach
    auto players = mc.getPlayerList();
    if (players.empty())
        return false;

    float playerX = static_cast<float>(player.getPosX());
    float playerZ = static_cast<float>(player.getPosZ());

    bool found = false;
    for (auto& p : players) {
        // Calculate distance
        float dx = static_cast<float>(p.posX) - playerX;
        float dz = static_cast<float>(p.posZ) - playerZ;
        float distSq = dx * dx + dz * dz;

        // Within 4.5 blocks (reach + some buffer)
        if (distSq < 20.25f) {  // 4.5^2
            found = true;
        }

        // Always cleanup GlobalRef - don't break early
        if (p.instance) {
            env->DeleteGlobalRef(p.instance);
            p.instance = nullptr;
        }
    }

    // Clear any exceptions that occurred during iteration
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

    return found;
}

int AutoClicker::getHeldItemId() {
    return player.getHeldItemId();
}

void AutoClicker::updateFatigue() {
    const auto sessionDuration =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - sessionStart).count();
    const float sessionLoad = std::min(static_cast<float>(sessionDuration) / 60.0f * 0.004f, 0.10f);

    if (fatiguePhaseClicksRemaining <= 0) {
        if (recoveringFromFatigue) {
            // Exhaustion builds over a sustained run instead of appearing as a
            // single abrupt reroll every hundred clicks.
            recoveringFromFatigue = false;
            std::uniform_real_distribution<float> exhaustionDist(0.08f, 0.18f);
            std::uniform_int_distribution<int> lengthDist(90, 220);
            fatigueTargetMultiplier = 1.0f + sessionLoad + exhaustionDist(rng);
            fatiguePhaseClicksRemaining = lengthDist(rng);
        } else {
            // Recovery is shorter and retains some long-session fatigue.
            recoveringFromFatigue = true;
            std::uniform_real_distribution<float> recoveryDist(0.25f, 0.55f);
            std::uniform_int_distribution<int> lengthDist(40, 100);
            fatigueTargetMultiplier = 1.0f + sessionLoad * recoveryDist(rng);
            fatiguePhaseClicksRemaining = lengthDist(rng);
        }
    }

    --fatiguePhaseClicksRemaining;
    const float response = recoveringFromFatigue ? 0.045f : 0.018f;
    fatigueMultiplier += (fatigueTargetMultiplier - fatigueMultiplier) * response;
    fatigueMultiplier = std::clamp(fatigueMultiplier, 1.0f, 1.30f);
}

void AutoClicker::updateBurstMode() {
    // Humans click in bursts - periods of faster or slower clicking
    // Not every click, just occasional clusters

    if (burstClicksRemaining > 0) {
        burstClicksRemaining--;
        if (burstClicksRemaining == 0) {
            inBurst = false;
            burstSpeedMultiplier = 1.0f;
        }
        return;
    }

    // Small chance to enter burst mode each click
    std::uniform_int_distribution<int> burstChanceDist(0, 100);
    if (burstChanceDist(rng) > 8)
        return;  // 8% chance

    inBurst = true;

    // Burst length: 3-12 clicks
    std::uniform_int_distribution<int> lengthDist(3, 12);
    burstClicksRemaining = lengthDist(rng);

    // Burst speed: 70% chance faster, 30% chance slower
    std::uniform_int_distribution<int> typeDist(0, 100);
    if (typeDist(rng) < 70) {
        // Fast burst (0.85-0.95x delay = faster)
        std::uniform_real_distribution<float> fastDist(0.85f, 0.95f);
        burstSpeedMultiplier = fastDist(rng);
    } else {
        // Slow burst (1.05-1.20x delay = slower)
        std::uniform_real_distribution<float> slowDist(1.05f, 1.20f);
        burstSpeedMultiplier = slowDist(rng);
    }
}

void AutoClicker::updateSigmaState() {
    // Humans drift between focused (tight, low kurtosis) and distracted/loose
    // (spread, high kurtosis) states. A fixed sigma produces a fixed kurtosis,
    // which is a statistical fingerprint. Wander sigma instead.

    if (sigmaClicksRemaining <= 0) {
        std::uniform_real_distribution<float> targetDist(0.08f, 0.44f);
        std::uniform_int_distribution<int> lengthDist(40, 180);
        sigmaTarget = targetDist(rng);
        sigmaClicksRemaining = lengthDist(rng);
    }

    --sigmaClicksRemaining;
    sigmaState += (sigmaTarget - sigmaState) * 0.025f;
    sigmaState = std::clamp(sigmaState, 0.08f, 0.44f);
}

float AutoClicker::nextPink() {
    // Voss-McCartney: resample the row for every bit that rolled over in the
    // counter; the sum of power-of-two update rates approximates a 1/f
    // spectrum. Then low-pass for smoothness.
    const int prev = pinkCounter;
    ++pinkCounter;
    const int changed = prev ^ pinkCounter;
    for (int bit = 0; bit < 16; ++bit) {
        if (changed & (1 << bit))
            pinkRows[bit] = static_cast<float>(secureUnit()) * 2.0f - 1.0f;
    }
    float sum = 0.0f;
    for (const float row : pinkRows)
        sum += row;
    const float target = sum / 16.0f;
    pinkState += (target - pinkState) * 0.4f;
    return pinkState;
}

void AutoClicker::updateShapeParams() {
    // Every so often the entire generation shape shifts: tempo walk step size,
    // mean-reversion strength, hesitation rate, and double-tap rate all
    // re-randomize together. Measurement windows that straddle a phase change
    // see different std / kurtosis / tail mass, so no single statistical
    // fingerprint persists across a fight or a session.

    if (shapePhaseClicksRemaining > 0) {
        --shapePhaseClicksRemaining;
        return;
    }

    std::uniform_real_distribution<float> stepDist(0.08f, 0.24f);   // tempo step scale
    std::uniform_real_distribution<float> pullDist(0.02f, 0.12f);   // mean reversion
    std::uniform_real_distribution<float> hesDist(0.01f, 0.06f);    // hesitation rate
    std::uniform_real_distribution<float> tapDist(0.005f, 0.04f);   // double-tap rate
    std::uniform_int_distribution<int> lengthDist(60, 200);

    tempoStepScale = stepDist(rng);
    tempoPull = pullDist(rng);
    hesitationChance = hesDist(rng);
    doubleTapChance = tapDist(rng);
    shapePhaseClicksRemaining = lengthDist(rng);
}

void AutoClicker::onRenderMenu() {
    static const char* randomizationModes[] = {"Random", "SecureRandom", "Human"};
    bool enabledValue = enabled;
    int randomizationMode = static_cast<int>(settings.randomizationMode);
    ui::two_column(
        "General",
        [this, &enabledValue, &randomizationMode]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();

            if (settings.stableCPS) {
                // Stable mode: single slider for exact CPS (integer steps 1-25)
                int cpsInt = static_cast<int>(settings.stableCPSValue);
                if (ui::SliderInt("CPS", &cpsInt, 1, 25, "%d")) {
                    settings.stableCPSValue = static_cast<float>(cpsInt);
                    // Reset timeline on CPS change to apply immediately
                    stableTimelineStart = std::chrono::steady_clock::now();
                    stableClickCount = 0;
                }
            } else {
                // Random mode: min/max sliders
                float minLimit = 1.0f;
                float maxLimit = 20.0f;
                ui::SliderFloat("Min CPS", &settings.minCPS, 1.0f, 20.0f, &minLimit, &settings.maxCPS, "%.1f");
                ui::SliderFloat("Max CPS", &settings.maxCPS, 1.0f, 20.0f, &settings.minCPS, &maxLimit, "%.1f");
                (void)randomizationMode;
                (void)randomizationModes;
                ui::Checkbox("Randomization", &settings.randomization);
            }
        },
        "Conditions",
        [this]() {
            ui::Checkbox("Stable CPS", &settings.stableCPS);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            ui::Checkbox("Break Blocks", &settings.breakBlocks);
            ui::Checkbox("Inventory", &settings.workInInventory);
        });
}

std::string AutoClicker::getDisplayInfo() {
    if (settings.stableCPS)
        return "Stable";
    return "";
}



void AutoClicker::saveSettings(json::Value& node) const {
    node["minCps"] = settings.minCPS;
    node["maxCps"] = settings.maxCPS;
    node["weaponOnly"] = settings.weaponOnly;
    node["breakBlocks"] = settings.breakBlocks;
    node["workInInventory"] = settings.workInInventory;
    node["randomizationMode"] = static_cast<int>(settings.randomizationMode);
    node["randomization"] = settings.randomization;
    node["stableCps"] = settings.stableCPS;
    node["stableCpsValue"] = settings.stableCPSValue;
    node["advancedMode"] = settings.advancedMode;
    node["dropChance"] = settings.dropChance;
    node["spikeChance"] = settings.spikeChance;
    node["spikeMultiplier"] = settings.spikeMultiplier;
    node["kurtosis"] = settings.kurtosis;
    node["attackingOnly"] = settings.attackingOnly;
}

void AutoClicker::loadSettings(const json::Value& node) {
    settings.minCPS = node["minCps"].as_float(settings.minCPS);
    settings.maxCPS = node["maxCps"].as_float(settings.maxCPS);
    settings.weaponOnly = node["weaponOnly"].as_bool(settings.weaponOnly);
    settings.breakBlocks = node["breakBlocks"].as_bool(settings.breakBlocks);
    settings.workInInventory = node["workInInventory"].as_bool(settings.workInInventory);
    settings.randomizationMode = static_cast<RandomizationMode>(
        (std::clamp)(node["randomizationMode"].as_int(static_cast<int>(settings.randomizationMode)), 0, 2));
    settings.randomization = node["randomization"].as_bool(settings.randomization);
    settings.stableCPS = node["stableCps"].as_bool(settings.stableCPS);
    settings.stableCPSValue = node["stableCpsValue"].as_float(settings.stableCPSValue);
    settings.advancedMode = node["advancedMode"].as_bool(settings.advancedMode);
    settings.dropChance = node["dropChance"].as_float(settings.dropChance);
    settings.spikeChance = node["spikeChance"].as_float(settings.spikeChance);
    settings.spikeMultiplier = node["spikeMultiplier"].as_float(settings.spikeMultiplier);
    settings.kurtosis = node["kurtosis"].as_float(settings.kurtosis);
    settings.attackingOnly = node["attackingOnly"].as_bool(settings.attackingOnly);
}
