#pragma once

#include "../module.h"
#include "../../jni/java.h"
#include <vector>
#include <mutex>

// Forward declare
struct ImDrawList;

// Cached player data - stores world positions for interpolation
struct CachedPlayer {
	std::string name;
	double posX = 0, posY = 0, posZ = 0;           // Current position
	double lastPosX = 0, lastPosY = 0, lastPosZ = 0; // Last tick position
	float distance = 0;
	float health = 20.0f;
	float maxHealth = 20.0f;
	bool valid = false;
};

class NameTags : public Module
{
public:
	NameTags();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;  // Cache player positions (main thread)
	void onRender() override;  // Project positions to screen (render thread)

	struct Settings {
		float range = 64.0f;
		float scale = 1.0f;
		bool background = true;
		bool showHealth = true;
		bool showDistance = true;
	} settings;

private:
	int debugCounter = 0;
	bool matrixDebugDone = false;
	
	// Thread-safe player cache
	std::mutex cacheMutex;
	std::vector<CachedPlayer> cachedPlayers;
	double cachedRenderX = 0, cachedRenderY = 0, cachedRenderZ = 0;
	float cachedPartialTicks = 0.0f;
	
	// World change detection
	jobject lastWorldRef = nullptr;
	void checkWorldChange();
	
	void renderPlayerTag(ImDrawList* draw, float screenX, float screenY, const char* name, float distance, float health, float maxHealth);
};

extern NameTags* nameTags;
