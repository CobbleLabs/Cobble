#define IMGUI_DEFINE_MATH_OPERATORS
#include "trajectories.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/jni/jni_cache.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/network/json.hpp"
#include "../../sdk/player/visual_cache.h"

Trajectories* trajectories = nullptr;

// Minecraft item IDs for throwable projectiles
static constexpr int ITEM_ENDER_PEARL = 368;
static constexpr int ITEM_EGG = 344;
static constexpr int ITEM_SNOWBALL = 332;
static constexpr int ITEM_BOW = 261;
static constexpr int ITEM_SPLASH_POTION = 373;
static constexpr int ITEM_FISHING_ROD = 346;
static constexpr int ITEM_EXP_BOTTLE = 384;

// Max simulation ticks (internal, not exposed to UI)
static constexpr int MAX_SIMULATION_TICKS = 240;

Trajectories::Trajectories() : Module("Trajectories") {
    keybind = 0;
    renders = true;
}

void Trajectories::onEnable() {
    log_ok("Trajectories enabled");
    lastWorldRef = nullptr;

    RenderData::initialized = false;
    RenderData::Reset();

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedTrajectory.clear();
    renderedTrajectory.clear();
    lastRenderTime = 0;

    // Initialize previous positions to avoid invalid interpolation on first frame
    prevLocalX = cachedLocalX = 0;
    prevLocalY = cachedLocalY = 0;
    prevLocalZ = cachedLocalZ = 0;
    firstFrameAfterEnable = true;
}

void Trajectories::onDisable() {
    log_ok("Trajectories disabled");
    lastWorldRef = nullptr;

    std::lock_guard<std::mutex> lock(cacheMutex);
    cachedTrajectory.clear();
    renderedTrajectory.clear();
    lastRenderTime = 0;
}

void Trajectories::checkWorldChange() {
    if (!Java::Env() || !Java::Initialized)
        return;

    jobject currentWorld = mc.getWorld();
    bool worldChanged = false;

    if (lastWorldRef == nullptr && currentWorld != nullptr)
        worldChanged = true;
    else if (lastWorldRef != nullptr && currentWorld == nullptr)
        worldChanged = true;
    else if (lastWorldRef != nullptr && currentWorld != nullptr) {
        if (!Java::Env()->IsSameObject(lastWorldRef, currentWorld))
            worldChanged = true;
    }

    if (worldChanged) {
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            cachedTrajectory.clear();
        }

        RenderData::initialized = false;
        RenderData::Reset();

        if (lastWorldRef) {
            Java::Env()->DeleteGlobalRef(lastWorldRef);
            lastWorldRef = nullptr;
        }

        if (currentWorld) {
            lastWorldRef = Java::Env()->NewGlobalRef(currentWorld);
        }
    }

    if (currentWorld)
        Java::Env()->DeleteLocalRef(currentWorld);
}

bool Trajectories::isThrowableItem(int itemId) {
    switch (itemId) {
        case ITEM_ENDER_PEARL:
            return settings.showEnderPearl;
        case ITEM_EGG:
            return settings.showEgg;
        case ITEM_SNOWBALL:
            return settings.showSnowball;
        case ITEM_BOW:
            return settings.showBow;
        case ITEM_SPLASH_POTION: {
            // Check specific potion type based on metadata
            int potionMeta = player.getHeldItemDamage();

            // Minecraft 1.8 potion metadata:
            // Bit 14 (value 16384) = Splash potion flag
            // If this bit is NOT set, it's a drinkable potion - don't show trajectory
            bool isSplash = (potionMeta & 0x4000) != 0;  // 0x4000 = 16384 = bit 14
            if (!isSplash)
                return false;  // Drinkable potion, no trajectory

            // The metadata value directly encodes the potion type
            // Common splash potion metadata values:
            // - Healing: 16389, 16421 (& 0x3F gives 5 or 21)
            // - Healing II: 16421 (& 0x3F gives 21)
            // - Poison: 16388, 16420 (& 0x3F gives 4 or 20)
            // - Slowness: 16394, 16426 (& 0x3F gives 10 or 26)

            int effectId = potionMeta & 0x3F;  // Lower 6 bits contain effect info

            // Healing potions: effect IDs 5, 6, 21, 22 (Instant Health I/II variants)
            bool isHealing = (effectId == 5 || effectId == 6 || effectId == 21 || effectId == 22 || effectId == 37 ||
                              effectId == 38);

            // Poison potions: effect IDs 4, 20, 36 (Poison variants)
            bool isPoison = (effectId == 4 || effectId == 20 || effectId == 36 || effectId == 52);

            // Slowness potions: effect IDs 2, 10, 18, 26, 34, 42 (Slowness variants)
            bool isSlowness = (effectId == 2 || effectId == 10 || effectId == 18 || effectId == 26 || effectId == 34 ||
                               effectId == 42);

            if (isHealing && settings.showPotionHealing)
                return true;
            if (isPoison && settings.showPotionPoison)
                return true;
            if (isSlowness && settings.showPotionSlowness)
                return true;

            return false;
        }
        case ITEM_FISHING_ROD:
            return false;  // Not commonly used, disabled
        case ITEM_EXP_BOTTLE:
            return settings.showExpBottle;
        default:
            return false;
    }
}

