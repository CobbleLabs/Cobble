#pragma once

#include <atomic>
#include <mutex>
#include <vector>
#include "java.h"

// ============================================================================
// PacketInterceptor - JVMTI-based packet filtering for Blink
//
// Intercepts NetworkManager.sendPacket() calls at the Java level,
// allowing hybrid buffering of movement packets while keeping
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

// Criticals control - independent packet delay for crit timing
void setCriticalsActive(bool active);
bool isCriticalsActive();

// Queue management
int getQueuedCount();
void flushQueue();  // Send all queued packets
void clearQueue();  // Discard queued packets
void Tick();        // Drive hybrid queue (continuous movement stream)

// Debug packet logging - logs all intercepted packets to console
void setDebugPacketLogging(bool enabled);
bool isDebugPacketLogging();

// Debug info
void printDebugInfo();

// Hybrid debug: last sent server timeline position
bool getLastSentPosition(double& x, double& y, double& z);

// Internal - called from Java via registered native
// Returns true if packet should be sent, false to block
bool shouldSendPacket(JNIEnv* env, jobject packet);

}  // namespace PacketInterceptor
