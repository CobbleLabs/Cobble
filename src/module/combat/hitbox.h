#pragma once

#include "../module.h"
#include "../../jni/java.h"
#include <vector>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

struct HitboxPlayerCache {
	float interpX, interpY, interpZ;
	bool valid = false;
};

class Hitbox : public Module
{
public:
	Hitbox();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRender() override;
	void onRenderMenu() override;
	std::string getDisplayInfo() override;

	struct Settings {
		float expansion = 0.1f;       // Hitbox expansion (0.0 - 1.0)
		bool showHitbox = false;      // Visualize expanded hitbox
		bool filled = false;          // Fill the hitbox box
		float hitboxColor[4] = { 1.0f, 0.4f, 0.4f, 0.7f }; // RGBA
	} settings;

private:
	// Entity override state
	struct OverrideState {
		jobject originalEntityHit = nullptr;
		jobject originalTypeOfHit = nullptr;
		bool needsRestore = false;
	} overrideState;
	
	// Thread-safe cache for rendering
	std::mutex renderCacheMutex;
	std::vector<HitboxPlayerCache> renderCache;
	float cachedRenderX = 0, cachedRenderY = 0, cachedRenderZ = 0;
	ULONGLONG lastUpdateTick = 0;
	
	void restoreEntityHit(JNIEnv* env);
	bool rayIntersectsAABB(float ox, float oy, float oz, float dx, float dy, float dz,
	                        float minX, float minY, float minZ, float maxX, float maxY, float maxZ, float maxDist);
};

extern Hitbox* hitbox;
