#pragma once

#include "../module.h"
#include "../../jni/java.h"
#include <vector>
#include <mutex>

// Forward declare
struct ImDrawList;

// Cached block data for rendering
struct BlockData {
	int blockId = 0;
	int x = 0, y = 0, z = 0;
	float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	bool valid = false;
};

class BlockESP : public Module
{
public:
	BlockESP();

	void onEnable() override;
	void onDisable() override;
	void onUpdate() override;
	void onRender() override;
	void onRenderMenu() override;

	struct Settings {
		// Block types
		bool drawDiamond = true;
		bool drawEmerald = true;
		bool drawIron = true;
		bool drawGold = true;
		bool drawLapis = false;
		bool drawRedstone = false;
		bool drawCoal = false;
		bool drawChests = true;
		
		// Scan settings
		float scanRadius = 25.0f;
		float scanInterval = 5.0f;  // Seconds between scans
		
		// Visual settings
		float alpha = 0.5f;
		bool distanceFade = true;
	} settings;

private:
	// Thread-safe block cache
	std::mutex cacheMutex;
	std::vector<BlockData> cachedBlocks;
	
	// Scan state
	long long lastScanTime = 0;
	
	// Render thread data
	double cachedLocalX = 0, cachedLocalY = 0, cachedLocalZ = 0;
	double cachedLocalLastX = 0, cachedLocalLastY = 0, cachedLocalLastZ = 0;
	
	// Rendering safety
	long long lastUpdateTick = 0;
	
	// Scan for blocks around player
	void scanBlocks();
	
	// Get block color based on type
	void getBlockColor(int blockId, float* outColor);
};

extern BlockESP* blockESP;
