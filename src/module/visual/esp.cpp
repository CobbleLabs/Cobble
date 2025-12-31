#include "esp.h"
#include "../../sdk/minecraft.h"
#include "../../sdk/render_data.h"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../ui/widgets/widgets.hpp"
#include "../../../ext/imgui/imgui.h"
#include <cmath>
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
	
	for (const auto& p : localCache)
	{
		if (!p.valid) continue;
		
		// Interpolate entity position
		float interpX = (float)(p.lastPosX + (p.posX - p.lastPosX) * partialTicks);
		float interpY = (float)(p.lastPosY + (p.posY - p.lastPosY) * partialTicks);
		float interpZ = (float)(p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks);
		
		// 2D Box Projection (Head/Feet)
		Vector3 feetPos{ interpX, interpY, interpZ };
		Vector3 headPos{ interpX, interpY + 1.9f, interpZ }; // 1.9m height
		
		// Apply camera relative translation
		feetPos.x -= (float)renderX; feetPos.y -= (float)renderY; feetPos.z -= (float)renderZ;
		headPos.x -= (float)renderX; headPos.y -= (float)renderY; headPos.z -= (float)renderZ;
		
		Vector2 screenFeet, screenHead;
		bool feetVisible = CWorldToScreen::WorldToScreen(feetPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, screenFeet);
		bool headVisible = CWorldToScreen::WorldToScreen(headPos, RenderData::modelView, RenderData::projection, RenderData::screenWidth, RenderData::screenHeight, screenHead);
		
		if (!feetVisible && !headVisible) continue;
		
		// If clipped, we skip to avoid infinite lines
		// Typically with robust clipping we could draw partial boxes, but simply skipping mixed visibility is safer for now
		if (!feetVisible || !headVisible) continue;
		
		float height = screenFeet.y - screenHead.y;
		if (height < 1.0f) continue;
		
		float width = height * 0.5f;
		float left = screenFeet.x - width * 0.5f;
		float right = screenFeet.x + width * 0.5f;
		float top = screenHead.y;
		float bottom = screenFeet.y;
		
		// Helpers
		auto boxCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.boxColor[0], settings.boxColor[1], settings.boxColor[2], settings.boxColor[3]));
		auto fillCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.filledColor[0], settings.filledColor[1], settings.filledColor[2], settings.filledColor[3]));
		auto outlineCol = ImGui::ColorConvertFloat4ToU32(ImVec4(settings.outlineColor[0], settings.outlineColor[1], settings.outlineColor[2], settings.outlineColor[3]));
		
		// Filled Box
		if (settings.filled) {
			draw->AddRectFilled(ImVec2(left, top), ImVec2(right, bottom), fillCol, 8.0f);
		}
		
		// Box Outline
		if (settings.box) {
			if (settings.outline) {
				draw->AddRect(ImVec2(left - 1, top - 1), ImVec2(right + 1, bottom + 1), outlineCol, 8.0f);
				draw->AddRect(ImVec2(left + 1, top + 1), ImVec2(right - 1, bottom - 1), outlineCol, 8.0f);
			}
			draw->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxCol, 8.0f);
		}
		
		// Health Bar
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
	ui::two_column(
		"Settings", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::Checkbox("Box", &settings.box);
			ui::Checkbox("Filled", &settings.filled);
			ui::Checkbox("Outline", &settings.outline);
			ui::Checkbox("Health Bar", &settings.healthBar);
			ui::SliderFloat("Range", &settings.range, 16.0f, 256.0f, "%.0fm");
		},
		"Colors", [this]() {
			ui::color("Box Color", settings.boxColor);
			ui::color("Fill Color", settings.filledColor);
			ui::color("Outline Color", settings.outlineColor);
		}
	);
}
