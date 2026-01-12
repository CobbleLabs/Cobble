#include "esp.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../util/team/friends_manager.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../../infra/ext/imgui/imgui.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

ESP* esp = nullptr;

ESP::ESP() : Module("ESP")
{
	keybind = 0;
}

void ESP::onEnable()
{
	log_ok("ESP enabled");
	lastWorldRef = nullptr;
	
	// Reset RenderData
	RenderData::initialized = false;
	RenderData::Reset();
	
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedPlayers.clear();
}

void ESP::onDisable()
{
	log_ok("ESP disabled");
	lastWorldRef = nullptr; // Cleanup handled in checkWorldChange or next enable
	
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedPlayers.clear();
}

void ESP::checkWorldChange()
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
			cachedPlayers.clear();
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

void ESP::onUpdate()
{
	lastUpdateTick = GetTickCount64();

	// checkWorldChange(); // Optional: similar to NameTags
	
	if (!player.isValid()) {
		std::lock_guard<std::mutex> lock(cacheMutex);
		cachedPlayers.clear();
		return;
	}
	
	RenderData::Update();
	
	RenderPos renderPos = mc.getRenderPos();
	float partialTicks = mc.getPartialTicks();
	
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	
	auto players = mc.getPlayerList();
	std::vector<EspPlayer> newCache;
	
	for (auto& p : players)
	{
		double dx = p.posX - localX;
		double dy = p.posY - localY;
		double dz = p.posZ - localZ;
		float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);
		
		if (dist > settings.range || dist < 0.5f) {
			if (p.instance) {
				Java::Env->DeleteLocalRef(p.instance);
				p.instance = nullptr;
			}
			continue;
		}
		
		EspPlayer cached;
		cached.name = p.name;
		cached.uuid = p.uuid;  // For team/friend detection
		cached.posX = p.posX;
		cached.posY = p.posY;
		cached.posZ = p.posZ;
		cached.lastPosX = p.lastPosX;
		cached.lastPosY = p.lastPosY;
		cached.lastPosZ = p.lastPosZ;
		cached.distance = dist;
		cached.health = p.health;
		cached.maxHealth = p.maxHealth;
		cached.valid = true;
		
		newCache.push_back(cached);
		
		if (p.instance) {
			Java::Env->DeleteLocalRef(p.instance);
			p.instance = nullptr;
		}
	}
	
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cachedPlayers = std::move(newCache);
		cachedRenderX = renderPos.x;
		cachedRenderY = renderPos.y;
		cachedRenderZ = renderPos.z;
		cachedPartialTicks = partialTicks;
		cachedLocalX = localX;
		cachedLocalY = localY;
		cachedLocalZ = localZ;
		cachedLocalLastX = player.getLastPosX();
		cachedLocalLastY = player.getLastPosY();
		cachedLocalLastZ = player.getLastPosZ();
	}
}

// Helper color function (duplicated from nametags for independence)
static ImU32 GetHealthColor(float health, float maxHealth)
{
	if (maxHealth <= 0.0f) maxHealth = 1.0f;
	float percent = health / maxHealth;
	if (percent < 0.0f) percent = 0.0f;
	if (percent > 1.0f) percent = 1.0f;

	int r, g;
	if (percent > 0.5f) {
		r = (int)((1.0f - percent) * 2.0f * 255.0f);
		g = 255;
	} else {
		r = 255;
		g = (int)(percent * 2.0f * 255.0f);
	}

	return IM_COL32(r, g, 0, 255);
}

// Helper: Draw a line between two 3D points if both are visible
static void Draw3DLine(ImDrawList* draw, const Vector3& p1, const Vector3& p2, 
                       double renderX, double renderY, double renderZ, ImU32 color, float thickness = 1.5f)
{
	Vector3 v1 = { p1.x - (float)renderX, p1.y - (float)renderY, p1.z - (float)renderZ };
	Vector3 v2 = { p2.x - (float)renderX, p2.y - (float)renderY, p2.z - (float)renderZ };
	
	Vector2 s1, s2;
	bool vis1 = CWorldToScreen::WorldToScreen(v1, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, s1);
	bool vis2 = CWorldToScreen::WorldToScreen(v2, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, s2);
	
	if (vis1 && vis2) {
		draw->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), color, thickness);
	}
}

