#pragma once

#include "../java.h"
#include <cstdint>
#include <string>

// ============================================================================
// ApolloHandshake - Sends Apollo/Lunar protocol handshake to servers
// ============================================================================
//
// This module implements the client-side Apollo protocol to:
// 1. Register the lunar:apollo plugin channel
// 2. Send PlayerHandshakeMessage with version info
// 3. Optionally send ping requests to validate connection
//
// This should trigger servers running Apollo to send us:
// - UpdateTeamMembersMessage (team data)
// - OverrideNametagMessage (nametag colors)
// - Other module-specific data
// ============================================================================

namespace ApolloHandshake {

// Initialize JNI references (call after Java::Init)
bool Init();

// Cleanup
void Shutdown();

// Check if ready to send
bool IsReady();

// Get current world serial (for tracking world changes)
uint64_t GetWorldSerial();

// Send the complete Apollo handshake sequence
// Should be called after S01PacketJoinGame/S07PacketRespawn
// Returns true if packets were sent successfully
bool SendHandshake(JNIEnv* env);

// Send a ping request to validate the channel is alive
// Returns true if packet was sent successfully
bool SendPingRequest(JNIEnv* env);

// Called when a world change is detected (from packet observer)
// This triggers the handshake sequence
void OnWorldChange(JNIEnv* env);

// Enable/disable debug logging
void SetDebugLogging(bool enabled);
bool IsDebugLogging();

// Statistics
struct Stats {
    uint64_t handshakesSent = 0;
    uint64_t pingsSent = 0;
    uint64_t responsesReceived = 0;
    int64_t lastHandshakeMs = 0;
    int64_t lastResponseMs = 0;
};

Stats GetStats();

// Called when we receive any Apollo response (for stats tracking)
void OnApolloResponse();

// Tick function - call from main loop to process pending handshakes
void Tick();

// Check if there's a pending handshake waiting to be sent
bool HasPendingHandshake();

// Request a nametag refresh by sending a new handshake
// Use this when you need updated nametag data
void RequestNametagRefresh();

// Send OverrideConfigurableSettingsMessage to enable nametag/team modules
bool SendConfigurableSettings(JNIEnv* env);

// Request nametag data from server (sends handshake + config + ping)
// Call this with a keybind to force the server to send nametag info
// Returns true if at least one packet was sent successfully
bool RequestNametagData();

}  // namespace ApolloHandshake