Trajectories::ProjectileType Trajectories::getProjectileType(int itemId, float bowCharge) {
    ProjectileType type;

    switch (itemId) {
        case ITEM_BOW:
            // Arrow physics - speed scales with bow charge (0.0 to 3.0)
            // In Minecraft, fully charged = 3.0 velocity, minimum = ~0.3
            type.gravity = 0.05f;
            type.initialSpeed = 0.3f + (bowCharge * 2.7f);  // 0.3 to 3.0 based on charge
            type.drag = 0.99f;
            break;

        case ITEM_ENDER_PEARL:
            // Ender pearl has specific physics
            type.gravity = 0.03f;
            type.initialSpeed = 1.5f;
            type.drag = 0.99f;
            break;

        case ITEM_EGG:
        case ITEM_SNOWBALL:
        case ITEM_EXP_BOTTLE:
            // Standard throwable physics
            type.gravity = 0.03f;
            type.initialSpeed = 1.5f;
            type.drag = 0.99f;
            break;

        case ITEM_SPLASH_POTION:
            // Splash potion physics - accurate Minecraft values
            // EntityPotion uses 0.4 velocity multiplier with extra upward boost
            // Gravity: 0.05 blocks/tick², Drag: 0.99 per tick
            type.gravity = 0.05f;      // Same as arrows/most entities
            type.initialSpeed = 0.4f;  // Potion throw speed multiplier
            type.drag = 0.99f;
            type.upwardAngle = 20.0f;  // Positive = goes more upward (pitch - 20)
            break;

        case ITEM_FISHING_ROD:
            // Fishing bobber
            type.gravity = 0.04f;
            type.initialSpeed = 1.5f;
            type.drag = 0.92f;
            break;

        default:
            type.gravity = 0.03f;
            type.initialSpeed = 1.5f;
            type.drag = 0.99f;
            break;
    }

    return type;
}

float Trajectories::getBowChargeLevel() {
    if (bowChargeStartTime == 0)
        return 0.0f;

    // Bow takes 1000ms (20 ticks) to fully charge
    long long chargeTime = GetTickCount64() - bowChargeStartTime;
    float charge = (float)chargeTime / 1000.0f;
    return (std::min)(1.0f, charge);
}

