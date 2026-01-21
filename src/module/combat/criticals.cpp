#include "criticals.h"
#include "../../jni/packet_interceptor.hpp"
#include "../../sdk/core/minecraft.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"

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
    isDelaying = false;
    wasAttacking = false;
    log_ok("[Criticals] Module ENABLED");
}

void Criticals::onDisable() {
    // Make sure to release packets if we were delaying
    if (isDelaying) {
        log_debug("[Criticals] Disabling while delay active - stopping delay");
        stopPacketDelay();
    }
    log_ok("[Criticals] Module DISABLED");
}

bool Criticals::isHoldingWeapon() {
    int itemId = player.getHeldItemId();
    // Swords: 268 (wood), 272 (stone), 267 (iron), 276 (diamond), 283 (gold)
    // Axes: 271, 275, 258, 279, 286
    return (itemId == 268 || itemId == 272 || itemId == 267 || itemId == 276 || itemId == 283 || 
            itemId == 271 || itemId == 275 || itemId == 258 || itemId == 279 || itemId == 286);
}

bool Criticals::shouldStartDelay() {
    // Check weapon condition
    if (settings.weaponOnly && !isHoldingWeapon()) {
        log_debug("[Criticals] Condition FAILED: Not holding weapon (weaponOnly=true)");
        return false;
    }

    // Must be looking at an entity to attack
    if (!mc.isTargetingEntity()) {
        log_debug("[Criticals] Condition FAILED: Not targeting entity");
        return false;
    }

    // Must be falling (motionY < 0) - this is when crits happen
    double motionY = player.getMotionY();
    if (motionY >= 0) {
        log_debug("[Criticals] Condition FAILED: Not falling (motionY=%.4f >= 0)", motionY);
        return false;
    }

    // Must not be on ground
    if (player.isOnGround()) {
        log_debug("[Criticals] Condition FAILED: On ground");
        return false;
    }

    log_debug("[Criticals] All conditions PASSED: targeting entity, falling (motionY=%.4f), in air", motionY);
    return true;
}

void Criticals::startPacketDelay() {
    if (isDelaying) {
        log_debug("[Criticals] startPacketDelay called but already delaying");
        return;
    }

    // Use dedicated Criticals packet delay (not Blink's)
    log_ok("[Criticals] >>> STARTING packet delay (max %d ms)", settings.maxDelay);
    PacketInterceptor::setCriticalsActive(true);
    isDelaying = true;
    delayStartTime = std::chrono::steady_clock::now();
}

void Criticals::stopPacketDelay() {
    if (!isDelaying) {
        log_debug("[Criticals] stopPacketDelay called but not delaying");
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - delayStartTime).count();

    // Release packets using Criticals-specific function
    log_ok("[Criticals] <<< STOPPING packet delay (was active for %lld ms)", elapsed);
    PacketInterceptor::setCriticalsActive(false);
    isDelaying = false;
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

    // Get current attack state
    bool isAttacking = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // Detect attack press (rising edge)
    bool attackPressed = isAttacking && !wasAttacking;

    // If we're currently delaying packets
    if (isDelaying) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - delayStartTime).count();

        // Release packets after max delay time
        if (elapsed >= settings.maxDelay) {
            log_debug("[Criticals] Max delay reached (%lld ms >= %d ms)", elapsed, settings.maxDelay);
            stopPacketDelay();
        }
        // Also release if we land on ground
        else if (player.isOnGround()) {
            log_debug("[Criticals] Landed on ground - releasing early");
            stopPacketDelay();
        }
    }

    // Start delay on attack if conditions are met
    if (attackPressed) {
        log_debug("[Criticals] Attack detected (LMB pressed)");
        if (!isDelaying) {
            if (shouldStartDelay()) {
                startPacketDelay();
            }
        } else {
            log_debug("[Criticals] Already delaying - ignoring attack");
        }
    }

    wasAttacking = isAttacking;
}

void Criticals::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ImGui::Spacing();

            // Max delay slider
            int delay = settings.maxDelay;
            if (ui::SliderInt("Max Delay", &delay, 50, 500, "%d ms"))
                settings.maxDelay = delay;

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Note:");
            ImGui::TextWrapped("High delay values may make hits feel laggy.");
        },
        "Conditions",
        [this]() { 
            ui::Checkbox("Weapon Only", &settings.weaponOnly); 
        });
}
