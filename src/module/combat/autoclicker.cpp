#include "autoclicker.h"
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
    clicksSinceLastFatigueUpdate = 0;
    inBurst = false;
    burstClicksRemaining = 0;
    burstSpeedMultiplier = 1.0f;
    currentJitterMode = 0;
    jitterModeClicksRemaining = 0;
    plusCpsState = std::clamp((settings.minCPS + settings.maxCPS) * 0.5f, 1.0f, 25.0f);
    plusStateClicksRemaining = 0;
    clickDownFraction = 0.5f;

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
            SendMessage(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, 0);
            SendMessage(hwnd, WM_RBUTTONUP, 0, 0);
        } else {
            // Check HitSelect before attacking
            if (hitSelect && hitSelect->enabled && !hitSelect->shouldAllowClick()) {
                // HitSelect blocked this click, still update timing
                stableLastClick = now;
                return;
            }
            SendMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
            SendMessage(hwnd, WM_LBUTTONUP, 0, 0);
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

    // Update humanization systems (only in randomized mode)
    if (!lastClickWasDown) {
        float targetCps = generateTargetCps(minCps, maxCps);
        currentClickIntervalMs = 1000.0f / std::max(targetCps, 0.1f);

        if (settings.randomizationMode == RandomizationMode::RANDOM) {
            clickDownFraction = 0.5f;
        } else if (settings.randomizationMode == RandomizationMode::SECURE_RANDOM) {
            clickDownFraction = 0.38f + static_cast<float>(secureUnit()) * 0.12f;
        } else {
            double a = secureUnit();
            double b = secureUnit();
            clickDownFraction = 0.27f + static_cast<float>((a + b) * 0.5) * 0.24f;
        }
    }

    float phase = lastClickWasDown ? clickDownFraction : (1.0f - clickDownFraction);
    float delay = currentClickIntervalMs * phase;
    nextDelayMs = std::clamp(static_cast<int>(std::lround(delay)), 8, 500);
    return;

    // Add non-uniform jitter based on current mode
    int jitterAmount = 0;
    switch (currentJitterMode) {
        case 0:  // No jitter
            jitterAmount = 0;
            break;
        case 1:  // Small jitter Â±2ms
        {
            std::uniform_int_distribution<int> jitterDist(-2, 2);
            jitterAmount = jitterDist(rng);
        } break;
        case 2:  // Medium jitter Â±5ms
        {
            std::uniform_int_distribution<int> jitterDist(-5, 5);
            jitterAmount = jitterDist(rng);
        } break;
        case 3:  // Large jitter Â±12ms
        {
            std::uniform_int_distribution<int> jitterDist(-12, 12);
            jitterAmount = jitterDist(rng);
        } break;
    }
    delay += jitterAmount;

    // Halve the delay because alternating pattern needs 2 calls per click
    delay *= 0.5f;

    // Clamp to sane values (these are per-event, not per-click)
    nextDelayMs = static_cast<int>(delay);
    nextDelayMs = (std::max)(nextDelayMs, 20);   // Min ~25 CPS
    nextDelayMs = (std::min)(nextDelayMs, 100);  // Max ~5 CPS
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

    if (settings.randomizationMode == RandomizationMode::SECURE_RANDOM) {
        return minCps + static_cast<float>(secureUnit()) * range;
    }

    if (plusStateClicksRemaining <= 0) {
        const float center = minCps + static_cast<float>((secureUnit() + secureUnit()) * 0.5) * range;
        plusCpsState = std::clamp(center, minCps, maxCps);
        plusStateClicksRemaining = 3 + static_cast<int>(secureUnit() * 10.0);
    }

    --plusStateClicksRemaining;
    const float randomWalk = static_cast<float>((secureUnit() - secureUnit()) * range * 0.18);
    const float microVariation = static_cast<float>((secureUnit() + secureUnit() - 1.0) * range * 0.07);
    const float centerPull = ((minCps + maxCps) * 0.5f - plusCpsState) * 0.08f;
    plusCpsState = std::clamp(plusCpsState + randomWalk + centerPull, minCps, maxCps);
    return std::clamp(plusCpsState + microVariation, minCps, maxCps);
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

        // Always cleanup LocalRef - don't break early
        if (p.instance) {
            env->DeleteLocalRef(p.instance);
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
    // Humans get tired over time - clicking naturally slows down
    // Then they "wake up" and speed up again

    clicksSinceLastFatigueUpdate++;

    // Update fatigue every 100-200 clicks
    if (clicksSinceLastFatigueUpdate < 100)
        return;

    std::uniform_int_distribution<int> shouldUpdateDist(0, 100);
    if (shouldUpdateDist(rng) > 50)
        return;

    clicksSinceLastFatigueUpdate = 0;

    // Session duration affects fatigue
    auto sessionDuration =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - sessionStart).count();

    // Base fatigue increases slowly over time (0.1% per minute)
    float baseFatigue = 1.0f + (sessionDuration / 600.0f) * 0.1f;

    // Add random variation (Â±5%)
    std::uniform_real_distribution<float> variationDist(-0.05f, 0.05f);
    fatigueMultiplier = baseFatigue + variationDist(rng);

    // Clamp to reasonable range (90% to 115% of normal speed)
    fatigueMultiplier = (std::max)(0.90f, (std::min)(1.15f, fatigueMultiplier));
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

void AutoClicker::updateJitterMode() {
    // Non-uniform jitter: sometimes no jitter, sometimes more
    // Humans don't have consistent timing variance

    if (jitterModeClicksRemaining > 0) {
        jitterModeClicksRemaining--;
        return;
    }

    // Choose new jitter mode
    std::uniform_int_distribution<int> modeDist(0, 100);
    int roll = modeDist(rng);

    // Distribution:
    // 25% - no jitter (mode 0)
    // 40% - small jitter Â±2ms (mode 1)
    // 25% - medium jitter Â±5ms (mode 2)
    // 10% - large jitter Â±10ms (mode 3)

    if (roll < 25) {
        currentJitterMode = 0;
    } else if (roll < 65) {
        currentJitterMode = 1;
    } else if (roll < 90) {
        currentJitterMode = 2;
    } else {
        currentJitterMode = 3;
    }

    // How long to stay in this mode: 5-25 clicks
    std::uniform_int_distribution<int> lengthDist(5, 25);
    jitterModeClicksRemaining = lengthDist(rng);
}

void AutoClicker::onRenderMenu() {
    static const char* randomizationModes[] = {"Random", "SecureRandom", "SecureRandom+"};
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
                if (ui::Combo("Randomization", &randomizationMode, randomizationModes,
                              IM_ARRAYSIZE(randomizationModes))) {
                    settings.randomizationMode = static_cast<RandomizationMode>(randomizationMode);
                    plusStateClicksRemaining = 0;
                    calculateNextDelay();
                }
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

    switch (settings.randomizationMode) {
        case RandomizationMode::RANDOM:
            return "Random";
        case RandomizationMode::SECURE_RANDOM:
            return "SecureRandom";
        case RandomizationMode::SECURE_RANDOM_PLUS:
            return "SecureRandom+";
    }
    return "Random";
}



void AutoClicker::saveSettings(json::Value& node) const {
    node["minCps"] = settings.minCPS;
    node["maxCps"] = settings.maxCPS;
    node["weaponOnly"] = settings.weaponOnly;
    node["breakBlocks"] = settings.breakBlocks;
    node["workInInventory"] = settings.workInInventory;
    node["randomizationMode"] = static_cast<int>(settings.randomizationMode);
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
    settings.stableCPS = node["stableCps"].as_bool(settings.stableCPS);
    settings.stableCPSValue = node["stableCpsValue"].as_float(settings.stableCPSValue);
    settings.advancedMode = node["advancedMode"].as_bool(settings.advancedMode);
    settings.dropChance = node["dropChance"].as_float(settings.dropChance);
    settings.spikeChance = node["spikeChance"].as_float(settings.spikeChance);
    settings.spikeMultiplier = node["spikeMultiplier"].as_float(settings.spikeMultiplier);
    settings.kurtosis = node["kurtosis"].as_float(settings.kurtosis);
    settings.attackingOnly = node["attackingOnly"].as_bool(settings.attackingOnly);
}