// Render 3D wireframe box for a player
static void Render3DBox(ImDrawList* draw, float x, float y, float z, 
                        double renderX, double renderY, double renderZ, 
                        ImU32 boxCol, ImU32 outlineCol, ImU32 fillCol,
                        bool drawOutline, bool drawFilled)
{
	// Player hitbox dimensions (Minecraft standard)
	static constexpr float HALF_WIDTH = 0.3f;  // Total width = 0.6 blocks
	static constexpr float HEIGHT = 1.8f;       // Player height
	
	// 8 corners of the hitbox
	// Bottom face (Y = 0)
	Vector3 corners[8] = {
		{ x - HALF_WIDTH, y,          z - HALF_WIDTH }, // 0: bottom back-left
		{ x + HALF_WIDTH, y,          z - HALF_WIDTH }, // 1: bottom back-right
		{ x + HALF_WIDTH, y,          z + HALF_WIDTH }, // 2: bottom front-right
		{ x - HALF_WIDTH, y,          z + HALF_WIDTH }, // 3: bottom front-left
		{ x - HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH }, // 4: top back-left
		{ x + HALF_WIDTH, y + HEIGHT, z - HALF_WIDTH }, // 5: top back-right
		{ x + HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH }, // 6: top front-right
		{ x - HALF_WIDTH, y + HEIGHT, z + HALF_WIDTH }, // 7: top front-left
	};
	
	// Project all corners to screen space
	Vector2 screenCorners[8];
	bool visible[8];
	int visibleCount = 0;
	for (int i = 0; i < 8; i++) {
		Vector3 v = { corners[i].x - (float)renderX, corners[i].y - (float)renderY, corners[i].z - (float)renderZ };
		visible[i] = CWorldToScreen::WorldToScreen(v, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, screenCorners[i]);
		if (visible[i]) visibleCount++;
	}
	
	// Need at least 3 visible corners to render faces
	if (visibleCount < 3) return;
	
	// 6 faces of the box (each face is 4 corners in counter-clockwise order when viewed from outside)
	// We'll check if the face is front-facing using the cross product in screen space
	static constexpr int faces[6][4] = {
		{0, 1, 2, 3}, // Bottom (Y-)
		{7, 6, 5, 4}, // Top (Y+)
		{3, 2, 6, 7}, // Front (Z+)
		{1, 0, 4, 5}, // Back (Z-)
		{0, 3, 7, 4}, // Left (X-)
		{2, 1, 5, 6}, // Right (X+)
	};
	
	// Draw filled faces
	if (drawFilled) {
		for (int f = 0; f < 6; f++) {
			int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2], i3 = faces[f][3];
			
			// All 4 corners must be visible
			if (!visible[i0] || !visible[i1] || !visible[i2] || !visible[i3]) continue;
			
			// Check if face is front-facing using cross product in screen space
			// (p1 - p0) x (p2 - p0), check z component sign
			float ax = screenCorners[i1].x - screenCorners[i0].x;
			float ay = screenCorners[i1].y - screenCorners[i0].y;
			float bx = screenCorners[i2].x - screenCorners[i0].x;
			float by = screenCorners[i2].y - screenCorners[i0].y;
			float cross = ax * by - ay * bx;
			
			// In screen space (Y down), positive cross means counter-clockwise (front-facing)
			if (cross > 0) {
				ImVec2 points[4] = {
					ImVec2(screenCorners[i0].x, screenCorners[i0].y),
					ImVec2(screenCorners[i1].x, screenCorners[i1].y),
					ImVec2(screenCorners[i2].x, screenCorners[i2].y),
					ImVec2(screenCorners[i3].x, screenCorners[i3].y),
				};
				draw->AddConvexPolyFilled(points, 4, fillCol);
			}
		}
	}
	
	// 12 edges of the box
	static constexpr int edges[12][2] = {
		// Bottom face
		{0, 1}, {1, 2}, {2, 3}, {3, 0},
		// Top face
		{4, 5}, {5, 6}, {6, 7}, {7, 4},
		// Vertical edges
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};
	
	// Draw outline first (thicker, darker lines behind)
	if (drawOutline) {
		for (int i = 0; i < 12; i++) {
			int a = edges[i][0], b = edges[i][1];
			if (visible[a] && visible[b]) {
				draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y), 
				              ImVec2(screenCorners[b].x, screenCorners[b].y), outlineCol, 4.0f);
			}
		}
		// Draw outline corner circles
		for (int i = 0; i < 8; i++) {
			if (visible[i]) {
				draw->AddCircleFilled(ImVec2(screenCorners[i].x, screenCorners[i].y), 2.5f, outlineCol, 8);
			}
		}
	}
	
	// Draw main box lines
	for (int i = 0; i < 12; i++) {
		int a = edges[i][0], b = edges[i][1];
		if (visible[a] && visible[b]) {
			draw->AddLine(ImVec2(screenCorners[a].x, screenCorners[a].y), 
			              ImVec2(screenCorners[b].x, screenCorners[b].y), boxCol, 2.0f);
		}
	}
	
	// Draw corner circles for smooth rounded joints
	for (int i = 0; i < 8; i++) {
		if (visible[i]) {
			draw->AddCircleFilled(ImVec2(screenCorners[i].x, screenCorners[i].y), 1.5f, boxCol, 8);
		}
	}
}

