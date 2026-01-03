#pragma once

#include "java.h"
#include <vector>
#include <mutex>
#include <atomic>

// ============================================================================
// PacketInterceptor - JVMTI-based packet filtering for Blink
//
// Intercepts NetworkManager.sendPacket() calls at the Java level,
// allowing selective blocking of movement packets while keeping
// keepalives and other traffic flowing.
// ============================================================================

namespace PacketInterceptor {

// Initialize the packet interceptor
// Must be called after Java::Init() and with tiEnv available
bool Init();

// Cleanup
void Shutdown();

// Check if initialized successfully
bool IsReady();

// Blink control
void setBlinkActive(bool active);
bool isBlinkActive();

// Queue management
int getQueuedCount();
void flushQueue();  // Send all queued packets
void clearQueue();  // Discard queued packets
void Tick();  // Drain queued packets after Blink deactivates

// Debug info
void printDebugInfo();

// Internal - called from Java via registered native
// Returns true if packet should be sent, false to block
bool shouldSendPacket(JNIEnv* env, jobject packet);

} // namespace PacketInterceptor
