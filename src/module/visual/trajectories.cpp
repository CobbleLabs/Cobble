#include "trajectories.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include "../../../infra/ext/imgui/imgui.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../ui/menu/menu.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"

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
static constexpr int MAX_SIMULATION_TICKS = 300;

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
    float rightOffsetAmount = 0.4f;  // Lateral offset in blocks (to the right)
    float forwardOffset = 1.5f;      // Forward offset in blocks (line starts further away)

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
    double relPosY = -0.1 + (dirY * forwardOffset);  // Slightly below eye + forward pitch offset
    double relPosZ = rightZ + forwardZ;

    // Add starting point
    TrajectoryPoint start;
    start.relX = (float)relPosX;
    start.relY = (float)relPosY;
    start.relZ = (float)relPosZ;
    start.valid = true;
    newTrajectory.push_back(start);

    for (int tick = 0; tick < MAX_SIMULATION_TICKS; tick++) {
        // Store previous relative position for collision check
        double prevRelX = relPosX;
        double prevRelY = relPosY;
        double prevRelZ = relPosZ;

        // Smooth trajectory: add multiple sub-steps per tick for smoother curve
        const int subSteps = 4;
        double subVx = vx / subSteps;
        double subVy = vy / subSteps;
        double subVz = vz / subSteps;
        double subGravity = physics.gravity / subSteps;

        for (int sub = 0; sub < subSteps; sub++) {
            // Apply sub-step velocity
            relPosX += subVx;
            relPosY += subVy;
            relPosZ += subVz;

            // Apply sub-step gravity
            subVy -= subGravity;

            // Add intermediate point for smoother curve
            TrajectoryPoint subPoint;
            subPoint.relX = (float)relPosX;
            subPoint.relY = (float)relPosY;
            subPoint.relZ = (float)relPosZ;
            subPoint.valid = true;
            newTrajectory.push_back(subPoint);
        }

        // Update actual velocity for next tick (apply full tick physics)
        vy -= physics.gravity;
        vx *= physics.drag;
        vy *= physics.drag;
        vz *= physics.drag;

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
            // Hit a block - stop simulation
            // Keep the physics-simulated position (don't use raytrace result)
            // This ensures the endpoint matches the interpolated trajectory
            break;
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

    RenderPos renderPos = mc.getRenderPos();
    float partialTicks = mc.getPartialTicks();

    // Cache render data with previous position for interpolation
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cachedRenderX = renderPos.x;
        cachedRenderY = renderPos.y;
        cachedRenderZ = renderPos.z;
        cachedPartialTicks = partialTicks;

        // Get current position
        double newX = player.getPosX();
        double newY = player.getPosY();
        double newZ = player.getPosZ();

        // On first frame, set prev = current to avoid interpolation from 0
        if (firstFrameAfterEnable) {
            prevLocalX = newX;
            prevLocalY = newY;
            prevLocalZ = newZ;
            firstFrameAfterEnable = false;
        } else {
            // Store previous position before updating current
            prevLocalX = cachedLocalX;
            prevLocalY = cachedLocalY;
            prevLocalZ = cachedLocalZ;
        }

        cachedLocalX = newX;
        cachedLocalY = newY;
        cachedLocalZ = newZ;
        cachedYaw = player.getYaw();
        cachedPitch = player.getPitch();
        cachedSneaking = player.isSneaking();
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

    if (localTrajectory.size() < 2)
        return;

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
    polylinePoints.reserve(localTrajectory.size());

    // Add ALL trajectory points - the 3D physics naturally creates the parabola
    for (size_t i = 0; i < localTrajectory.size(); i++) {
        const auto& point = localTrajectory[i];
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
            // Use natural 3D projection - no artificial offsets
            polylinePoints.push_back(ImVec2(screenPos.x, screenPos.y));
        }
    }

    // Draw the trajectory as a single smooth polyline (anti-aliased)
    if (polylinePoints.size() >= 2) {
        draw->AddPolyline(polylinePoints.data(), (int)polylinePoints.size(), lineColor, ImDrawFlags_None,
                          settings.thickness);
    }
}

void Trajectories::onRenderMenu() {
    ui::two_column(
        "General",
        [this]() {
            ui::CheckboxBind("Enabled", &enabled, &keybind);
            ui::SliderFloat("Line Thickness", &settings.thickness, 1.0f, 5.0f, "%.1f");

            ImGui::Spacing();
            ImGui::Text("Line Color");
            ImGui::ColorEdit4("##linecolor", settings.lineColor,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
        },
        "Projectiles",
        [this]() {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 2));
            ImGui::Checkbox("Bow", &settings.showBow);
            ImGui::Checkbox("Ender Pearl", &settings.showEnderPearl);
            ImGui::Checkbox("Egg", &settings.showEgg);
            ImGui::Checkbox("Snowball", &settings.showSnowball);
            ImGui::Checkbox("Healing II", &settings.showPotionHealing);
            ImGui::Checkbox("Poison", &settings.showPotionPoison);
            ImGui::Checkbox("Slowness", &settings.showPotionSlowness);
            ImGui::PopStyleVar();
        });
}