void ESP::onRender()
{
	if (!RenderData::updated) return;
	
	// Prevent rendering if game loop is paused (loading, menu, etc)
	if (GetTickCount64() - lastUpdateTick > 300) return;
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	std::vector<EspPlayer> localCache;
	double renderX, renderY, renderZ;
	float partialTicks;
	double localX, localY, localZ;
	double localLastX, localLastY, localLastZ;
	
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		localCache = cachedPlayers;
		renderX = cachedRenderX;
		renderY = cachedRenderY;
		renderZ = cachedRenderZ;
		partialTicks = cachedPartialTicks;
		localX = cachedLocalX;
		localY = cachedLocalY;
		localZ = cachedLocalZ;
		localLastX = cachedLocalLastX;
		localLastY = cachedLocalLastY;
		localLastZ = cachedLocalLastZ;
	}
	
	// Fallback logic
	bool useFallback = (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0);
	if (useFallback) {
		renderX = localLastX + (localX - localLastX) * partialTicks;
		renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;
		renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
	}
	
	// Prepare static colors (outline doesn't change)
	auto outlineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.outlineColor[0], settings.outlineColor[1], settings.outlineColor[2], settings.outlineColor[3]));
	
	for (const auto& p : localCache)
	{
		if (!p.valid) continue;
		
		// Determine box color based on team/friend status
		ImU32 boxCol, fillCol;
		if (settings.teamColors && friendsManager && !p.uuid.empty()) {
			// Use FriendsManager to check if this player is friend/teammate
			bool isFriendOrTeam = friendsManager->shouldIgnoreByUUID(p.uuid, ChatColor::UNKNOWN, ChatColor::UNKNOWN);
			
			if (isFriendOrTeam) {
				// Friend/Teammate = Green
				boxCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.friendColor[0], settings.friendColor[1], settings.friendColor[2], settings.friendColor[3]));
				fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.friendColor[0], settings.friendColor[1], settings.friendColor[2], settings.filledColor[3]));
			} else {
				// Enemy = Red
				boxCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.enemyColor[0], settings.enemyColor[1], settings.enemyColor[2], settings.enemyColor[3]));
				fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.enemyColor[0], settings.enemyColor[1], settings.enemyColor[2], settings.filledColor[3]));
			}
		} else {
			// Team colors disabled or no UUID - use default colors
			boxCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3]));
			fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.filledColor[0], settings.filledColor[1], settings.filledColor[2], settings.filledColor[3]));
		}
		
		// Interpolate entity position
		float interpX = (float)(p.lastPosX + (p.posX - p.lastPosX) * partialTicks);
		float interpY = (float)(p.lastPosY + (p.posY - p.lastPosY) * partialTicks);
		float interpZ = (float)(p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks);
		
		// === 3D Box Rendering (handles its own filled) ===
		if (settings.box3D) {
			Render3DBox(draw, interpX, interpY, interpZ, renderX, renderY, renderZ, 
			            boxCol, outlineCol, fillCol, settings.outline, settings.filled);
		}
		
		// === 2D Projection for Box/HealthBar (skip filled if using 3D) ===
		bool needs2DBox = settings.box;
		bool needs2DFilled = settings.filled && !settings.box3D; // 3D handles its own fill
		bool needs2DHealth = settings.healthBar;
		
		if (!needs2DBox && !needs2DFilled && !needs2DHealth) continue;
		
		// Project head/feet for 2D overlays
		Vector3 feetPos{ interpX, interpY, interpZ };
		Vector3 headPos{ interpX, interpY + 1.9f, interpZ }; // 1.9m height
		
		// Apply camera relative translation
		feetPos.x -= (float)renderX; feetPos.y -= (float)renderY; feetPos.z -= (float)renderZ;
		headPos.x -= (float)renderX; headPos.y -= (float)renderY; headPos.z -= (float)renderZ;
		
		Vector2 screenFeet, screenHead;
		bool feetVisible = CWorldToScreen::WorldToScreen(feetPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, screenFeet);
		bool headVisible = CWorldToScreen::WorldToScreen(headPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, screenHead);
		
		// Skip 2D overlays if points not visible
		if (!feetVisible || !headVisible) continue;
		
		float height = screenFeet.y - screenHead.y;
		if (height < 1.0f) continue;
		
		float width = height * 0.5f;
		float left = screenFeet.x - width * 0.5f;
		float right = screenFeet.x + width * 0.5f;
		float top = screenHead.y;
		float bottom = screenFeet.y;
		
		// Filled Box (only with Box 2D mode - 3D handles its own fill)
		if (settings.filled && !settings.box3D) {
			draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), fillCol, 8.0f);
		}
		
		// 2D Box (only with Box 2D mode)
		if (settings.box) {
			if (settings.outline) {
				draw->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1), outlineCol, 8.0f);
				draw->AddRect(ImVec2(left + 1, top + 1), ImVec2(right - 1, bottom - 1), outlineCol, 8.0f);
			}
			draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxCol, 8.0f);
		}
		
		// Health Bar (works with both 2D and 3D modes)
		if (settings.healthBar) {
			float barWidth = 2.0f;
			float barDist = 3.0f;
			
			float fillHeight = height * (p.health / p.maxHealth);
			if (fillHeight > height) fillHeight = height;
			if (fillHeight < 0) fillHeight = 0;
			
			float barLeft = left - barDist - barWidth;
			float barRight = left - barDist;
			
			// Background
			draw->AddRectFilled(ImVec2(barLeft - 1, top - 1), ImVec2(barRight + 1, bottom + 1), IM_COL32(0,0,0,150), 4.0f);
			
			// Health
			ImU32 hpColor = GetHealthColor(p.health, p.maxHealth);
			draw->AddRectFilled(ImVec2(barLeft, bottom - fillHeight), ImVec2(barRight, bottom), hpColor, 4.0f);
		}
	}
}

