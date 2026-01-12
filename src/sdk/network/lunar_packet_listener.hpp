#pragma once

#include <functional>
#include <string>
#include <vector>
#include "../../jni/java.h"

// ============================================================================
// LunarPacketListener - Intercepts Lunar Client API packets
//
// Specifically targets LCPacketNametagsUpdate and LCPacketNametagsOverride
// for team/friend detection purposes.
// ============================================================================

namespace LunarPacketListener {

// Initialize the listener (call after Java::Init)
bool Init();

// Cleanup
void Shutdown();

// Check if ready
bool IsReady();

// Enable/disable debug logging
void setDebugLogging(bool enabled);
bool isDebugLogging();

// Callback types for nametag packets
struct NametagEntry {
    std::string playerName;
    std::string uuid;
    std::vector<std::string> lines;  // Multi-line nametag content
};

using NametagUpdateCallback = std::function<void(const std::vector<NametagEntry>& entries)>;
using NametagOverrideCallback =
    std::function<void(const std::string& playerUuid, const std::vector<std::string>& lines)>;

// Register callbacks
void onNametagUpdate(NametagUpdateCallback callback);
void onNametagOverride(NametagOverrideCallback callback);

// Poll for new packets (call from main loop)
void Tick();

// Process incoming packet - returns true if it was a Lunar nametag packet
bool processIncomingPacket(JNIEnv* env, jobject packet);

// Get cached class references for external use
jclass getNametagsUpdateClass();
jclass getNametagsOverrideClass();

}  // namespace LunarPacketListener
