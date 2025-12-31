#include "nametags.h"
#include "../../sdk/minecraft.h"
#include "../../sdk/render_data.h"
#include "../../util/logger.hpp"
#include "../../util/math/worldToScreen.h"
#include "../../ui/widgets/widgets.hpp"

#include "../../../ext/imgui/imgui.h"
#include <cmath>
#include <cstdio>

NameTags* nameTags = nullptr;

NameTags::NameTags() : Module("NameTags")
{
	keybind = 0;  // No default keybind
}

void NameTags::onEnable()
{
	log_ok("NameTags enabled");
	debugCounter = 0;
	matrixDebugDone = false;
	lastWorldRef = nullptr;  // Force world change detection on first update
	
	// Reset RenderData to force fresh matrices
	RenderData::initialized = false;
	RenderData::initFailed = false;
	RenderData::initRetryCount = 0;
	RenderData::Reset();
	
	// Clear any stale cached players
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedPlayers.clear();
}

void NameTags::onDisable()
{
	log_ok("NameTags disabled");
	
	// Clean up world ref
	/*if (lastWorldRef && Java::Env) {
		Java::Env->DeleteGlobalRef(lastWorldRef);
	}*/
	lastWorldRef = nullptr;
	
	std::lock_guard<std::mutex> lock(cacheMutex);
	cachedPlayers.clear();
}

// Detect world change (server join/leave, dimension change)
// Reinitializes RenderData when world reference changes
void NameTags::checkWorldChange()
{
	if (!Java::Env || !Java::Initialized) return;
	
	jobject currentWorld = mc.getWorld();
	
	// First time or world changed
	bool worldChanged = false;
	
	if (lastWorldRef == nullptr && currentWorld != nullptr) {
		// First world load
		worldChanged = true;
	} else if (lastWorldRef != nullptr && currentWorld == nullptr) {
		// Left server
		worldChanged = true;
	} else if (lastWorldRef != nullptr && currentWorld != nullptr) {
		// Check if same object
		if (!Java::Env->IsSameObject(lastWorldRef, currentWorld)) {
			worldChanged = true;
		}
	}
	
	if (worldChanged) {
		// Clear cache
		{
			std::lock_guard<std::mutex> lock(cacheMutex);
			cachedPlayers.clear();
		}
		
		// Force RenderData to reinitialize
		RenderData::initialized = false;
		RenderData::initFailed = false;
		RenderData::initRetryCount = 0;
		RenderData::Reset();
		
		matrixDebugDone = false;
		
		// Clean up old global ref
		if (lastWorldRef) {
			Java::Env->DeleteGlobalRef(lastWorldRef);
			lastWorldRef = nullptr;
		}
		
		// Store new global ref
		if (currentWorld) {
			lastWorldRef = Java::Env->NewGlobalRef(currentWorld);
		}
	}
	
	// Clean up local ref
	if (currentWorld) {
		Java::Env->DeleteLocalRef(currentWorld);
	}
}

// Called from MAIN thread - cache player positions and render info
void NameTags::onUpdate()
{
	// Check for world changes first
	// checkWorldChange();
	
	// If player is not valid (in menu, left server, etc), clear cache
	if (!player.isValid()) {
		std::lock_guard<std::mutex> lock(cacheMutex);
		cachedPlayers.clear();
		return;
	}
	
	// Update render matrices from Java
	RenderData::Update();
	
	// Get render position and partial ticks
	RenderPos renderPos = mc.getRenderPos();
	float partialTicks = mc.getPartialTicks();
	
	// Debug once
	if (!matrixDebugDone) {
		matrixDebugDone = true;
		
		char buf[256];
		snprintf(buf, sizeof(buf), "RenderData initialized: %s, updated: %s", 
			RenderData::initialized ? "yes" : "no",
			RenderData::updated ? "yes" : "no");
		log_debug(buf);
		
		snprintf(buf, sizeof(buf), "RenderPos: %.2f, %.2f, %.2f, partialTicks: %.3f", 
			renderPos.x, renderPos.y, renderPos.z, partialTicks);
		log_debug(buf);
		
		snprintf(buf, sizeof(buf), "Screen: %dx%d", 
			RenderData::screenWidth, RenderData::screenHeight);
		log_debug(buf);
	}
	
	// Get local player position for distance calculation
	double localX = player.getPosX();
	double localY = player.getPosY();
	double localZ = player.getPosZ();
	
	// Get player list from Java
	auto players = mc.getPlayerList();
	
	// Build new cache
	std::vector<CachedPlayer> newCache;
	
	for (auto& p : players)
	{
		double dx = p.posX - localX;
		double dy = p.posY - localY;
		double dz = p.posZ - localZ;
		float dist = (float)std::sqrt(dx * dx + dy * dy + dz * dz);
		
		// Skip if out of range or too close (self)
		if (dist > settings.range || dist < 0.5f) {
			if (p.instance) {
				Java::Env->DeleteLocalRef(p.instance);
				p.instance = nullptr;
			}
			continue;
		}
		
		CachedPlayer cached;
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
	
	// Thread-safe swap
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		cachedPlayers = std::move(newCache);
		cachedRenderX = renderPos.x;
		cachedRenderY = renderPos.y;
		cachedRenderZ = renderPos.z;
		cachedPartialTicks = partialTicks;
	}
}

