#pragma once

#include "../java.h"

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

}  // namespace PacketInterceptor