void ESP::onRenderMenu()
{
	// Cache previous states to detect toggles
	bool prevBox2D = settings.box;
	bool prevBox3D = settings.box3D;
	
	ui::two_column(
		"Settings", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::Checkbox("Box 2D", &settings.box);
			ui::Checkbox("Box 3D", &settings.box3D);
			ui::Checkbox("Filled", &settings.filled);
			ui::Checkbox("Outline", &settings.outline);
			ui::Checkbox("Health Bar", &settings.healthBar);
			ui::Checkbox("Team Colors", &settings.teamColors);
			ui::SliderFloat("Range", &settings.range, 16.0f, 256.0f, "%.0fm");
		},
		"Colors", [this]() {
			if (settings.teamColors) {
				ui::color("Friend Color", settings.friendColor);
				ui::color("Enemy Color", settings.enemyColor);
			} else {
				ui::color("Box Color", settings.boxColor);
			}
			ui::color("Fill Color", settings.filledColor);
			ui::color("Outline Color", settings.outlineColor);
		}
	);
	
	// Mutually exclusive: if one was just enabled, disable the other
	if (settings.box && !prevBox2D) {
		settings.box3D = false;
	}
	if (settings.box3D && !prevBox3D) {
		settings.box = false;
	}
}

std::string ESP::getDisplayInfo() {
	if (settings.box3D) return "3D";
	if (settings.box) return "2D";
	return "";
}

