#include "criticals.h"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

Criticals* criticals = nullptr;

Criticals::Criticals() : Module("Criticals") {
    keybind = 0;
}

void Criticals::onEnable() {
    lastHurtTime = 0;
    hitTimeMs = 0;
    isExtendingAirTime = false;
}

void Criticals::onDisable() {
    isExtendingAirTime = false;
}

bool Criticals::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    // Axes: 271, 275, 258, 279, 286
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 || itemId == 271 ||
            itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

bool Criticals::checkConditions() {
    if (settings.mousePressed) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            return false;
    }

    if (settings.weaponOnly && !isHoldingWeapon())
        return false;

    if (settings.lookingAtPlayer && !mc.isTargetingEntity())
        return false;

    if (settings.movingOnly) {
        if (!(GetAsyncKeyState('W') & 0x8000))
            return false;
    }

    return true;
}

void Criticals::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    int currentHurtTime = player.getHurtTime();

    // Detect hit (hurtTime goes from 0 to max on hit)
    bool justGotHit = currentHurtTime > 0 && currentHurtTime > lastHurtTime;

    if (justGotHit) {
        // Check conditions before activating
        if (checkConditions()) {
            hitTimeMs = GetTickCount();
            isExtendingAirTime = true;
        }
    }

    // Subtle air time extension - extremely delicate to avoid detection
    if (isExtendingAirTime) {
        unsigned long elapsed = GetTickCount() - hitTimeMs;

        // Wait 80ms before applying (more natural delay after hit)
        // And only within the configured window
        if (elapsed > 80 && elapsed < (unsigned long)settings.duration) {
            // Only when in air
            if (!player.isOnGround()) {
                double motionY = player.getMotionY();

                // Tighter apex detection - only at the very top of the arc
                // When motionY is very close to 0 (transitioning from up to down)
                if (motionY > -0.04 && motionY < 0.04) {
                    // Calculate how far into the effect we are (0.0 to 1.0)
                    float progress = (float)(elapsed - 80) / (float)(settings.duration - 80);

                    // Gradual effect - starts weak, peaks in middle, fades out
                    // Bell curve: stronger in middle of duration
                    float intensity = sinf(progress * 3.14159f);

                    // Very small base adjustment (0.3% to 1%) based on intensity
                    double baseReduction = 0.997 + (0.007 * (1.0 - intensity));

                    // Add natural randomization - varies each frame
                    unsigned long frameRand = GetTickCount() ^ (GetTickCount() >> 3);
                    double randomFactor = 1.0;

                    // Only apply effect ~60% of frames for more natural look
                    if ((frameRand % 10) < 6) {
                        // Tiny random variation (±0.2%)
                        double randomOffset = ((frameRand % 5) - 2) * 0.001;
                        randomFactor = baseReduction + randomOffset;

                        // Clamp to safe range
                        if (randomFactor < 0.993)
                            randomFactor = 0.993;
                        if (randomFactor > 1.002)
                            randomFactor = 1.002;

                        double newMotionY = motionY * randomFactor;

                        // Only set if the change is meaningful but subtle
                        if (newMotionY != motionY) {
                            player.setMotionY(newMotionY);
                        }
                    }
                }
            }
        } else if (elapsed >= (unsigned long)settings.duration) {
            // Duration expired
            isExtendingAirTime = false;
        }
    }

    lastHurtTime = currentHurtTime;
}

void Criticals::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();

            // Delay slider with ms suffix
            int delay = settings.duration;
            if (ui::SliderInt("Delay", &delay, 10, 300, "%d ms"))
                settings.duration = delay;
        },
        "Conditions",
        [this]() {
            ui::Checkbox("Mouse Pressed", &settings.mousePressed);
            ui::Checkbox("Weapon Only", &settings.weaponOnly);
            ui::Checkbox("Looking At Player", &settings.lookingAtPlayer);
            ui::Checkbox("Moving Only", &settings.movingOnly);
        });
}
