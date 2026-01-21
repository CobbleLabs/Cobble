#pragma once

#include "java.h"

// ============================================================================
// PacketInterceptor - Apollo Teams packet interception
// Lunar Client team/nametag packets for friend/team detection.
// ============================================================================

namespace PacketInterceptor {

// Initialize the packet interceptor
bool Init();

// Cleanup
void Shutdown();

// Check if initialized successfully
bool IsReady();

// Called each frame to process queued packets
void Tick();

// Debug logging
void setDebugPacketLogging(bool enabled);
bool isDebugPacketLogging();

// Debug info
void printDebugInfo();

// Internal - called from Java via registered native
// Returns true if packet should be sent, false to block
bool shouldSendPacket(JNIEnv* env, jobject packet);

}  // namespace PacketInterceptor
