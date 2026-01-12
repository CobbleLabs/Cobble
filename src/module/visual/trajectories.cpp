#include "trajectories.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../ui/menu/menu.h"
#include "../../../infra/ext/imgui/imgui.h"
#include <cmath>
#include <algorithm>
#include <Windows.h>

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

Trajectories::Trajectories() : Module("Trajectories")
{
	keybind = 0;
}

void Trajectories::onEnable()
{
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

void Trajectories::onDisable()
{
	log_ok("Trajectories disabled");
	lastWorldRef = nullptr;
	
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedTrajectory.clear();
}

void Trajectories::checkWorldChange()
{
	if (!Java::Env || !Java::Initialized) return;
	
	jobject currentWorld = mc.getWorld();
	bool worldChanged = false;
	
	if (lastWorldRef == nullptr && currentWorld != nullptr) worldChanged = true;
	else if (lastWorldRef != nullptr && currentWorld == nullptr) worldChanged = true;
	else if (lastWorldRef != nullptr && currentWorld != nullptr) {
		if (!Java::Env->IsSameObject(lastWorldRef, currentWorld)) worldChanged = true;
	}
	
	if (worldChanged) {
		{
			std::lock_guard<std::mutex> lock(cacheMutex);
			cachedTrajectory.clear();
		}
		
		RenderData::initialized = false;
		RenderData::Reset();
		
		if (lastWorldRef) {
			Java::Env->DeleteGlobalRef(lastWorldRef);
			lastWorldRef = nullptr;
		}
		
		if (currentWorld) {
			lastWorldRef = Java::Env->NewGlobalRef(currentWorld);
		}
	}
	
	if (currentWorld) Java::Env->DeleteLocalRef(currentWorld);
}

bool Trajectories::isThrowableItem(int itemId)
{
	switch (itemId) {
		case ITEM_ENDER_PEARL:
		case ITEM_EGG:
		case ITEM_SNOWBALL:
		case ITEM_BOW:
		case ITEM_SPLASH_POTION:
		case ITEM_FISHING_ROD:
		case ITEM_EXP_BOTTLE:
			return true;
		default:
			return false;
	}
}

Trajectories::ProjectileType Trajectories::getProjectileType(int itemId, float bowCharge)
{
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
			// Splash potion physics - adjusted to match actual Minecraft throw distance
			// Pots fly farther than other throwables
			type.gravity = 0.04f;       // Lower gravity = longer arc
			type.initialSpeed = 0.6f;   // Higher speed = lands farther
			type.drag = 0.99f;
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

float Trajectories::getBowChargeLevel()
{
	if (bowChargeStartTime == 0) return 0.0f;
	
	// Bow takes 1000ms (20 ticks) to fully charge
	long long chargeTime = GetTickCount64() - bowChargeStartTime;
	float charge = (float)chargeTime / 1000.0f;
	return std::min(1.0f, charge);
}

void Trajectories::simulateTrajectory()
{
	if (!player.isValid()) return;
	if (Menu::isOpen) return;
	
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
	float yawRad = yaw * 0.017453292f;   // PI / 180
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
	
	// Simulate trajectory with many points for smooth curve
	// Store points RELATIVE to eye position for smooth rendering
	std::vector<TrajectoryPoint> newTrajectory;
	
	// Start position relative to eye (0,0,0)
	double relPosX = 0;
	double relPosY = 0;
	double relPosZ = 0;
	
	// Add starting point (at eye position = 0,0,0 relative)
	TrajectoryPoint start;
	start.relX = 0;
	start.relY = 0;
	start.relZ = 0;
	start.valid = true;
	newTrajectory.push_back(start);
	
	for (int tick = 0; tick < MAX_SIMULATION_TICKS; tick++) {
		// Store previous relative position for collision check
		double prevRelX = relPosX;
		double prevRelY = relPosY;
		double prevRelZ = relPosZ;
		
		// Apply velocity to relative position
		relPosX += vx;
		relPosY += vy;
		relPosZ += vz;
		
		// Apply gravity (affects Y velocity)
		vy -= physics.gravity;
		
		// Apply air resistance (drag)
		vx *= physics.drag;
		vy *= physics.drag;
		vz *= physics.drag;
		
		// Add point to trajectory (relative coordinates)
		TrajectoryPoint point;
		point.relX = (float)relPosX;
		point.relY = (float)relPosY;
		point.relZ = (float)relPosZ;
		point.valid = true;
		newTrajectory.push_back(point);
		
		// Check for block collision using raytrace (need absolute coords for raytrace)
		double absFromX = startX + prevRelX;
		double absFromY = startY + prevRelY;
		double absFromZ = startZ + prevRelZ;
		double absToX = startX + relPosX;
		double absToY = startY + relPosY;
		double absToZ = startZ + relPosZ;
		
		Minecraft::Vec3 from = { (float)absFromX, (float)absFromY, (float)absFromZ };
		Minecraft::Vec3 to = { (float)absToX, (float)absToY, (float)absToZ };
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
		if (absY < -64 || absY > 320) break;
		
		if (sqrt(relPosX * relPosX + relPosZ * relPosZ) > 150.0) break;
	}
	
	// Update cache thread-safely
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cachedTrajectory = std::move(newTrajectory);
	}
}

void Trajectories::onUpdate()
{
	lastUpdateTick = GetTickCount64();
	
	if (!player.isValid()) {
		std::lock_guard<std::mutex> lock(cacheMutex);
		cachedTrajectory.clear();
		return;
	}
	
	checkWorldChange();
	RenderData::Update();
	
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

void Trajectories::onRender()
{
	if (!enabled) return;
	if (!RenderData::updated) return;
	if (Menu::isOpen) return;
	
	// Prevent rendering if game loop is paused
	if (GetTickCount64() - lastUpdateTick > 300) return;
	
	ImDrawList* draw = ImGui::GetBackgroundDrawList();
	if (!draw) return;
	
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
	
	if (localTrajectory.size() < 2) return;
	
	// Interpolate player position using partial ticks for smooth rendering
	// This eliminates jitter when moving
	double interpX = prevX + (localX - prevX) * partialTicks;
	double interpY = prevY + (localY - prevY) * partialTicks;
	double interpZ = prevZ + (localZ - prevZ) * partialTicks;
	// Use eye height for world position calculation
	eyeHeight = isSneaking ? 1.54f : 1.62f;
	
	// Get line color
	ImU32 lineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
		settings.lineColor[0],
		settings.lineColor[1],
		settings.lineColor[2],
		settings.lineColor[3]
	));
	
	// Get screen center (crosshair position)
	float screenCenterX = RenderData::screenWidth / 2.0f;
	float screenCenterY = RenderData::screenHeight / 2.0f;
	
	// Collect screen points for polyline rendering (smoother than individual segments)
	std::vector<ImVec2> polylinePoints;
	polylinePoints.reserve(localTrajectory.size() + 1);
	
	// Start from crosshair (screen center)
	polylinePoints.push_back(ImVec2(screenCenterX, screenCenterY));
	
	Vector2 lastValidScreen;
	bool hasLastValid = false;
	
	// Add trajectory points starting from index 1 (skip origin)
	for (size_t i = 1; i < localTrajectory.size(); i++) {
		const auto& point = localTrajectory[i];
		if (!point.valid) continue;
		
		// Convert relative position to world position using interpolated player pos
		double worldX = interpX + point.relX;
		double worldY = interpY + eyeHeight + point.relY;
		double worldZ = interpZ + point.relZ;
		
		Vector3 cameraRelPos{
			(float)(worldX - renderX),
			(float)(worldY - renderY),
			(float)(worldZ - renderZ)
		};
		
		// Project to screen
		Vector2 screenPos;
		bool visible = CWorldToScreen::WorldToScreen(cameraRelPos, RenderData::modelView, RenderData::projection,
		                                              RenderData::screenWidth, RenderData::screenHeight, screenPos);
		
		if (visible) {
			polylinePoints.push_back(ImVec2(screenPos.x, screenPos.y));
			lastValidScreen = screenPos;
			hasLastValid = true;
		}
	}
	
	// Draw the trajectory as a single smooth polyline (anti-aliased)
	if (polylinePoints.size() >= 2) {
		draw->AddPolyline(polylinePoints.data(), (int)polylinePoints.size(), lineColor, 
		                  ImDrawFlags_None, settings.thickness);
	}
	
	// Draw circle at the landing point (last point of trajectory)
	if (hasLastValid) {
		ImU32 circleColor = ImGui::ColorConvertFloat4ToU32(ImVec4(
			settings.circleColor[0],
			settings.circleColor[1],
			settings.circleColor[2],
			settings.circleColor[3]
		));
		draw->AddCircle(
			ImVec2(lastValidScreen.x, lastValidScreen.y),
			settings.circleRadius,
			circleColor,
			32,  // Increased segments for smoother circle
			settings.circleThickness
		);
	}
}

void Trajectories::onRenderMenu()
{
	ui::two_column(
		"General", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ui::SliderFloat("Line Thickness", &settings.thickness, 1.0f, 5.0f, "%.1f");
			ui::SliderFloat("Circle Radius", &settings.circleRadius, 5.0f, 50.0f, "%.0f");
			ui::SliderFloat("Circle Thickness", &settings.circleThickness, 1.0f, 5.0f, "%.1f");
		},
		"Colors", [this]() {
			ImGui::Text("Line Color");
			ImGui::ColorEdit4("##linecolor", settings.lineColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
			ImGui::Spacing();
			ImGui::Text("Circle Color");
			ImGui::ColorEdit4("##circlecolor", settings.circleColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
		}
	);
}
