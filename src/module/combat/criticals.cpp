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

    // Subtle air time extension - only at very specific moments
    if (isExtendingAirTime) {
        unsigned long elapsed = GetTickCount() - hitTimeMs;

        // Wait 50ms before applying (more natural)
        // And only within the configured window
        if (elapsed > 50 && elapsed < (unsigned long)settings.duration) {
            // Only when in air
            if (!player.isOnGround()) {
                double motionY = player.getMotionY();

                // Only apply near the apex of the arc
                // When motionY is close to 0 (transitioning from up to down)
                if (motionY > -0.08 && motionY < 0.08) {
                    // Tiny adjustment - just 1-2% reduction
                    double adjustment = motionY * 0.98;

                    // Add randomization to avoid pattern detection
                    if ((GetTickCount() % 3) == 0) {
                        adjustment = motionY * 0.97;
                    }

                    player.setMotionY(adjustment);
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