// Called from RENDER thread
void NameTags::onRender()
{
	debugCounter++;
	bool shouldLog = (debugCounter % 120 == 0);
	
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!draw) return;
	
	if (!RenderData::updated) return;
	
	// Copy cache safely
	std::vector<CachedPlayer> localCache;
	double renderX, renderY, renderZ;
	float partialTicks;
	{
		std::lock_guard<std::mutex> lock(cacheMutex);
		localCache = cachedPlayers;
		renderX = cachedRenderX;
		renderY = cachedRenderY;
		renderZ = cachedRenderZ;
		partialTicks = cachedPartialTicks;
	}
	
	if (localCache.empty()) return;
	
	int renderedCount = 0;
	
	for (auto& p : localCache)
	{
		if (!p.valid) continue;
		
		// origin = renderPos - entityLastPos + (entityLastPos - entityPos) * renderPartialTicks
		// This interpolates the entity position between ticks for smooth movement
		
		float entityHeight = 2.0f;  // Above head
		
		// Point = renderPos - (entityLastPos interpolated) + offset
		float interpX = (float)(p.lastPosX + (p.posX - p.lastPosX) * partialTicks);
		float interpY = (float)(p.lastPosY + (p.posY - p.lastPosY) * partialTicks) + entityHeight;
		float interpZ = (float)(p.lastPosZ + (p.posZ - p.lastPosZ) * partialTicks);
		
		// Position relative to render origin (camera)
		float relX = interpX - (float)renderX;
		float relY = interpY - (float)renderY;
		float relZ = interpZ - (float)renderZ;
		
		Vector3 worldPos{ relX, relY, relZ };
		Vector2 screenPos;
		
		bool onScreen = CWorldToScreen::WorldToScreen(
			worldPos,
			RenderData::modelView,
			RenderData::projection,
			RenderData::screenWidth,
			RenderData::screenHeight,
			screenPos
		);
		
		if (onScreen) {
			renderPlayerTag(draw, screenPos.x, screenPos.y, p.name.c_str(), p.distance, p.health, p.maxHealth);
			renderedCount++;
		}
	}
	
	if (shouldLog) {
		char buf[64];
		snprintf(buf, sizeof(buf), "NameTags: %zu cached, %d rendered", localCache.size(), renderedCount);
		log_debug(buf);
	}
}

void NameTags::renderPlayerTag(ImDrawList* draw, float screenX, float screenY, const char* name, float distance, float health, float maxHealth)
{
	if (!draw) return;
	
	float displayScale = settings.scale;
	
	// Build text
	char displayText[128];
	if (settings.showHealth && settings.showDistance) {
		snprintf(displayText, sizeof(displayText), "%s [%.0f HP] [%.1fm]", name, health, distance);
	} else if (settings.showHealth) {
		snprintf(displayText, sizeof(displayText), "%s [%.0f HP]", name, health);
	} else if (settings.showDistance) {
		snprintf(displayText, sizeof(displayText), "%s [%.1fm]", name, distance);
	} else {
		snprintf(displayText, sizeof(displayText), "%s", name);
	}
	
	ImVec2 textSize = ImGui::CalcTextSize(displayText);
	float posX = screenX - textSize.x / 2.0f;
	float posY = screenY - textSize.y;  // Above the point
	
	// Background
	if (settings.background) {
		float padding = 4.0f * displayScale;
		float bgHeight = textSize.y + padding * 2;
		if (settings.showHealth) bgHeight += 6.0f * displayScale; // Space for health bar
		
		draw->AddRectFilled(
			ImVec2(posX - padding, posY - padding),
			ImVec2(posX + textSize.x + padding, posY + bgHeight),
			IM_COL32(0, 0, 0, 160),
			3.0f);
	}
	
	// Text with shadow
	draw->AddText(ImVec2(posX + 1, posY + 1), IM_COL32(0, 0, 0, 255), displayText);
	draw->AddText(ImVec2(posX, posY), IM_COL32(255, 255, 255, 255), displayText);
	
	// Health bar
	if (settings.showHealth && maxHealth > 0) {
		float barWidth = textSize.x;
		float barHeight = 4.0f * displayScale;
		float barY = posY + textSize.y + 3.0f;
		float healthPercent = std::fmax(0.0f, std::fmin(1.0f, health / maxHealth));
		
		// Background bar
		draw->AddRectFilled(
			ImVec2(posX, barY),
			ImVec2(posX + barWidth, barY + barHeight),
			IM_COL32(50, 50, 50, 200), 2.0f);
		
		// Health fill (green to red gradient based on health)
		int r = (int)(255 * (1.0f - healthPercent));
		int g = (int)(255 * healthPercent);
		draw->AddRectFilled(
			ImVec2(posX, barY),
			ImVec2(posX + barWidth * healthPercent, barY + barHeight),
			IM_COL32(r, g, 0, 255), 2.0f);
	}
}

void NameTags::onRenderMenu()
{
	ui::two_column(
		"Settings", [this]() {
			ui::CheckboxBind("Enabled", &enabled, &keybind);
			ImGui::Spacing();
			ui::SliderFloat("Scale", &settings.scale, 0.5f, 2.0f, "%.1f");
			ui::Checkbox("Background", &settings.background);
		},
		"Display", [this]() {
			ui::Checkbox("Show Health", &settings.showHealth);
			ui::Checkbox("Show Distance", &settings.showDistance);
		}
	);
}
