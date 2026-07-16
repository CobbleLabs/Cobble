#include "fastplace.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/network/json.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>

FastPlace* fastPlace = nullptr;

FastPlace::FastPlace() : Module("FastPlace") {
    keybind = 0;
    std::random_device rd;
    rng.seed(rd());
    rollNextDelay();
}

void FastPlace::onEnable() {
    lastPlace = std::chrono::steady_clock::now();
    rollNextDelay();
}

void FastPlace::onDisable() {}

double FastPlace::unit() {
    std::uniform_real_distribution<double> d(0.0, 1.0);
    return d(rng);
}

void FastPlace::rollNextDelay() {
    float lo = static_cast<float>(settings.minDelayMs);
    float hi = static_cast<float>(settings.maxDelayMs);
    if (lo > hi)
        std::swap(lo, hi);

    if (!settings.randomization || (hi - lo) < 0.5f) {
        // Flat uniform pick across the band (or a fixed value when lo == hi).
        nextDelayMs = lo + static_cast<float>(unit()) * (hi - lo);
        return;
    }

    // Humanized interval, same shape as the AutoClicker: a bell-weighted centre
    // in the band plus multiplicative log-normal jitter (right-skewed, positively
    // kurtotic) so the spacing between placements looks organic instead of a
    // dead-flat uniform histogram. Clamped back into [lo, hi].
    const double u = (unit() + unit() + unit()) / 3.0;  // central tendency
    float base = lo + static_cast<float>(u) * (hi - lo);
    const double u1 = std::max(1e-9, unit());
    const double u2 = unit();
    const double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307179586 * u2);
    const double sigma = 0.22;  // ~22% coefficient of variation
    base *= static_cast<float>(std::exp(sigma * z - 0.5 * sigma * sigma));
    nextDelayMs = std::clamp(base, lo, hi);
}

void FastPlace::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Only when right mouse button is held
    if ((GetAsyncKeyState(VK_RBUTTON) & 0x8000) == 0)
        return;

    // Check if holding a block (if blocksOnly is enabled)
    // Block IDs in Minecraft 1.8 are 1-255
    if (settings.blocksOnly) {
        int heldItemId = player.getHeldItemId();
        if (heldItemId <= 0 || heldItemId > 255)
            return;
    }

    auto now = std::chrono::steady_clock::now();
    const float elapsed = std::chrono::duration<float, std::milli>(now - lastPlace).count();
    if (elapsed < nextDelayMs)
        return;

    // Our interval elapsed: drop the vanilla place cooldown to 0 and LEAVE it,
    // so the game performs a placement on its next tick. We must not force the
    // cooldown back up on the in-between frames — onUpdate runs every frame
    // (faster than the 20 TPS game tick), so re-raising it would clobber the 0
    // before the tick reads it and nothing would ever place. Instead we simply
    // wait out the interval (returning above) and let the game's own countdown
    // sit until we open the next window. With min == max == 0 this fires every
    // frame → instant placement, exactly the classic FastPlace behaviour.
    mc.setRightClickDelay(0);
    lastPlace = now;
    rollNextDelay();
}

void FastPlace::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ImGui::Spacing();

            int mn = settings.minDelayMs;
            if (ui::SliderInt("Min Delay", &mn, 0, 500, "%dms")) {
                settings.minDelayMs = mn;
                if (settings.minDelayMs > settings.maxDelayMs)
                    settings.maxDelayMs = settings.minDelayMs;
            }
            int mx = settings.maxDelayMs;
            if (ui::SliderInt("Max Delay", &mx, 0, 500, "%dms")) {
                settings.maxDelayMs = mx;
                if (settings.maxDelayMs < settings.minDelayMs)
                    settings.minDelayMs = settings.maxDelayMs;
            }
        },
        "Options",
        [this]() {
            ui::Checkbox("Blocks Only", &settings.blocksOnly);
            ui::Checkbox("Randomization", &settings.randomization);
        });
}

void FastPlace::saveSettings(json::Value& node) const {
    node["blocksOnly"] = settings.blocksOnly;
    node["minDelayMs"] = settings.minDelayMs;
    node["maxDelayMs"] = settings.maxDelayMs;
    node["randomization"] = settings.randomization;
}

void FastPlace::loadSettings(const json::Value& node) {
    settings.blocksOnly = node["blocksOnly"].as_bool(settings.blocksOnly);
    settings.minDelayMs = node["minDelayMs"].as_int(settings.minDelayMs);
    settings.maxDelayMs = node["maxDelayMs"].as_int(settings.maxDelayMs);
    settings.randomization = node["randomization"].as_bool(settings.randomization);
}