void Trajectories::simulateTrajectory() {
    if (!player.isValid())
        return;
    if (Menu::isOpen)
        return;

    // Get held item ID using the SDK function
    int heldItemId = player.getHeldItemId();
    cachedHeldItemId = heldItemId;

    // Check if holding a throwable item
    if (!isThrowableItem(heldItemId)) {
        // Reset bow charge when not holding throwable
        bowChargeStartTime = 0;
        wasBowCharging = false;
        cachedBowCharge = 0.0f;

        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedTrajectory.clear();
        return;
    }

    // Bow-specific logic: only show trajectory while charging
    if (heldItemId == ITEM_BOW) {
        bool isRightClickHeld = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;

        if (isRightClickHeld && !mc.isInGui()) {
            // Started charging or continuing to charge
            if (!wasBowCharging) {
                bowChargeStartTime = GetTickCount64();
                wasBowCharging = true;
            }
            cachedBowCharge = getBowChargeLevel();
        } else {
            // Not charging - clear trajectory
            bowChargeStartTime = 0;
            wasBowCharging = false;
            cachedBowCharge = 0.0f;

            std::lock_guard<std::mutex> lock(cacheMutex);
            cachedTrajectory.clear();
            return;
        }
    } else {
        // Non-bow items don't need charge tracking
        cachedBowCharge = 1.0f;
    }

    // Get projectile physics (pass bow charge for arrows)
    ProjectileType physics = getProjectileType(heldItemId, cachedBowCharge);

    // Get player eye position for trajectory simulation
    float eyeHeight = player.isSneaking() ? 1.54f : 1.62f;
    double startX = player.getPosX();
    double startY = player.getPosY() + eyeHeight;
    double startZ = player.getPosZ();

    // Use player's current look direction (this IS the crosshair direction)
    float yaw = player.getYaw();
    float pitch = player.getPitch();

    // Apply upward angle offset for items like potions (makes arc more curved)
    // Subtracting from pitch makes projectile go more upward (negative pitch = looking up)
    pitch -= physics.upwardAngle;

    // Convert degrees to radians
    float yawRad = yaw * 0.017453292f;  // PI / 180
    float pitchRad = pitch * 0.017453292f;

    // Calculate initial velocity vector from look direction
    // This matches how Minecraft calculates projectile direction
    float dirX = -sinf(yawRad) * cosf(pitchRad);
    float dirY = -sinf(pitchRad);
    float dirZ = cosf(yawRad) * cosf(pitchRad);

    // Apply initial speed
    float vx = dirX * physics.initialSpeed;
    float vy = dirY * physics.initialSpeed;
    float vz = dirZ * physics.initialSpeed;

    // Calculate right-hand offset (perpendicular to look direction in XZ plane)
    // This simulates the projectile coming from the player's right hand
    float rightOffsetAmount = 0.16f;
    float forwardOffset = 0.18f;

    // Right vector is perpendicular to look direction (negated for right side)
    float rightX = -cosf(yawRad) * rightOffsetAmount;
    float rightZ = -sinf(yawRad) * rightOffsetAmount;

    // Forward vector (same as look direction in XZ plane)
    float forwardX = dirX * forwardOffset;
    float forwardZ = dirZ * forwardOffset;

    // Simulate trajectory with many points for smooth curve
    // Store points RELATIVE to eye position for smooth rendering
    std::vector<TrajectoryPoint> newTrajectory;

    // Start position with right-hand offset + forward offset
    double relPosX = rightX + forwardX;
    double relPosY = -0.12 + (dirY * forwardOffset);
    double relPosZ = rightZ + forwardZ;

    // Add starting point
    TrajectoryPoint start;
    start.relX = (float)relPosX;
    start.relY = (float)relPosY;
    start.relZ = (float)relPosZ;
    start.valid = true;
    start.hitBlock = false;
    newTrajectory.push_back(start);

    const auto collisionSnapshot = VisualCache::GetSnapshot();

    for (int tick = 0; tick < MAX_SIMULATION_TICKS; tick++) {
        // Store previous relative position for collision check
        double prevRelX = relPosX;
        double prevRelY = relPosY;
        double prevRelZ = relPosZ;

        // Smooth trajectory using correct Verlet-style physics integration
        // Each substep applies proportional physics for true parabolic curve
        const int subSteps = 4;
        double subDrag = pow(physics.drag, 1.0 / subSteps);  // Fractional drag per substep
        double subGravity = physics.gravity / subSteps;

        for (int sub = 0; sub < subSteps; sub++) {
            // Apply velocity first (position update)
            relPosX += vx / subSteps;
            relPosY += vy / subSteps;
            relPosZ += vz / subSteps;

            // Update velocity for next substep (gravity + drag)
            vy -= (float)subGravity;
            vx *= (float)subDrag;
            vy *= (float)subDrag;
            vz *= (float)subDrag;

            // Add intermediate point for smoother curve
            TrajectoryPoint subPoint;
            subPoint.relX = (float)relPosX;
            subPoint.relY = (float)relPosY;
            subPoint.relZ = (float)relPosZ;
            subPoint.valid = true;
            subPoint.hitBlock = false;
            newTrajectory.push_back(subPoint);
        }

        // Check for block collision using raytrace (need absolute coords for raytrace)
        double absFromX = startX + prevRelX;
        double absFromY = startY + prevRelY;
        double absFromZ = startZ + prevRelZ;
        double absToX = startX + relPosX;
        double absToY = startY + relPosY;
        double absToZ = startZ + relPosZ;

        Minecraft::Vec3 from = {(float)absFromX, (float)absFromY, (float)absFromZ};
        Minecraft::Vec3 to = {(float)absToX, (float)absToY, (float)absToZ};
        Minecraft::Vec3 result;

        auto rayResult = mc.rayTraceBlocks(from, to, result, false);

        if (rayResult == Minecraft::RAYTRACE_BLOCKS_RESULT::HIT ||
            rayResult == Minecraft::RAYTRACE_BLOCKS_RESULT::HIT_FROM_AIRBLOCK) {
            if (!newTrajectory.empty()) {
                newTrajectory.back().relX = result.x - (float)startX;
                newTrajectory.back().relY = result.y - (float)startY;
                newTrajectory.back().relZ = result.z - (float)startZ;
                newTrajectory.back().hitBlock = true;
            }
            break;
        }

        // Check player collision using AABB intersection
        if (collisionSnapshot.valid) {
            bool hitAnyPlayer = false;
            for (const auto& p : collisionSnapshot.players) {
                // Player hitbox: 0.6 wide, 1.8 tall centered on position
                float halfWidth = 0.3f;
                float height = 1.8f;

                float minX = (float)p.posX - halfWidth;
                float maxX = (float)p.posX + halfWidth;
                float minY = (float)p.posY;
                float maxY = (float)p.posY + height;
                float minZ = (float)p.posZ - halfWidth;
                float maxZ = (float)p.posZ + halfWidth;

                // Check if current trajectory point is inside player AABB
                if (absToX >= minX && absToX <= maxX && absToY >= minY && absToY <= maxY && absToZ >= minZ &&
                    absToZ <= maxZ) {
                    hitAnyPlayer = true;
                    break;
                }
            }

            if (hitAnyPlayer) {
                if (!newTrajectory.empty()) {
                    newTrajectory.back().hitPlayer = true;
                }
                break;
            }
        }

        // Stop if below world or very far (using absolute Y)
        double absY = startY + relPosY;
        if (absY < -64 || absY > 320)
            break;

        if (sqrt(relPosX * relPosX + relPosZ * relPosZ) > 150.0)
            break;
    }

    // Update cache thread-safely
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedTrajectory = std::move(newTrajectory);
    }
}

