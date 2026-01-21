#include "sprint.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"

#include <cmath>

Sprint* sprint = nullptr;

Sprint::Sprint() : Module("Sprint") {
    keybind = 0;
}

void Sprint::onEnable() {
    log_debug("[Sprint] Module enabled");
}

void Sprint::onDisable() {
    log_debug("[Sprint] Module disabled");
}

void Sprint::onUpdate() {
    if (!enabled)
        return;
    if (Menu::isOpen)
        return;
    if (mc.isInGui())
        return;
    if (!player.isValid())
        return;

    // Don't sprint while sneaking - matches vanilla behavior
    if (player.isSneaking())
        return;
    
    // Don't sprint while using items (eating/drinking potions)
    if (player.isUsingItem())
        return;

    // Get player motion
    double motionX = player.getMotionX();
    double motionZ = player.getMotionZ();
    
    // Check if player has horizontal movement
    double horizontalSpeed = std::sqrt(motionX * motionX + motionZ * motionZ);
    
    // Minimum threshold for movement (very small to catch any movement)
    // Player walking speed is ~0.1, sprinting is ~0.13
    constexpr double MIN_MOVEMENT = 0.01;
    
    if (horizontalSpeed < MIN_MOVEMENT)
        return;
    
    // Calculate movement direction relative to player's facing
    // This determines if we're moving "forward" relative to where we're looking
    float yaw = player.getYaw();
    double yawRad = yaw * 3.14159265358979323846 / 180.0;
    
    // Forward direction vector based on yaw
    double forwardX = -std::sin(yawRad);
    double forwardZ = std::cos(yawRad);
    
    // Normalize motion vector
    double motionNormX = motionX / horizontalSpeed;
    double motionNormZ = motionZ / horizontalSpeed;
    
    // Dot product: positive = moving forward, negative = moving backward
    double forwardDot = motionNormX * forwardX + motionNormZ * forwardZ;
    
    // Only sprint if moving at least partially forward (dot > 0)
    // Lunar's toggle sprint activates even with slight forward component
    if (forwardDot > 0.1) {
        // Force sprint every tick - this is what makes Toggle Sprint work
        // Unlike vanilla which can interrupt sprint, we keep forcing it
        player.setSprinting(true);
    }
}

void Sprint::onRenderMenu() {
    ui::two_column(
        "General", [this]() { ui::CheckboxBind("Enabled", &enabled, &keybind); }, "Info",
        [this]() { ImGui::TextWrapped("Automatically sprints when moving forward. Works like Lunar Client's Toggle Sprint."); });
}
