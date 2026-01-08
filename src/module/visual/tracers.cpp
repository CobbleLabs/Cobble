#include "tracers.h"
#include "../../sdk/core/minecraft.h"
#include "../../sdk/player/render_data.h"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../../infra/ext/imgui/imgui.h"
#include <cmath>
#include <algorithm>

Tracers* tracers = nullptr;

Tracers::Tracers() : Module("Tracers")
{
	keybind = 0;
}

void Tracers::onEnable()
{
	log_ok("Tracers enabled");
	lastWorldRef = nullptr;
	
	// Reset RenderData
	RenderData::initialized = false;
	RenderData::Reset();
	
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedPlayers.clear();
}

void Tracers::onDisable()
{
	log_ok("Tracers disabled");
	lastWorldRef = nullptr;
	
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedPlayers.clear();
}

void Tracers::checkWorldChange()
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

void Tracers::onUpdate()
{
	lastUpdateTick = GetTickCount64();

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
	std::vector<TracerPlayer> newCache;
	
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
		
		TracerPlayer cached;
		cached.name = p.name;
		cached.posX = p.posX;
		cached.posY = p.posY;
		cached.posZ = p.posZ;
		cached.lastPosX = p.lastPosX;
		cached.lastPosY = p.lastPosY;
		cached.lastPosZ = p.lastPosZ;
		cached.distance = dist;
		cached.health = p.health;
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

// Helper: Get color based on distance
static ImU32 GetDistanceColor(float distance, float maxRange)
{
	float percent = 1.0f - (distance / maxRange);
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

// Helper: Get color based on health
static ImU32 GetHealthColor(float health)
{
	float percent = health / 20.0f;
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

void Tracers::onRender()
{
	if (!RenderData::updated) return;
	
	// Prevent rendering if game loop is paused
	if (GetTickCount64() - lastUpdateTick > 300) return;
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	std::vector<TracerPlayer> localCache;
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
	
	// Fallback logic for render position
	bool useFallback = (renderX == 0.0 && renderY == 0.0 && renderZ == 0.0);
	if (useFallback) {
		renderX = localLastX + (localX - localLastX) * partialTicks;
		renderY = localLastY + (localY - localLastY) * partialTicks + 1.62;  // Eye height
		renderZ = localLastZ + (localZ - localLastZ) * partialTicks;
	}
	
	// Determine start position for tracers
	ImVec2 startPos;
	if (settings.lineFrom == 0) {
		// Center of screen
		startPos = ImVec2(RenderData::screenWidth / 2.0f, RenderData::screenHeight / 2.0f);
	} else {
		// Bottom center of screen
		startPos = ImVec2(RenderData::screenWidth / 2.0f, RenderData::screenHeight);
	}
	
	// Draw tracers to each player
	for (const auto& p : localCache)
	{
		if (!p.valid) continue;
		
		// Interpolate entity position
		float interpX = (float)(p.lastPosX + (p.posX - p.lastPosX) * partialTicks);
		float interpY = (float)(p.lastPosY + (p.posY - p.lastPosY) * partialTicks) + 0.9f;  // Center of player
		float interpZ = (float)(p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks);
		
		// Convert to camera-relative coordinates
		Vector3 targetPos{
			interpX - (float)renderX,
			interpY - (float)renderY,
			interpZ - (float)renderZ
		};
		
		// Project to screen
		Vector2 screenPos;
		bool visible = CWorldToScreen::WorldToScreen(targetPos, RenderData::modelView, RenderData::projection, 
		                                              RenderData::screenWidth, RenderData::screenHeight, screenPos);
		
		if (!visible) continue;
		
		// Determine color
		ImU32 color;
		if (settings.colorByHealth) {
			color = GetHealthColor(p.health);
		} else if (settings.colorByDistance) {
			color = GetDistanceColor(p.distance, settings.range);
		} else {
			// Use custom color
			color = ImGui::ColorConvertFloat4ToU32(ImVec4(
				settings.tracerColor[0],
				settings.tracerColor[1],
				settings.tracerColor[2],
				settings.tracerColor[3]
			));
		}
		
		// Draw tracer line
		draw->AddLine(startPos, ImVec2(screenPos.x, screenPos.y), color, settings.thickness);
	}
}

void Tracers::onRenderMenu()
{
	ui::two_column(
		"Settings", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			
			// Line start position
			const char* lineFromOptions[] = { "Center", "Bottom" };
			ui::Combo("Line From", &settings.lineFrom, lineFromOptions, 2);
			
			ui::SliderFloat("Thickness", &settings.thickness, 1.0f, 5.0f, "%.1fpx");
			ui::SliderFloat("Range", &settings.range, 16.0f, 256.0f, "%.0fm");
		},
		"Colors", [this]() {
			ui::Checkbox("Color by Distance", &settings.colorByDistance);
			ui::Checkbox("Color by Health", &settings.colorByHealth);
			
			if (!settings.colorByDistance && !settings.colorByHealth) {
				ui::color("Tracer Color", settings.tracerColor);
			}
		}
	);
	
	// Make color modes mutually exclusive
	if (settings.colorByDistance) {
		settings.colorByHealth = false;
	}
}