void Trajectories::onUpdate() {
    lastUpdateTick = GetTickCount64();

    if (!player.isValid()) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedTrajectory.clear();
        return;
    }

    checkWorldChange();

    const auto snap = VisualCache::GetSnapshot();
    if (!snap.valid) {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedTrajectory.clear();
        return;
    }

    // Cache render data with previous position for interpolation
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedRenderX = snap.renderPos.x;
        cachedRenderY = snap.renderPos.y;
        cachedRenderZ = snap.renderPos.z;
        cachedPartialTicks = snap.partialTicks;
        prevLocalX = snap.localLastX;
        prevLocalY = snap.localLastY;
        prevLocalZ = snap.localLastZ;
        cachedLocalX = snap.localX;
        cachedLocalY = snap.localY;
        cachedLocalZ = snap.localZ;
        cachedYaw = player.getYaw();
        cachedPitch = player.getPitch();
        cachedSneaking = player.isSneaking();
        firstFrameAfterEnable = false;
    }

    // Simulate trajectory each tick
    simulateTrajectory();
}

void Trajectories::onRender() {
    if (!enabled)
        return;
    if (!RenderData::updated)
        return;
    if (Menu::isOpen)
        return;

    // Prevent rendering if game loop is paused
    if (GetTickCount64() - lastUpdateTick > 300)
        return;

    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    if (!draw)
        return;

    std::vector<TrajectoryPoint> localTrajectory;
    double renderX, renderY, renderZ;
    double localX, localY, localZ;
    double prevX, prevY, prevZ;
    float partialTicks;
    float eyeHeight;
    bool isSneaking;

    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        localTrajectory = cachedTrajectory;
        renderX = cachedRenderX;
        renderY = cachedRenderY;
        renderZ = cachedRenderZ;
        localX = cachedLocalX;
        localY = cachedLocalY;
        localZ = cachedLocalZ;
        prevX = prevLocalX;
        prevY = prevLocalY;
        prevZ = prevLocalZ;
        partialTicks = cachedPartialTicks;
        isSneaking = cachedSneaking;
    }

    if (localTrajectory.size() < 2) {
        renderedTrajectory.clear();
        return;
    }

    const long long now = GetTickCount64();
    const float frameSeconds =
        lastRenderTime > 0 ? (std::min)(0.05f, (float)(now - lastRenderTime) / 1000.0f) : 1.0f / 60.0f;
    lastRenderTime = now;
    const float trajectoryAlpha = 1.0f - std::exp(-18.0f * frameSeconds);

    const size_t oldSize = renderedTrajectory.size();
    renderedTrajectory.resize(localTrajectory.size());
    for (size_t i = 0; i < localTrajectory.size(); ++i) {
        const auto& target = localTrajectory[i];
        auto& rendered = renderedTrajectory[i];
        if (i >= oldSize || !rendered.valid) {
            rendered = target;
            continue;
        }
        rendered.relX += (target.relX - rendered.relX) * trajectoryAlpha;
        rendered.relY += (target.relY - rendered.relY) * trajectoryAlpha;
        rendered.relZ += (target.relZ - rendered.relZ) * trajectoryAlpha;
        rendered.valid = target.valid;
        rendered.hitBlock = target.hitBlock;
        rendered.hitPlayer = target.hitPlayer;
    }

    // Interpolate player position using partial ticks for smooth rendering
    // This eliminates jitter when moving
    double interpX = prevX + (localX - prevX) * partialTicks;
    double interpY = prevY + (localY - prevY) * partialTicks;
    double interpZ = prevZ + (localZ - prevZ) * partialTicks;
    // Use eye height for world position calculation
    eyeHeight = isSneaking ? 1.54f : 1.62f;

    // Get line color
    ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(settings.lineColor[0], settings.lineColor[1], settings.lineColor[2], settings.lineColor[3]));

    // Get screen center (for reference, not used for starting point)
    // float screenCenterX = RenderData::screenWidth / 2.0f;
    // float screenCenterY = RenderData::screenHeight / 2.0f;

    // Collect screen points for polyline rendering (smoother than individual segments)
    std::vector<ImVec2> polylinePoints;
    polylinePoints.reserve(renderedTrajectory.size());

    // Track hit point for marker
    ImVec2 hitMarkerPos;
    bool hasHitMarker = false;

    const auto flushSegment = [&]() {
        if (polylinePoints.size() >= 2) {
            draw->AddPolyline(polylinePoints.data(), (int)polylinePoints.size(), IM_COL32(0, 0, 0, 170),
                              ImDrawFlags_None, settings.thickness + 2.5f);
            draw->AddPolyline(polylinePoints.data(), (int)polylinePoints.size(), lineColor, ImDrawFlags_None,
                              settings.thickness);
        }
        polylinePoints.clear();
    };

    for (const auto& point : renderedTrajectory) {
        if (!point.valid)
            continue;

        // Convert relative position to world position using interpolated player pos
        double worldX = interpX + point.relX;
        double worldY = interpY + eyeHeight + point.relY;
        double worldZ = interpZ + point.relZ;

        Vector3 cameraRelPos{(float)(worldX - renderX), (float)(worldY - renderY), (float)(worldZ - renderZ)};

        // Project to screen
        Vector2 screenPos;
        bool visible = CWorldToScreen::WorldToScreen(cameraRelPos, RenderData::modelView, RenderData::projection,
                                                     RenderData::screenWidth, RenderData::screenHeight, screenPos);

        if (visible) {
            const ImVec2 projected(screenPos.x, screenPos.y);
            if (polylinePoints.empty()) {
                polylinePoints.push_back(projected);
            } else {
                const float dx = projected.x - polylinePoints.back().x;
                const float dy = projected.y - polylinePoints.back().y;
                if (dx * dx + dy * dy >= 1.0f || point.hitBlock || point.hitPlayer)
                    polylinePoints.push_back(projected);
            }

            if (point.hitBlock && !hasHitMarker) {
                hitMarkerPos = projected;
                hasHitMarker = true;
            }
        } else {
            flushSegment();
        }
    }

    flushSegment();

    // Draw hit marker only for block hits (not player hits)
    if (hasHitMarker) {
        const float markerRadius = (std::max)(3.5f, settings.thickness * 2.2f);
        draw->AddCircleFilled(hitMarkerPos, markerRadius + 2.0f, IM_COL32(0, 0, 0, 170), 16);
        draw->AddCircleFilled(hitMarkerPos, markerRadius, lineColor, 16);
        draw->AddCircle(hitMarkerPos, markerRadius + 3.5f, lineColor, 16, 1.0f);
    }
}

