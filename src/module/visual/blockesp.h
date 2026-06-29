#pragma once

#include <mutex>
#include <vector>
#include "../../jni/java.h"
#include "../module.h"

// Forward declare
struct ImDrawList;

// Cached block data for rendering
struct BlockData {
    int blockId = 0;
    int x = 0, y = 0, z = 0;
    float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    bool valid = false;
};

class BlockESP : public Module {
public:
    BlockESP();

    void onEnable() override;
    void onDisable() override;
    void onUpdate() override;
    void onRender() override;
    void onRenderMenu() override;
    void saveSettings(json::Value& node) const override;
    void loadSettings(const json::Value& node) override;

    struct Settings {
        // Visual settings
        float range = 20.0f;  // Scan radius in blocks
        int opacity = 35;     // 0-100%
        bool fadeWithDistance = true;
    } settings;

    bool enabledBlockIds[256];
    float blockColors[256][3];

private:
    // Thread-safe block cache
    std::mutex cacheMutex;
    std::vector<BlockData> cachedBlocks;

    // Scan state
    long long lastScanTime = 0;
    double lastScanX = 0, lastScanY = 0, lastScanZ = 0;

    // Render thread data
    double cachedLocalX = 0, cachedLocalY = 0, cachedLocalZ = 0;
    double cachedLocalLastX = 0, cachedLocalLastY = 0, cachedLocalLastZ = 0;
    double cachedRenderX = 0, cachedRenderY = 0, cachedRenderZ = 0;
    float cachedPartialTicks = 0.0f;
    unsigned int lastSettingsSignature = 0;

    // Rendering safety
    long long lastUpdateTick = 0;

    // Scan for blocks around player
    void scanBlocks();

    // Get block color based on type
    void getBlockColor(int blockId, float* outColor);

    void parseBlockColorEntry(const std::string& entry);
};

extern BlockESP* blockESP;
