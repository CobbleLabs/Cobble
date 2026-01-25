#pragma once

#include "../java.h"
#include <deque>
#include <mutex>
#include <chrono>

// BacktrackGate - Intercepts incoming entity position packets for Lag Based mode
//
// Similar to BlinkGate but for INCOMING packets (S14, S18)
// Delays entity position updates when advantageous for hitting

namespace BacktrackGate {

struct DelayedPacket {
    jobject packet;  // GlobalRef
    std::chrono::steady_clock::time_point timestamp;
    int entityId;
};

// Initialize the gate (called during packet interceptor init)
void Init();

// Shutdown and cleanup
void Shutdown();

// Check if an incoming entity packet should be delayed
// Returns true if packet should be processed normally, false if delayed
bool ShouldProcessPacket(JNIEnv* env, jobject packet);

// Get packets that are ready to be released (exceeded latency threshold)
std::deque<DelayedPacket> GetReadyPackets();

// Flush all delayed packets immediately
void FlushAll(JNIEnv* env);

// Get count of currently delayed packets
int GetDelayedCount();

// Native callback for JVMTI hook
jboolean JNICALL NativeShouldProcess(JNIEnv* env, jclass, jobject packet);

}  // namespace BacktrackGate