void Trajectories::onRenderMenu() {
    bool enabledValue = enabled;
    ui::two_column(
        "General",
        [this, &enabledValue]() {
            if (ui::CheckboxBind("Enabled", &enabledValue, &keybind)) {
                if (enabledValue != enabled) {
                    setEnabled(enabledValue);
                }
            }
            ui::SliderFloat("Line Thickness", &settings.thickness, 1.0f, 5.0f, "%.1f");

            ImGui::Spacing();
            ui::color("Line Color", settings.lineColor);
        },
        "Projectiles",
        [this]() {
            ui::Checkbox("Bow", &settings.showBow);
            ui::Checkbox("Ender Pearl", &settings.showEnderPearl);
            ui::Checkbox("Egg", &settings.showEgg);
            ui::Checkbox("Snowball", &settings.showSnowball);
            ui::Checkbox("Healing II", &settings.showPotionHealing);
            ui::Checkbox("Poison", &settings.showPotionPoison);
            ui::Checkbox("Slowness", &settings.showPotionSlowness);
        });
}



void Trajectories::saveSettings(json::Value& node) const {
    node["thickness"] = settings.thickness;
    json::writeColor(node, "lineColor", settings.lineColor);
    node["showBow"] = settings.showBow;
    node["showEnderPearl"] = settings.showEnderPearl;
    node["showEgg"] = settings.showEgg;
    node["showSnowball"] = settings.showSnowball;
    node["showPotionHealing"] = settings.showPotionHealing;
    node["showPotionPoison"] = settings.showPotionPoison;
    node["showPotionSlowness"] = settings.showPotionSlowness;
    node["showExpBottle"] = settings.showExpBottle;
}

void Trajectories::loadSettings(const json::Value& node) {
    settings.thickness = node["thickness"].as_float(settings.thickness);
    json::readColor(node, "lineColor", settings.lineColor);
    settings.showBow = node["showBow"].as_bool(settings.showBow);
    settings.showEnderPearl = node["showEnderPearl"].as_bool(settings.showEnderPearl);
    settings.showEgg = node["showEgg"].as_bool(settings.showEgg);
    settings.showSnowball = node["showSnowball"].as_bool(settings.showSnowball);
    settings.showPotionHealing = node["showPotionHealing"].as_bool(settings.showPotionHealing);
    settings.showPotionPoison = node["showPotionPoison"].as_bool(settings.showPotionPoison);
    settings.showPotionSlowness = node["showPotionSlowness"].as_bool(settings.showPotionSlowness);
    settings.showExpBottle = node["showExpBottle"].as_bool(settings.showExpBottle);
}
